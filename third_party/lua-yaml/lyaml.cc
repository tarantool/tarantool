/*
 * lyaml.c, LibYAML binding for Lua
 *
 * Copyright (c) 2009, Andrew Danforth <acd@weirdness.net>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * Portions of this software were inspired by Perl's YAML::LibYAML module by
 * Ingy döt Net <ingy@cpan.org>
 *
 */

#include "lyaml.h"

#include "trivia/util.h"

#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <inttypes.h>

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
#include <lj_obj.h>
#include <lj_ctype.h>
#include <lj_cdata.h>
#include <lj_cconv.h>
#include <lj_state.h>

#include "yaml.h"
} /* extern "C" */

#include "base64.h"
#include "lua/utils.h"
#include "lua/serializer.h"
#include "lib/core/decimal.h"
#include "mp_extension_types.h" /* MP_DECIMAL, MP_UUID */
#include "tt_uuid.h" /* tt_uuid_to_string(), UUID_STR_LEN */
#include "tweaks.h"

#define LUAYAML_TAG_PREFIX "tag:yaml.org,2002:"

#define OOM_ERRMSG "yaml: out of memory"

#define RETURN_ERRMSG(s, msg) do { \
      lua_pushstring(s->L, msg); \
      s->error = 1; \
      return; \
   } while(0)

struct lua_yaml_loader {
   lua_State *L;
   struct luaL_serializer *cfg;
   int anchortable_index;
   int document_count;
   yaml_parser_t parser;
   yaml_event_t event;
   char validevent;
   char error;
};

struct lua_yaml_dumper {
   lua_State *L;
   struct luaL_serializer *cfg;
   int anchortable_index;
   unsigned int anchor_number;
   yaml_emitter_t emitter;
   char error;
   /** Global tag to label the result document by. */
   yaml_tag_directive_t begin_tag;
   /**
    * - end_tag == &begin_tag - a document is not labeld with a
    * global tag.
    * - end_tag == &begin_tag + 1 - a document is labeled with a
    * global tag specified in begin_tag attribute. End_tag pointer
    * is used instead of tag count because of libyaml API - it
    * takes begin and end pointers of tags array.
    */
   yaml_tag_directive_t *end_tag;

   lua_State *outputL;
   luaL_Buffer yamlbuf;
   int reftable_index;
};

/**
 * By default, all strings that contain '\n' are encoded in the block scalar
 * style. Setting this flag to false, makes the encoder use default yaml style
 * with excessive newlines for all strins without "\n\n" substring. This is a
 * compatibility-only feature.
 */
static bool yaml_pretty_multiline = true;
TWEAK_BOOL(yaml_pretty_multiline);

/**
 * If this flag is set, a binary data field will be decoded to a plain Lua
 * string, not a varbinary object.
 */
static bool yaml_decode_binary_as_string = false;
TWEAK_BOOL(yaml_decode_binary_as_string);

/**
 * Verify whether a string represents a boolean literal in YAML.
 *
 * Non-standard: only subset of YAML 1.1 boolean literals are
 * treated as boolean values.
 *
 * @param str Literal to check.
 * @param len Length of @a str.
 * @param[out] out Result boolean value.
 *
 * @retval Whether @a str represents a boolean value.
 */
static inline bool
yaml_is_bool(const char *str, size_t len, bool *out)
{
   if ((len == 5 && memcmp(str, "false", 5) == 0) ||
       (len == 2 && memcmp(str, "no", 2) == 0)) {
      *out = false;
      return true;
   }
   if ((len == 4 && memcmp(str, "true", 4) == 0) ||
       (len == 3 && memcmp(str, "yes", 3) == 0)) {
      *out = true;
      return true;
   }
   return false;
}

/**
 * Verify whether a string represents a null literal in YAML.
 *
 * Non-standard: don't match an empty string, 'Null' and 'NULL' as
 * null.
 *
 * @param str Literal to check.
 * @param len Length of @a str.
 *
 * @retval Whether @a str represents a null value.
 */
static inline bool
yaml_is_null(const char *str, size_t len)
{
   if (len == 1 && str[0] == '~')
      return true;
   if (len == 4 && memcmp(str, "null", 4) == 0)
      return true;
   return false;
}

