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

#include <string.h>
#include <stdlib.h>

#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
#include <lj_obj.h>
#include <lj_ctype.h>
#include <lj_cdata.h>
#include <lj_cconv.h>
#include <lj_state.h>

#include "yaml.h"
#include "b64.h"

/* configurable flags */
static char Dump_Auto_Array = 1;
static char Dump_Error_on_Unsupported = 0;
static char Dump_Check_Metatables = 1;
static char Load_Set_Metatables = 1;
static char Load_Numeric_Scalars = 1;
static char Load_Nulls_As_Nil = 0;

#define LUAYAML_TAG_PREFIX "tag:yaml.org,2002:"

#define LUAYAML_KIND_UNKNOWN 0
#define LUAYAML_KIND_SEQUENCE 1
#define LUAYAML_KIND_MAPPING 2

#define RETURN_ERRMSG(s, msg) do { \
      lua_pushstring(s->L, msg); \
      s->error = 1; \
      return; \
   } while(0)

struct lua_yaml_loader {
   lua_State *L;
   int anchortable_index;
   int sequencemt_index;
   int mapmt_index;
   int document_count;
   yaml_parser_t parser;
   yaml_event_t event;
   char validevent;
   char error;
};

struct lua_yaml_dumper {
   lua_State *L;
   int anchortable_index;
   unsigned int anchor_number;
   yaml_emitter_t emitter;
   char error;

   lua_State *outputL;
   luaL_Buffer yamlbuf;
};

static int l_null(lua_State *);

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
   lua_newtable(loader->L);
   if (loader->mapmt_index != 0) {
      lua_pushvalue(loader->L, loader->mapmt_index);
      lua_setmetatable(loader->L, -2);
   }

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

   lua_newtable(loader->L);
   if (loader->sequencemt_index != 0) {
      lua_pushvalue(loader->L, loader->sequencemt_index);
      lua_setmetatable(loader->L, -2);
   }

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
         lua_pushnumber(loader->L, strtod(str, NULL));
         return;
      } else if (!strcmp(tag, "bool")) {
         lua_pushboolean(loader->L, !strcmp(str, "true") || !strcmp(str, "yes"));
         return;
      } else if (!strcmp(tag, "binary")) {
         frombase64(loader->L, (const unsigned char *)str, length);
         return;
      }
   }

   if (loader->event.data.scalar.style == YAML_PLAIN_SCALAR_STYLE) {
      if (!strcmp(str, "~")) {
         if (Load_Nulls_As_Nil)
            lua_pushnil(loader->L);
         else
            l_null(loader->L);
         return;
      } else if (!strcmp(str, "true") || !strcmp(str, "yes")) {
         lua_pushboolean(loader->L, 1);
         return;
      } else if (!strcmp(str, "false") || !strcmp(str, "no")) {
         lua_pushboolean(loader->L, 0);
         return;
      }
   }

   lua_pushlstring(loader->L, str, length);

   /* plain scalar and Lua can convert it to a number?  make it so... */
   if (Load_Numeric_Scalars
      && loader->event.data.scalar.style == YAML_PLAIN_SCALAR_STYLE
      && lua_isnumber(loader->L, -1)) {
      lua_Number n = lua_tonumber(loader->L, -1);
      lua_pop(loader->L, 1);
      lua_pushnumber(loader->L, n);
   }

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

static int l_load(lua_State *L) {
   struct lua_yaml_loader loader;
   int top = lua_gettop(L);

   luaL_argcheck(L, lua_isstring(L, 1), 1, "must provide a string argument");

   loader.L = L;
   loader.validevent = 0;
   loader.error = 0;
   loader.document_count = 0;
   loader.mapmt_index = loader.sequencemt_index = 0;

   if (Load_Set_Metatables) {
      /* create sequence metatable */
      lua_newtable(L);
      lua_pushliteral(L, "_yaml");
      lua_pushliteral(L, "sequence");
      lua_rawset(L, -3);
      loader.sequencemt_index = top + 1;

      /* create map metatable */
      lua_newtable(L);
      lua_pushliteral(L, "_yaml");
      lua_pushliteral(L, "map");
      lua_rawset(L, -3);
      loader.mapmt_index = top + 2;
   }

   /* create table used to track anchors */
   lua_newtable(L);
   loader.anchortable_index = lua_gettop(L);

   yaml_parser_initialize(&loader.parser);
   yaml_parser_set_input_string(&loader.parser,
      (const unsigned char *)lua_tostring(L, 1), lua_strlen(L, 1));
   load(&loader);

   delete_event(&loader);
   yaml_parser_delete(&loader.parser);

   if (loader.error)
      lua_error(L);

   return loader.document_count;
}