/**
 * Verify whether a string represents a number literal in YAML.
 *
 * Non-standard:
 *
 * False-positives:
 * - 'inf', 'nan' literals despite the case are parsed as numbers
 *   (the standard specifies only 'inf', 'Inf', 'INF', 'nan',
 *   'NaN', 'NAN').
 * - 'infinity' (ignoring case) is considered a number.
 * - Binary literals ('0b...') are considered numbers.
 *
 * Bugs:
 * - Octal numbers are not supported.
 *
 * This function is used only in encoding for wrapping strings
 * containing number literals in quotes to make YAML parser
 * handle them as strings. It means false-positives will lead to
 * extra quotation marks and are not dangerous at all.
 *
 * @param str Literal to check.
 * @param len Length of @a str.
 *
 * @retval Whether @a str represents a number value.
 */
static inline bool
yaml_is_number(const char *str, size_t len, struct lua_State *L)
{
   /*
    * TODO: Should be implemented with the literal parser
    * instead of using strtod() and lua_isnumber().
    * Using parser will make it possible to remove the third
    * argument.
    */
   if (len == 0)
      return false;

   if (lua_isnumber(L, -1))
      return true;

   char *endptr = NULL;
   fpconv_strtod(str, &endptr);
   if (endptr == str + len)
      return true;

   return false;
}

static void generate_error_message(struct lua_yaml_loader *loader) {
   char buf[256];
   luaL_Buffer b;

   luaL_buffinit(loader->L, &b);

   luaL_addstring(&b, loader->parser.problem ? loader->parser.problem : "A problem");
   snprintf(buf, sizeof(buf), " at document: %d", loader->document_count);
   luaL_addstring(&b, buf);

   if (loader->parser.problem_mark.line || loader->parser.problem_mark.column) {
      snprintf(buf, sizeof(buf), ", line: %d, column: %d\n",
         (int) loader->parser.problem_mark.line + 1,
         (int) loader->parser.problem_mark.column + 1);
      luaL_addstring(&b, buf);
   } else {
      luaL_addstring(&b, "\n");
   }

   if (loader->parser.context) {
      snprintf(buf, sizeof(buf), "%s at line: %d, column: %d\n",
         loader->parser.context,
         (int) loader->parser.context_mark.line + 1,
         (int) loader->parser.context_mark.column + 1);
      luaL_addstring(&b, buf);
   }

   luaL_pushresult(&b);
}

static inline void delete_event(struct lua_yaml_loader *loader) {
   if (loader->validevent) {
      yaml_event_delete(&loader->event);
      loader->validevent = 0;
   }
}

static inline int do_parse(struct lua_yaml_loader *loader) {
   delete_event(loader);
   if (yaml_parser_parse(&loader->parser, &loader->event) != 1) {
      generate_error_message(loader);
      loader->error = 1;
      return 0;
   }

   loader->validevent = 1;
   return 1;
}

static int load_node(struct lua_yaml_loader *loader);

static void handle_anchor(struct lua_yaml_loader *loader) {
   const char *anchor = (char *)loader->event.data.scalar.anchor;
   if (!anchor)
      return;

   lua_pushstring(loader->L, anchor);
   lua_pushvalue(loader->L, -2);
   lua_rawset(loader->L, loader->anchortable_index);
}

static void load_map(struct lua_yaml_loader *loader) {
   lua_createtable(loader->L, 0, 5);
   if (loader->cfg->decode_save_metatables)
      luaL_setmaphint(loader->L, -1);

   handle_anchor(loader);
   while (1) {
      int r;
      /* load key */
      if (load_node(loader) == 0 || loader->error)
         return;

      /* load value */
      r = load_node(loader);
      if (loader->error)
         return;
      if (r != 1)
         RETURN_ERRMSG(loader, "unanticipated END event");
      lua_rawset(loader->L, -3);
   }
}

static void load_sequence(struct lua_yaml_loader *loader) {
   int index = 1;

   lua_createtable(loader->L, 5, 0);
   if (loader->cfg->decode_save_metatables)
      luaL_setarrayhint(loader->L, -1);

   handle_anchor(loader);
   while (load_node(loader) == 1 && !loader->error)
      lua_rawseti(loader->L, -2, index++);
}

static void load_scalar(struct lua_yaml_loader *loader) {
   const char *str = (char *)loader->event.data.scalar.value;
   unsigned int length = loader->event.data.scalar.length;
   const char *tag = (char *)loader->event.data.scalar.tag;

   if (tag && !strncmp(tag, LUAYAML_TAG_PREFIX, sizeof(LUAYAML_TAG_PREFIX) - 1)) {
      tag += sizeof(LUAYAML_TAG_PREFIX) - 1;

      if (!strcmp(tag, "str")) {
         lua_pushlstring(loader->L, str, length);
         return;
      } else if (!strcmp(tag, "int")) {
         lua_pushinteger(loader->L, strtol(str, NULL, 10));
         return;
      } else if (!strcmp(tag, "float")) {
         double dval = fpconv_strtod(str, NULL);
         luaL_checkfinite(loader->L, loader->cfg, dval);
         lua_pushnumber(loader->L, dval);
         return;
      } else if (!strcmp(tag, "bool")) {
         bool value = false;
         yaml_is_bool(str, length, &value);
         lua_pushboolean(loader->L, value);
         return;
      } else if (!strcmp(tag, "binary")) {
         int bufsize = base64_decode_bufsize(length);
         char *buf = (char *)xmalloc(bufsize);
         int size = base64_decode(str, length, buf, bufsize);
         if (yaml_decode_binary_as_string)
            lua_pushlstring(loader->L, buf, size);
         else
            luaT_pushvarbinary(loader->L, buf, size);
         free(buf);
         return;
      }
   }

   if (loader->event.data.scalar.style == YAML_PLAIN_SCALAR_STYLE) {
      bool value;
      if (!length) {
         /*
          * Non-standard: an empty value/document is null
          * according to the standard, but we decode it as an
          * empty string.
          */
         lua_pushliteral(loader->L, "");
         return;
      } else if (yaml_is_null(str, length)) {
         luaL_pushnull(loader->L);
         return;
      } else if (yaml_is_bool(str, length, &value)) {
         lua_pushboolean(loader->L, value);
         return;
      }

      /* plain scalar and Lua can convert it to a number?  make it so... */
      char *endptr = NULL;
      long long ival = strtoll(str, &endptr, 10);
      if (endptr == str + length && ival != LLONG_MAX) {
         luaL_pushint64(loader->L, ival);
         return;
      }
      unsigned long long uval = strtoull(str, &endptr, 10);
      if (endptr == str + length) {
         luaL_pushuint64(loader->L, uval);
         return;
      }
      double dval = fpconv_strtod(str, &endptr);
      if (endptr == str + length) {
         luaL_checkfinite(loader->L, loader->cfg, dval);
         lua_pushnumber(loader->L, dval);
         return;
      }
   }

   lua_pushlstring(loader->L, str, length);

   handle_anchor(loader);
}

static void load_alias(struct lua_yaml_loader *loader) {
   char *anchor = (char *)loader->event.data.alias.anchor;
   lua_pushstring(loader->L, anchor);
   lua_rawget(loader->L, loader->anchortable_index);
   if (lua_isnil(loader->L, -1)) {
      char buf[256];
      snprintf(buf, sizeof(buf), "invalid reference: %s", anchor);
      RETURN_ERRMSG(loader, buf);
   }
}

static int load_node(struct lua_yaml_loader *loader) {
   if (!do_parse(loader))
      return -1;

   switch (loader->event.type) {
      case YAML_DOCUMENT_END_EVENT:
      case YAML_MAPPING_END_EVENT:
      case YAML_SEQUENCE_END_EVENT:
         return 0;

      case YAML_MAPPING_START_EVENT:
         load_map(loader);
         return 1;

      case YAML_SEQUENCE_START_EVENT:
         load_sequence(loader);
         return 1;

      case YAML_SCALAR_EVENT:
         load_scalar(loader);
         return 1;

      case YAML_ALIAS_EVENT:
         load_alias(loader);
         return 1;

      case YAML_NO_EVENT:
         lua_pushliteral(loader->L, "libyaml returned YAML_NO_EVENT");
         loader->error = 1;
         return -1;

      default:
         lua_pushliteral(loader->L, "invalid event");
         loader->error = 1;
         return -1;
   }
}