static int dump_node(struct lua_yaml_dumper *dumper);

static int is_binary_string(const unsigned char *str, size_t len) {
   // this could be optimized to examine an entire word in each loop iteration
   while (len-- > 0) {
     if (*str++ & 0x80) return 1;
   }
   return 0;
}

static inline const char *
dump_tostring(struct lua_State *L, int index)
{
	if (index < 0)
		index = lua_gettop(L) + index + 1;
	lua_getglobal(L, "tostring");
	lua_pushvalue(L, index);
	lua_call(L, 1, 1);
	lua_replace(L, index);
	return lua_tostring(L, index);
}

static int dump_scalar(struct lua_yaml_dumper *dumper) {
   int type = lua_type(dumper->L, -1);
   size_t len;
   const char *str = NULL;
   yaml_char_t *tag = NULL;
   yaml_event_t ev;
   yaml_scalar_style_t style = YAML_PLAIN_SCALAR_STYLE;
   int is_binary = 0;

   if (type == LUA_TSTRING) {
      str = lua_tolstring(dumper->L, -1, &len);
      if (len <= 5 && (!strcmp(str, "true")
         || !strcmp(str, "false")
         || !strcmp(str, "~")
         || !strcmp(str, "null"))) {
         style = YAML_SINGLE_QUOTED_SCALAR_STYLE;
      } else if (lua_isnumber(dumper->L, -1)) {
         /* string is convertible to number, quote it to preserve type */
         style = YAML_SINGLE_QUOTED_SCALAR_STYLE;
      } else if ((is_binary = is_binary_string((const unsigned char *)str, len))) {
         tobase64(dumper->L, -1);
         str = lua_tolstring(dumper->L, -1, &len);
         tag = (yaml_char_t *) LUAYAML_TAG_PREFIX "binary";
      }
   } else if (type == LUA_TNUMBER) {
      /* have Lua convert number to a string */
      str = lua_tolstring(dumper->L, -1, &len);
   } else if (type == LUA_TBOOLEAN) {
      if (lua_toboolean(dumper->L, -1)) {
         str = "true";
         len = 4;
      } else {
         str = "false";
         len = 5;
      }
   } else if (type == LUA_TCDATA) {
		GCcdata *cd = cdataV(dumper->L->base + lua_gettop(dumper->L) - 1);
		str = dump_tostring(dumper->L, -1);
		len = strlen(str);
		switch (cd->ctypeid) {
		case CTID_UINT64:
			len -= 3;
			break;
		case CTID_INT64:
			len -= 2;
			break;
		}
   } else if (type == LUA_TUSERDATA) {
		str = dump_tostring(dumper->L, -1);
		len = strlen(str);
		style = YAML_VERBATIM_SCALAR_STYLE;
   } else if (type == LUA_TLIGHTUSERDATA) {
		void *ptr = luaL_checkudata(dumper->L, -1, NULL);
		if (ptr == NULL) {
			str = "null";
			len = 4;
			style = YAML_PLAIN_SCALAR_STYLE;
		} else {
			str = dump_tostring(dumper->L, -1);
			len = strlen(str);
			style = YAML_VERBATIM_SCALAR_STYLE;
		}
   } else if (type == LUA_TFUNCTION) {
		str = dump_tostring(dumper->L, -1);
		len = strlen(str);
   }
   yaml_scalar_event_initialize(&ev, NULL, tag, (unsigned char *)str, len,
      !is_binary, !is_binary, style);
   if (is_binary)
      lua_pop(dumper->L, 1);
   return yaml_emitter_emit(&dumper->emitter, &ev);
}

static yaml_char_t *get_yaml_anchor(struct lua_yaml_dumper *dumper) {
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
      yaml_alias_event_initialize(&ev, (yaml_char_t *)lua_tostring(dumper->L, -1));
      yaml_emitter_emit(&dumper->emitter, &ev);
      lua_pop(dumper->L, 1);
   }
   return (yaml_char_t *)s;
}