/**
 * Decode YAML document global tag onto Lua stack.
 * @param loader Initialized loader to load tag from.
 * @retval 2 Tag handle and prefix are pushed. Both are not nil.
 * @retval 2 If an error occurred during decoding. Nil and error
 *         string are pushed.
 */
static int load_tag(struct lua_yaml_loader *loader) {
   yaml_tag_directive_t *start, *end;
   /* Initial parser step. Detect the documents start position. */
   if (do_parse(loader) == 0)
      goto parse_error;
   if (loader->event.type != YAML_STREAM_START_EVENT) {
      lua_pushnil(loader->L);
      lua_pushstring(loader->L, "expected STREAM_START_EVENT");
      return 2;
   }
   /* Parse a document start. */
   if (do_parse(loader) == 0)
      goto parse_error;
   if (loader->event.type == YAML_STREAM_END_EVENT)
      goto no_tags;
   assert(loader->event.type == YAML_DOCUMENT_START_EVENT);
   start = loader->event.data.document_start.tag_directives.start;
   end = loader->event.data.document_start.tag_directives.end;
   if (start == end)
      goto no_tags;
   if (end - start > 1) {
      lua_pushnil(loader->L);
      lua_pushstring(loader->L, "can not decode multiple tags");
      return 2;
   }
   lua_pushstring(loader->L, (const char *) start->handle);
   lua_pushstring(loader->L, (const char *) start->prefix);
   return 2;

parse_error:
   lua_pushnil(loader->L);
   /* Make nil be before an error message. */
   lua_insert(loader->L, -2);
   return 2;

no_tags:
   lua_pushnil(loader->L);
   return 1;
}

static void load(struct lua_yaml_loader *loader) {
   if (!do_parse(loader))
      return;

   if (loader->event.type != YAML_STREAM_START_EVENT)
      RETURN_ERRMSG(loader, "expected STREAM_START_EVENT");

   while (1) {
      if (!do_parse(loader))
         return;

      if (loader->event.type == YAML_STREAM_END_EVENT)
         return;

      loader->document_count++;
      if (load_node(loader) != 1)
         RETURN_ERRMSG(loader, "unexpected END event");
      if (loader->error)
         return;

      if (!do_parse(loader))
         return;
      if (loader->event.type != YAML_DOCUMENT_END_EVENT)
         RETURN_ERRMSG(loader, "expected DOCUMENT_END_EVENT");

      /* reset anchor table */
      lua_newtable(loader->L);
      lua_replace(loader->L, loader->anchortable_index);
   }
}

/**
 * Decode YAML documents onto Lua stack. First value on stack
 * is string with YAML document. Second value is options:
 * {tag_only = boolean}. Options are not required.
 * @retval N Pushed document count, if tag_only option is not
 *         specified, or equals to false.
 * @retval 2 If tag_only option is true. Tag handle and prefix
 *         are pushed.
 * @retval 2 If an error occurred during decoding. Nil and error
 *         string are pushed.
 */
static int l_load(lua_State *L) {
   struct lua_yaml_loader loader;
   if (! lua_isstring(L, 1)) {
usage_error:
      return luaL_error(L, "Usage: yaml.decode(document, "\
                        "[{tag_only = boolean}])");
   }
   size_t len;
   const char *document = lua_tolstring(L, 1, &len);
   loader.L = L;
   loader.cfg = luaL_checkserializer(L);
   loader.validevent = 0;
   loader.error = 0;
   loader.document_count = 0;
   if (!yaml_parser_initialize(&loader.parser))
      return luaL_error(L, OOM_ERRMSG);
   yaml_parser_set_input_string(&loader.parser, (yaml_char_t *) document, len);
   bool tag_only;
   if (! lua_isnoneornil(L, 2)) {
      if (! lua_istable(L, 2))
         goto usage_error;
      lua_getfield(L, 2, "tag_only");
      tag_only = lua_isboolean(L, -1) && lua_toboolean(L, -1);
   } else {
      tag_only = false;
   }

   int rc;
   if (! tag_only) {
      /* create table used to track anchors */
      lua_newtable(L);
      loader.anchortable_index = lua_gettop(L);
      load(&loader);
      if (loader.error)
         lua_error(L);
      rc = loader.document_count;
   } else {
      rc = load_tag(&loader);
   }
   delete_event(&loader);
   yaml_parser_delete(&loader.parser);
   return rc;
}

static int dump_node(struct lua_yaml_dumper *dumper);

static yaml_char_t *get_yaml_anchor(struct lua_yaml_dumper *dumper) {
   if (lua_type(dumper->L, -1) != LUA_TTABLE)
      return NULL;
   const char *s = "";
   lua_pushvalue(dumper->L, -1);
   lua_rawget(dumper->L, dumper->anchortable_index);
   if (!lua_toboolean(dumper->L, -1)) {
      lua_pop(dumper->L, 1);
      return NULL;
   }

   if (lua_isboolean(dumper->L, -1)) {
      /* this element is referenced more than once but has not been named */
      char buf[32];
      snprintf(buf, sizeof(buf), "%u", dumper->anchor_number++);
      lua_pop(dumper->L, 1);
      lua_pushvalue(dumper->L, -1);
      lua_pushstring(dumper->L, buf);
      s = lua_tostring(dumper->L, -1);
      lua_rawset(dumper->L, dumper->anchortable_index);
   } else {
      /* this is an aliased element */
      yaml_event_t ev;
      const char *str = lua_tostring(dumper->L, -1);
      if (!yaml_alias_event_initialize(&ev, (yaml_char_t *) str) ||
          !yaml_emitter_emit(&dumper->emitter, &ev))
         luaL_error(dumper->L, OOM_ERRMSG);
      lua_pop(dumper->L, 1);
   }
   return (yaml_char_t *)s;
}

static int dump_table(struct lua_yaml_dumper *dumper, struct luaL_field *field,
                      yaml_char_t *anchor){
   yaml_event_t ev;

   yaml_mapping_style_t yaml_style = (field->compact)
      ? (YAML_FLOW_MAPPING_STYLE) : YAML_BLOCK_MAPPING_STYLE;
   if (!yaml_mapping_start_event_initialize(&ev, anchor, NULL, 0, yaml_style) ||
       !yaml_emitter_emit(&dumper->emitter, &ev))
         return 0;

   lua_pushnil(dumper->L);
   while (lua_next(dumper->L, -2)) {
      lua_pushvalue(dumper->L, -2); /* push copy of key on top of stack */
      if (!dump_node(dumper) || dumper->error)
         return 0;
      lua_pop(dumper->L, 1); /* pop copy of key */
      if (!dump_node(dumper) || dumper->error)
         return 0;
      lua_pop(dumper->L, 1);
   }

   return yaml_mapping_end_event_initialize(&ev) != 0 &&
          yaml_emitter_emit(&dumper->emitter, &ev) != 0 ? 1 : 0;
}

static int dump_array(struct lua_yaml_dumper *dumper, struct luaL_field *field,
                      yaml_char_t *anchor){
   unsigned i;
   yaml_event_t ev;

   yaml_sequence_style_t yaml_style = (field->compact)
      ? (YAML_FLOW_SEQUENCE_STYLE) : YAML_BLOCK_SEQUENCE_STYLE;
   if (!yaml_sequence_start_event_initialize(&ev, anchor, NULL, 0, yaml_style) ||
       !yaml_emitter_emit(&dumper->emitter, &ev))
      return 0;

   for (i = 0; i < field->size; i++) {
      lua_rawgeti(dumper->L, -1, i + 1);
      if (!dump_node(dumper) || dumper->error)
         return 0;
      lua_pop(dumper->L, 1);
   }

   return yaml_sequence_end_event_initialize(&ev) != 0 &&
          yaml_emitter_emit(&dumper->emitter, &ev) != 0 ? 1 : 0;
}