static int dump_table(struct lua_yaml_dumper *dumper) {
   yaml_event_t ev;
   yaml_char_t *anchor = get_yaml_anchor(dumper);

   if (anchor && !*anchor) return 1;

   yaml_mapping_start_event_initialize(&ev, anchor, NULL, 0,
      YAML_BLOCK_MAPPING_STYLE);
   yaml_emitter_emit(&dumper->emitter, &ev);

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

   yaml_mapping_end_event_initialize(&ev);
   yaml_emitter_emit(&dumper->emitter, &ev);
   return 1;
}

static int dump_array(struct lua_yaml_dumper *dumper) {
   int i, n = luaL_getn(dumper->L, -1);
   yaml_event_t ev;
   yaml_char_t *anchor = get_yaml_anchor(dumper);

   if (anchor && !*anchor)
      return 1;

   yaml_sequence_start_event_initialize(&ev, anchor, NULL, 0,
      YAML_BLOCK_SEQUENCE_STYLE);
   yaml_emitter_emit(&dumper->emitter, &ev);

   for (i = 0; i < n; i++) {
      lua_rawgeti(dumper->L, -1, i + 1);
      if (!dump_node(dumper) || dumper->error)
         return 0;
      lua_pop(dumper->L, 1);
   }

   yaml_sequence_end_event_initialize(&ev);
   yaml_emitter_emit(&dumper->emitter, &ev);

   return 1;
}

static int figure_table_type(lua_State *L) {
   int type = LUAYAML_KIND_UNKNOWN;

   if (lua_getmetatable(L, -1)) {
      /* has metatable, look for _yaml key */
      lua_pushliteral(L, "_yaml");
      lua_rawget(L, -2);
      if (lua_isstring(L, -1)) {
         const char *s = lua_tostring(L, -1);
         if (!strcmp(s, "sequence") || !strcmp(s, "seq"))
            type = LUAYAML_KIND_SEQUENCE;
         else if (!strcmp(s, "map") || !strcmp(s, "mapping"))
            type = LUAYAML_KIND_MAPPING;
      }
      lua_pop(L, 2); /* pop value and metatable */
   }

   return type;
}

static int dump_null(struct lua_yaml_dumper *dumper) {
   yaml_event_t ev;
   yaml_scalar_event_initialize(&ev, NULL, NULL,
      (unsigned char *)"null", 4, 1, 1, YAML_PLAIN_SCALAR_STYLE);
   return yaml_emitter_emit(&dumper->emitter, &ev);
}

static int dump_node(struct lua_yaml_dumper *dumper) {
   int type = lua_type(dumper->L, -1);

	if (type == LUA_TCDATA ||
	    type == LUA_TSTRING || type == LUA_TBOOLEAN ||
	    type == LUA_TNUMBER || type == LUA_TUSERDATA) {
		return dump_scalar(dumper);
   } else
	if (type == LUA_TTABLE) {
      int type = LUAYAML_KIND_UNKNOWN;

      if (Dump_Check_Metatables)
         type = figure_table_type(dumper->L);

      if (type == LUAYAML_KIND_UNKNOWN && Dump_Auto_Array &&
          luaL_getn(dumper->L, -1) > 0) {
         type = LUAYAML_KIND_SEQUENCE;
      }

      if (type == LUAYAML_KIND_SEQUENCE)
         return dump_array(dumper);
      return dump_table(dumper);
   } else
	if (type == LUA_TFUNCTION) {
	  return dump_scalar(dumper);
   } else { /* unsupported Lua type */
      if (Dump_Error_on_Unsupported) {
         char buf[256];
         snprintf(buf, sizeof(buf),
            "cannot dump object of type: %s", lua_typename(dumper->L, type));
         lua_pushstring(dumper->L, buf);
         dumper->error = 1;
      } else {
         return dump_null(dumper);
      }
   }
   return 0;
}

static void dump_document(struct lua_yaml_dumper *dumper) {
   yaml_event_t ev;

   yaml_document_start_event_initialize(&ev, NULL, NULL, NULL, 0);
   yaml_emitter_emit(&dumper->emitter, &ev);

   if (!dump_node(dumper) || dumper->error)
      return;

   yaml_document_end_event_initialize(&ev, 0);
   yaml_emitter_emit(&dumper->emitter, &ev);
}

static int append_output(void *arg, unsigned char *buf, size_t len) {
   struct lua_yaml_dumper *dumper = (struct lua_yaml_dumper *)arg;
   luaL_addlstring(&dumper->yamlbuf, (char *)buf, len);
   return 1;
}