static int yaml_is_flow_mode(struct lua_yaml_dumper *dumper) {
   /*
    * Tarantool-specific: always quote strings in FLOW SEQUENCE
    * Flow: [1, 'a', 'testing']
    * Block:
    * - 1
    * - a
    * - testing
    */

   if (dumper->emitter.flow_level > 0) {
      return 1;
   } else {
      yaml_event_t *evp;
      for (evp = dumper->emitter.events.head;
            evp != dumper->emitter.events.tail; evp++) {
         if ((evp->type == YAML_SEQUENCE_START_EVENT &&
                  evp->data.sequence_start.style == YAML_FLOW_SEQUENCE_STYLE) ||
               (evp->type == YAML_MAPPING_START_EVENT &&
                evp->data.mapping_start.style == YAML_FLOW_MAPPING_STYLE)) {
            return 1;
         }
      }
   }
   return 0;
}

static int dump_node(struct lua_yaml_dumper *dumper)
{
   size_t len = 0;
   const char *str = "";
   const char *force_literal_substring = yaml_pretty_multiline ? "\n" : "\n\n";
   yaml_char_t *tag = NULL;
   yaml_event_t ev;
   yaml_scalar_style_t style = YAML_PLAIN_SCALAR_STYLE;
   int is_binary = 0;
   char buf[DT_IVAL_TO_STRING_BUFSIZE];
   struct luaL_field field;
   bool unused;
   (void) unused;

   luaT_reftable_serialize(dumper->L, dumper->reftable_index);
   yaml_char_t *anchor = get_yaml_anchor(dumper);
   if (anchor && !*anchor)
      return 1;

   int top = lua_gettop(dumper->L);
   luaL_checkfield(dumper->L, dumper->cfg, top, &field);
   switch(field.type) {
   case MP_UINT:
      snprintf(buf, sizeof(buf) - 1, "%" PRIu64, field.ival);
      buf[sizeof(buf) - 1] = 0;
      str = buf;
      len = strlen(buf);
      break;
   case MP_INT:
      snprintf(buf, sizeof(buf) - 1, "%" PRIi64, field.ival);
      buf[sizeof(buf) - 1] = 0;
      str = buf;
      len = strlen(buf);
      break;
   case MP_FLOAT:
      fpconv_g_fmt(buf, field.fval, dumper->cfg->encode_number_precision);
      str = buf;
      len = strlen(buf);
      break;
   case MP_DOUBLE:
      fpconv_g_fmt(buf, field.dval, dumper->cfg->encode_number_precision);
      str = buf;
      len = strlen(buf);
      break;
   case MP_ARRAY:
      return dump_array(dumper, &field, anchor);
   case MP_MAP:
      return dump_table(dumper, &field, anchor);
   case MP_STR:
      str = field.sval.data;
      len = field.sval.len;

      if (yaml_is_null(str, len) || yaml_is_bool(str, len, &unused) ||
          yaml_is_number(str, len, dumper->L)) {
         /*
          * The string is convertible to a null, a boolean or
          * a number, quote it to preserve its type.
          */
         style = YAML_SINGLE_QUOTED_SCALAR_STYLE;
         break;
      }
      style = YAML_ANY_SCALAR_STYLE; // analyze_string(dumper, str, len, &is_binary);
      if (yaml_is_flow_mode(dumper)) {
         style = YAML_SINGLE_QUOTED_SCALAR_STYLE;
      } else if (strstr(str, force_literal_substring) != NULL) {
         /*
          * Tarantool-specific: use literal block style for either every
          * multiline string or string containing "\n\n" depending on compat
          * setup.
          * Useful for tutorial().
          */
         style = YAML_LITERAL_SCALAR_STYLE;
      }
      break;
   case MP_BIN:
      is_binary = 1;
      len = base64_encode_bufsize(field.sval.len, BASE64_NOWRAP);
      str = (char *)xmalloc(len);
      len = base64_encode(field.sval.data, field.sval.len, (char *)str, len,
                          BASE64_NOWRAP);
      tag = (yaml_char_t *) LUAYAML_TAG_PREFIX "binary";
      break;
   case MP_BOOL:
      if (field.bval) {
         str = "true";
         len = 4;
      } else {
         str = "false";
         len = 5;
      }
      break;
   case MP_NIL:
      style = YAML_PLAIN_SCALAR_STYLE;
      str = "null";
      len = 4;
      break;
   case MP_EXT:
      switch (field.ext_type) {
      case MP_DECIMAL:
         str = decimal_str(field.decval);
         len = strlen(str);
         break;
      case MP_UUID:
         str = tt_uuid_str(field.uuidval);
         len = UUID_STR_LEN;
         break;
      case MP_ERROR:
         str = field.errorval->errmsg;
         len = strlen(str);
         break;
      case MP_DATETIME:
         len = datetime_to_string(field.dateval, buf, sizeof(buf));
         str = buf;
         break;
      case MP_INTERVAL:
         len = interval_to_string(field.interval, buf, sizeof(buf));
         str = buf;
         break;
      default:
         assert(0); /* checked by luaL_checkfield() */
      }
      break;
    }

   int rc = 1;
   if (!yaml_scalar_event_initialize(&ev, NULL, tag, (unsigned char *)str, len,
                                     !is_binary, !is_binary, style) ||
       !yaml_emitter_emit(&dumper->emitter, &ev))
      rc = 0;

   if (is_binary)
      free((void *)str);

   return rc;
}

static void dump_document(struct lua_yaml_dumper *dumper) {
   yaml_event_t ev;

   if (!yaml_document_start_event_initialize(&ev, NULL, &dumper->begin_tag,
                                             dumper->end_tag, 0) ||
       !yaml_emitter_emit(&dumper->emitter, &ev))
      return;

   if (!dump_node(dumper) || dumper->error)
      return;

   if (!yaml_document_end_event_initialize(&ev, 0) ||
       !yaml_emitter_emit(&dumper->emitter, &ev))
      return;
}

static int append_output(void *arg, unsigned char *buf, size_t len) {
   struct lua_yaml_dumper *dumper = (struct lua_yaml_dumper *)arg;
   luaL_addlstring(&dumper->yamlbuf, (char *)buf, len);
   return 1;
}

static void find_references(struct lua_yaml_dumper *dumper) {
   int newval = -1;

   lua_pushvalue(dumper->L, -1); /* push copy of table */
   luaT_reftable_serialize(dumper->L, dumper->reftable_index);
   if (lua_type(dumper->L, -1) != LUA_TTABLE)
      goto done;

   lua_pushvalue(dumper->L, -1); /* push copy of table */
   lua_rawget(dumper->L, dumper->anchortable_index);
   if (lua_isnil(dumper->L, -1))
      newval = 0;
   else if (!lua_toboolean(dumper->L, -1))
      newval = 1;
   lua_pop(dumper->L, 1);
   if (newval != -1) {
      lua_pushvalue(dumper->L, -1);
      lua_pushboolean(dumper->L, newval);
      lua_rawset(dumper->L, dumper->anchortable_index);
   }
   if (newval)
      goto done;

   /* recursively process other table values */
   lua_pushnil(dumper->L);
   while (lua_next(dumper->L, -2) != 0) {
      find_references(dumper); /* find references on value */
      lua_pop(dumper->L, 1);
      find_references(dumper); /* find references on key */
   }

done:
   /*
    * Pop the serialized object, leave the original object on top
    * of the Lua stack.
    *
    * NB: It is important for the cycle above: it assumes that
    * table keys are not changed in the recursive call. Otherwise
    * it would feed an incorrect key to lua_next().
    */
   lua_pop(dumper->L, 1);
}