static void find_references(struct lua_yaml_dumper *dumper) {
   int newval = -1, type = lua_type(dumper->L, -1);
   if (type != LUA_TTABLE)
      return;

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
      return;

   /* recursively process other table values */
   lua_pushnil(dumper->L);
   while (lua_next(dumper->L, -2) != 0) {
      find_references(dumper); /* find references on value */
      lua_pop(dumper->L, 1);
      find_references(dumper); /* find references on key */
   }
}

static int l_dump(lua_State *L) {
   struct lua_yaml_dumper dumper;
   int i, argcount = lua_gettop(L);
   yaml_event_t ev;

   dumper.L = L;
   dumper.error = 0;
   /* create thread to use for YAML buffer */
   dumper.outputL = lua_newthread(L);
   luaL_buffinit(dumper.outputL, &dumper.yamlbuf);

   yaml_emitter_initialize(&dumper.emitter);
   yaml_emitter_set_unicode(&dumper.emitter, 1);
   yaml_emitter_set_indent(&dumper.emitter, 2);
   yaml_emitter_set_width(&dumper.emitter, 2);
   yaml_emitter_set_break(&dumper.emitter, YAML_LN_BREAK);
   yaml_emitter_set_output(&dumper.emitter, &append_output, &dumper);

   yaml_stream_start_event_initialize(&ev, YAML_UTF8_ENCODING);
   yaml_emitter_emit(&dumper.emitter, &ev);

   for (i = 0; i < argcount; i++) {
      lua_newtable(L);
      dumper.anchortable_index = lua_gettop(L);
      dumper.anchor_number = 0;
      lua_pushvalue(L, i + 1); /* push copy of arg we're processing */
      find_references(&dumper);
      dump_document(&dumper);
      if (dumper.error)
         break;
      lua_pop(L, 2); /* pop copied arg and anchor table */
   }

   yaml_stream_end_event_initialize(&ev);
   yaml_emitter_emit(&dumper.emitter, &ev);

   yaml_emitter_flush(&dumper.emitter);
   yaml_emitter_delete(&dumper.emitter);

   /* finalize and push YAML buffer */
   luaL_pushresult(&dumper.yamlbuf);

   if (dumper.error)
      lua_error(L);

   /* move buffer to original thread */
   lua_xmove(dumper.outputL, L, 1);
   return 1;
}

static int handle_config_option(lua_State *L) {
   const char *attr;
   int i;
   static const struct {
      const char *attr;
      char *val;
   } args[] = {
      { "dump_auto_array", &Dump_Auto_Array },
      { "dump_check_metatables", &Dump_Check_Metatables },
      { "dump_error_on_unsupported", &Dump_Error_on_Unsupported },
      { "load_set_metatables", &Load_Set_Metatables },
      { "load_numeric_scalars", &Load_Numeric_Scalars },
      { "load_nulls_as_nil", &Load_Nulls_As_Nil },
      { NULL, NULL }
   };

   luaL_argcheck(L, lua_isstring(L, -2), 1, "config attribute must be string");
   luaL_argcheck(L, lua_isboolean(L, -1) || lua_isnil(L, -1), 1,
      "value must be boolean or nil");

   attr = lua_tostring(L, -2);
   for (i = 0; args[i].attr; i++) {
      if (!strcmp(attr, args[i].attr)) {
         if (!lua_isnil(L, -1))
            *(args[i].val) = lua_toboolean(L, -1);
         lua_pushboolean(L, *(args[i].val));
         return 1;
      }
   }

   luaL_error(L, "unrecognized config option: %s", attr);
   return 0; /* never reached */
}

static int l_config(lua_State *L) {
   if (lua_istable(L, -1)) {
      lua_pushnil(L);
      while (lua_next(L, -2) != 0) {
         handle_config_option(L);
         lua_pop(L, 2);
      }
      return 0;
   }

   return handle_config_option(L);
}

static int l_null(lua_State *L) {
   lua_getglobal(L, "yaml");
   lua_pushliteral(L, "null");
   lua_rawget(L, -2);
   lua_replace(L, -2);

   return 1;
}

LUALIB_API int luaopen_yaml(lua_State *L) {
   const luaL_reg yamllib[] = {
      { "decode", l_load },
      { "encode", l_dump },
      { "configure", l_config },
      { "null", l_null },
      { NULL, NULL}
   };

   luaL_openlib(L, "yaml", yamllib, 0);
   return 1;
}

LUALIB_API int yamlL_encode(lua_State *L) {
	return l_dump(L);
}