int
lua_yaml_encode(lua_State *L, struct luaL_serializer *serializer,
                const char *tag_handle, const char *tag_prefix)
{
   struct lua_yaml_dumper dumper;
   yaml_event_t ev;

   dumper.L = L;
   dumper.begin_tag.handle = (yaml_char_t *) tag_handle;
   dumper.begin_tag.prefix = (yaml_char_t *) tag_prefix;
   assert((tag_handle == NULL) == (tag_prefix == NULL));
   /*
    * If a tag is specified, then tag list is not empty and
    * consists of a single tag.
    */
   if (tag_prefix != NULL)
      dumper.end_tag = &dumper.begin_tag + 1;
   else
      dumper.end_tag = &dumper.begin_tag;
   dumper.cfg = serializer;
   dumper.error = 0;
   /* create thread to use for YAML buffer */
   dumper.outputL = luaT_newthread(L);
   if (dumper.outputL == NULL) {
      return luaL_error(L, OOM_ERRMSG);
   }
   luaL_buffinit(dumper.outputL, &dumper.yamlbuf);

   if (!yaml_emitter_initialize(&dumper.emitter))
      goto error;

   yaml_emitter_set_unicode(&dumper.emitter, 1);
   yaml_emitter_set_indent(&dumper.emitter, 2);
   yaml_emitter_set_width(&dumper.emitter, 2);
   yaml_emitter_set_break(&dumper.emitter, YAML_LN_BREAK);
   yaml_emitter_set_output(&dumper.emitter, &append_output, &dumper);

   if (!yaml_stream_start_event_initialize(&ev, YAML_UTF8_ENCODING) ||
       !yaml_emitter_emit(&dumper.emitter, &ev))
      goto error;

   lua_newtable(L);
   dumper.anchortable_index = lua_gettop(L);
   dumper.anchor_number = 0;

   luaT_reftable_new(L, dumper.cfg, 1);
   dumper.reftable_index = lua_gettop(L);

   lua_pushvalue(L, 1); /* push copy of arg we're processing */
   find_references(&dumper);
   dump_document(&dumper);
   if (dumper.error)
      goto error;
   lua_pop(L, 3); /* pop copied arg and anchor/ref tables */

   if (!yaml_stream_end_event_initialize(&ev) ||
       !yaml_emitter_emit(&dumper.emitter, &ev) ||
       !yaml_emitter_flush(&dumper.emitter))
      goto error;

   /* finalize and push YAML buffer */
   luaL_pushresult(&dumper.yamlbuf);

   if (dumper.error)
      goto error;

   yaml_emitter_delete(&dumper.emitter);
   /* move buffer to original thread */
   lua_xmove(dumper.outputL, L, 1);
   return 1;

error:
   if (dumper.emitter.error == YAML_NO_ERROR ||
       dumper.emitter.error == YAML_MEMORY_ERROR) {
      yaml_emitter_delete(&dumper.emitter);
      return luaL_error(L, OOM_ERRMSG);
   } else {
      lua_pushnil(L);
      lua_pushstring(L, dumper.emitter.problem);
      yaml_emitter_delete(&dumper.emitter);
      return 2;
   }
}

/**
 * Serialize a Lua object as YAML string, taking into account a
 * global tag if specified.
 * @param object Lua object to dump under the global tag.
 * @param options Table with two options: tag prefix and tag
 *        handle.
 * @retval 1 Pushes Lua string with dumped object.
 * @retval 2 Pushes nil and error message.
 */
static int l_dump(lua_State *L) {
   struct luaL_serializer *serializer = luaL_checkserializer(L);
   if (lua_isnone(L, 1)) {
usage_error:
      return luaL_error(L, "Usage: encode(object, {tag_prefix = <string>, "\
                        "tag_handle = <string>})");
   }
   const char *prefix = NULL, *handle = NULL;
   if (! lua_isnoneornil(L, 2)) {
      if (! lua_istable(L, 2))
         goto usage_error;
      lua_getfield(L, 2, "tag_prefix");
      if (lua_isstring(L, -1))
         prefix = lua_tostring(L, -1);
      else if (! lua_isnil(L, -1))
         goto usage_error;

      lua_getfield(L, 2, "tag_handle");
      if (lua_isstring(L, -1))
         handle = lua_tostring(L, -1);
      else if (! lua_isnil(L, -1))
         goto usage_error;

      if ((prefix == NULL) != (handle == NULL))
         goto usage_error;
   }
   return lua_yaml_encode(L, serializer, handle, prefix);
}

static int
l_new(lua_State *L)
{
   lua_yaml_new_serializer(L);
   return 1;
}

static const luaL_Reg yamllib[] = {
   { "encode", l_dump },
   { "decode", l_load },
   { "new",    l_new },
   { NULL, NULL}
};

struct luaL_serializer *
lua_yaml_new_serializer(lua_State *L)
{
   struct luaL_serializer *s = luaL_newserializer(L, NULL, yamllib);
   s->has_compact = 1;
   return s;
}

int
luaopen_yaml(lua_State *L) {
   struct luaL_serializer *s = luaL_newserializer(L, "yaml", yamllib);
   s->has_compact = 1;
   return 1;
}

/* vim: et sw=3 ts=3 sts=3:
*/
