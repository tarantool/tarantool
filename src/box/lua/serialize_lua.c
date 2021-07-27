/*
 * Copyright 2010-2020, Tarantool AUTHORS, please see AUTHORS file.
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 *
 * 1. Redistributions of source code must retain the above
 *    copyright notice, this list of conditions and the
 *    following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDER> ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * <COPYRIGHT HOLDER> OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <ctype.h>
#include <lua.h>

#include "trivia/util.h"
#include "lua/utils.h"
#include "lua/serializer.h"
#include "say.h"

#include "lib/core/decimal.h"
#include "mp_extension_types.h"
#include "uuid/tt_uuid.h"

#include "lua-yaml/b64.h"

#include "serialize_lua.h"

#if 0
# define SERIALIZER_TRACE
#endif

/* Serializer for Lua output mode */
static struct luaL_serializer *serializer_lua;

enum {
	NODE_NONE_BIT		= 0,
	NODE_ROOT_BIT		= 1,
	NODE_RAW_BIT		= 2,
	NODE_LVALUE_BIT		= 3,
	NODE_RVALUE_BIT		= 4,
	NODE_MAP_KEY_BIT	= 5,
	NODE_MAP_VALUE_BIT	= 6,
	NODE_EMBRACE_BIT	= 7,
	NODE_QUOTE_BIT		= 8,

	NODE_MAX
};

enum {
	NODE_NONE		= (1u << NODE_NONE_BIT),
	NODE_ROOT		= (1u << NODE_ROOT_BIT),
	NODE_RAW		= (1u << NODE_RAW_BIT),
	NODE_LVALUE		= (1u << NODE_LVALUE_BIT),
	NODE_RVALUE		= (1u << NODE_RVALUE_BIT),
	NODE_MAP_KEY		= (1u << NODE_MAP_KEY_BIT),
	NODE_MAP_VALUE		= (1u << NODE_MAP_VALUE_BIT),
	NODE_EMBRACE		= (1u << NODE_EMBRACE_BIT),
	NODE_QUOTE		= (1u << NODE_QUOTE_BIT),
};

struct node {
	/** Link with previous node or key */
	union {
		struct node *prev;
		struct node *key;
	};

	/** The field data we're paring */
	struct luaL_field field;

	/** Node mask NODE_x */
	int mask;

	/** Node index in a map */
	int index;
};

/**
 * Serializer context.
 */
struct lua_dumper {
	/** Lua state to fetch data from */
	lua_State *L;

	/** General configure options */
	struct luaL_serializer *cfg;

	/** Lua dumper options */
	lua_dumper_opts_t *opts;

	/** Output state */
	lua_State *outputL;
	/** Output buffer */
	luaL_Buffer luabuf;

	/** Anchors for self references */
	int anchortable_index;
	unsigned int anchor_number;

	/** Error message buffer */
	char err_msg[256];
	int err;

	/** Indentation buffer */
	char indent_buf[256];

	/** Output suffix */
	char suffix_buf[32];
	int suffix_len;

	/** Previous node mask */
	int prev_nd_mask;

	/** Ignore indents */
	bool noindent;
};

#ifdef SERIALIZER_TRACE

#define __gen_mp_name(__v) [__v] = # __v
static const char *mp_type_names[] = {
	__gen_mp_name(MP_NIL),
	__gen_mp_name(MP_UINT),
	__gen_mp_name(MP_INT),
	__gen_mp_name(MP_STR),
	__gen_mp_name(MP_BIN),
	__gen_mp_name(MP_ARRAY),
	__gen_mp_name(MP_MAP),
	__gen_mp_name(MP_BOOL),
	__gen_mp_name(MP_FLOAT),
	__gen_mp_name(MP_DOUBLE),
	__gen_mp_name(MP_EXT),
};

static const char *mp_ext_type_names[] = {
	__gen_mp_name(MP_DECIMAL),
	__gen_mp_name(MP_UUID),
	__gen_mp_name(MP_ERROR),
};
#undef __gen_mp_name

#define __gen_nd_name(__v) [__v ##_BIT] = # __v
static const char *nd_type_names[] = {
	__gen_nd_name(NODE_NONE),
	__gen_nd_name(NODE_ROOT),
	__gen_nd_name(NODE_RAW),
	__gen_nd_name(NODE_LVALUE),
	__gen_nd_name(NODE_RVALUE),
	__gen_nd_name(NODE_MAP_KEY),
	__gen_nd_name(NODE_MAP_VALUE),
	__gen_nd_name(NODE_EMBRACE),
	__gen_nd_name(NODE_QUOTE),
};
#undef __gen_nd_name

static char *
trace_nd_mask_str(unsigned int nd_mask)
{
	static char mask_str[256];
	int left = sizeof(mask_str);
	int pos = 0;

	for (int i = 0; i < NODE_MAX; i++) {
		if (!(nd_mask & (1u << i)))
			continue;

		int nd_len = strlen(nd_type_names[i]);
		if (left >= nd_len + 1) {
			strcpy(&mask_str[pos], nd_type_names[i]);
			pos += nd_len;
			mask_str[pos++] = '|';
			left = sizeof(mask_str) - pos;
		}
	}

	if (pos != 0)
		mask_str[--pos] = '\0';
	else
		strcpy(mask_str, "UNKNOWN");

	return mask_str;
}

static void
trace_node(struct lua_dumper *d)
{
	int ltype = lua_type(d->L, -1);
	say_info("serializer-trace: node    : lua type %d -> %s",
		 ltype, lua_typename(d->L, ltype));

	if (d->err != 0)
		return;

	char mp_type[64], *type_str = mp_type;
	int top = lua_gettop(d->L);
	struct luaL_field field;

	memset(&field, 0, sizeof(field));
	luaL_checkfield(d->L, d->cfg, lua_gettop(d->L), &field);

	if (field.type < lengthof(mp_type_names)) {
		if (field.type == MP_EXT) {
			size_t max_ext = lengthof(mp_ext_type_names);
			snprintf(mp_type, sizeof(mp_type), "%s/%s",
				 mp_type_names[field.type],
				 field.ext_type < max_ext ?
				 mp_ext_type_names[field.ext_type] :
				 "UNKNOWN");
		} else {
			type_str = (char *)mp_type_names[field.type];
		}
	} else {
		type_str = "UNKNOWN";
	}

	memset(&field, 0, sizeof(field));

	luaL_checkfield(d->L, d->cfg, top, &field);
	say_info("serializer-trace: node    :\tfield type %s (%d)",
		 type_str, field.type);
}

static char *
trace_string(const char *src, size_t len)
{
	static char buf[128];
	size_t pos = 0;

	if (len > sizeof(buf)-1)
		len = sizeof(buf)-1;

	while (pos < len) {
		if (src[pos] == '\n') {
			buf[pos++] = '\\';
			buf[pos++] = 'n';
			continue;
		}
		buf[pos] = src[pos];
		pos++;
	}
	buf[pos] = '\0';
	return buf;
}

static void
trace_emit(struct lua_dumper *d, int nd_mask, int indent,
	   const char *str, size_t len)
{
	if (d->suffix_len) {
		say_info("serializer-trace: emit-sfx: \"%s\"",
			 trace_string(d->suffix_buf,
				      d->suffix_len));
	}

	static_assert(NODE_MAX < sizeof(int) * 8,
		      "NODE_MAX is too big");

	char *names = trace_nd_mask_str(nd_mask);

	say_info("serializer-trace: emit    : type %s (0x%x) "
		 "indent %d val \"%s\" len %zu",
		 names, nd_mask, indent,
		 trace_string(str, len), len);
}

static void
trace_anchor(const char *s, bool alias)
{
	say_info("serializer-trace: anchor  : alias %d name %s",
		 alias, s);
}

#else /* SERIALIZER_TRACE */

static void
trace_node(struct lua_dumper *d)
{
	(void)d;
}

static void
trace_emit(struct lua_dumper *d, int nd_mask, int indent,
	   const char *str, size_t len)
{
	(void)d;
	(void)nd_mask;
	(void)indent;
	(void)str;
	(void)len;
}

static void
trace_anchor(const char *s, bool alias)
{
	(void)s;
	(void)alias;
}

#endif /* SERIALIZER_TRACE */

static const char *lua_keywords[] = {
	"and", "break", "do", "else",
	"elseif", "end", "false", "for",
	"function", "if", "in", "local",
	"nil", "not", "or", "repeat",
	"return", "then", "true", "until",
	"while", "and",
};

static int
dump_node(struct lua_dumper *d, struct node *nd, int indent);

static int
emit_node(struct lua_dumper *d, struct node *nd, int indent,
	  const char *str, size_t len);

/**
 * Generate anchor numbers for self references.
 */
static const char *
get_lua_anchor(struct lua_dumper *d)
{
	const char *s = "";

	lua_pushvalue(d->L, -1);
	lua_rawget(d->L, d->anchortable_index);
	if (!lua_toboolean(d->L, -1)) {
		lua_pop(d->L, 1);
		return NULL;
	}

	if (lua_isboolean(d->L, -1)) {
		/*
		 * This element is referenced more
		 * than once but has not been named.
		 */
		char buf[32];
		snprintf(buf, sizeof(buf), "%u", d->anchor_number++);
		lua_pop(d->L, 1);
		lua_pushvalue(d->L, -1);
		lua_pushstring(d->L, buf);
		s = lua_tostring(d->L, -1);
		lua_rawset(d->L, d->anchortable_index);
		trace_anchor(s, false);
	} else {
		/*
		 * An aliased element.
		 *
		 * FIXME: Need an example to use.
		 *
		 * const char *str = lua_tostring(d->L, -1);
		 */
		const char *str = lua_tostring(d->L, -1);
		trace_anchor(str, true);
		lua_pop(d->L, 1);
	}
	return s;
}

static void
suffix_append(struct lua_dumper *d, const char *str, int len)
{
	int left = (int)sizeof(d->suffix_buf) - d->suffix_len;
	if (len < left) {
		memcpy(&d->suffix_buf[d->suffix_len], str, len);
		d->suffix_len += len;
		d->suffix_buf[d->suffix_len] = '\0';
	}
}

static inline void
suffix_reset(struct lua_dumper *d)
{
	d->suffix_len = 0;
}

static void
suffix_flush(struct lua_dumper *d)
{
	if (d->suffix_len) {
		luaL_addlstring(&d->luabuf, d->suffix_buf, d->suffix_len);
		suffix_reset(d);
	}
}

static int
gen_indent(struct lua_dumper *d, int indent)
{
	static_assert(sizeof(d->indent_buf) > 0,
		      "indent buffer is too small");

	if (indent > 0 && d->opts->block_mode && !d->noindent) {
		snprintf(d->indent_buf, sizeof(d->indent_buf),
			 "%*s", indent, "");
		size_t len = strlen(d->indent_buf);
		d->indent_buf[len] = '\0';
		return len;
	}

	return 0;
}

static void
emit_hex_char(struct lua_dumper *d, unsigned char c)
{
	luaL_addchar(&d->luabuf, '\\');
	luaL_addchar(&d->luabuf, 'x');

#define __emit_hex(v)						\
	do {							\
		if (v <= 9)				\
			luaL_addchar(&d->luabuf, '0' + v);	\
		else						\
			luaL_addchar(&d->luabuf, v - 10 + 'a');	\
	} while (0)

	__emit_hex((c >> 4));
	__emit_hex((c & 0xf));
#undef __emit_hex
}

/**
 * Emit the string with escapes if needed.
 *
 * FIXME: Probably should to revisit and make
 * sure we've not miss anything here (octal numbers
 * are missed for now and etc...).
 */
static void
emit_string(struct lua_dumper *d, const char *str, size_t len)
{
	for (size_t i = 0; i < len; i++) {
		if ((str[i]) == '\'' || str[i] == '\"') {
			luaL_addchar(&d->luabuf, '\\');
			luaL_addchar(&d->luabuf, str[i]);
		} else if (str[i] == '\0') {
			luaL_addchar(&d->luabuf, '\\');
			luaL_addchar(&d->luabuf, '0');
		} else if (str[i] == '\a') {
			luaL_addchar(&d->luabuf, '\\');
			luaL_addchar(&d->luabuf, 'a');
		} else if (str[i] == '\b') {
			luaL_addchar(&d->luabuf, '\\');
			luaL_addchar(&d->luabuf, 'b');
		} else if (str[i] == '\f') {
			luaL_addchar(&d->luabuf, '\\');
			luaL_addchar(&d->luabuf, 'f');
		} else if (str[i] == '\v') {
			luaL_addchar(&d->luabuf, '\\');
			luaL_addchar(&d->luabuf, 'v');
		} else if (str[i] == '\r') {
			luaL_addchar(&d->luabuf, '\\');
			luaL_addchar(&d->luabuf, 'r');
		} else if (str[i] == '\n') {
			luaL_addchar(&d->luabuf, '\\');
			luaL_addchar(&d->luabuf, 'n');
		} else if (str[i] == '\t') {
			luaL_addchar(&d->luabuf, '\\');
			luaL_addchar(&d->luabuf, 't');
		} else if (str[i] == '\xef') {
			if (i < len-1 && i < len-2 &&
			    str[i+1] == '\xbb' &&
			    str[i+2] == '\xbf') {
				emit_hex_char(d, 0xef);
				emit_hex_char(d, 0xbb);
				emit_hex_char(d, 0xbf);
			} else {
				emit_hex_char(d, str[i]);
			}
		} else if (isprint(str[i]) == 0) {
			emit_hex_char(d, str[i]);
		} else {
			luaL_addchar(&d->luabuf, str[i]);
		}
	}
}

/**
 * Emit value into output buffer.
 */
static void
emit_value(struct lua_dumper *d, struct node *nd,
	   int indent, const char *str, size_t len)
{
	trace_emit(d, nd->mask, indent, str, len);

	/*
	 * There might be previous closing symbols
	 * in the suffix queue. Since we're about
	 * to emit new values don't forget to prepend
	 * ending ones.
	 */
	suffix_flush(d);

	luaL_addlstring(&d->luabuf, d->indent_buf,
			gen_indent(d, indent));

	if (nd->mask & NODE_EMBRACE)
		luaL_addlstring(&d->luabuf, "[", 1);
	if (nd->mask & NODE_QUOTE)
		luaL_addlstring(&d->luabuf, "\"", 1);

	if (nd->field.type == MP_STR) {
		emit_string(d, str, len);
	} else {
		luaL_addlstring(&d->luabuf, str, len);
	}

	if (nd->mask & NODE_QUOTE)
		luaL_addlstring(&d->luabuf, "\"", 1);
	if (nd->mask & NODE_EMBRACE)
		luaL_addlstring(&d->luabuf, "]", 1);
}

/**
 * Emit a raw string into output.
 */
static void
emit_raw_value(struct lua_dumper *d, int indent,
	       const char *str, size_t len)
{
	struct node node = {
		.mask = NODE_RAW,
	};

	emit_value(d, &node, indent, str, len);
}

/**
 * Put an opening brace into the output.
 */
static int
emit_brace_open(struct lua_dumper *d, int indent)
{
	if (d->opts->block_mode) {
		int _indent;
		if (d->noindent)
			_indent = 0;
		else
			_indent = indent;

		emit_raw_value(d, _indent, "{\n", 2);
		if (d->noindent && d->prev_nd_mask & NODE_LVALUE)
			d->noindent = false;
	} else {
		emit_raw_value(d, indent, "{", 1);
	}

	return indent + d->opts->indent_lvl;
}

/**
 * Put a closing brace into the output.
 */
static void
emit_brace_close(struct lua_dumper *d, int indent)
{
	suffix_reset(d);

	if (d->opts->block_mode)
		emit_raw_value(d, 0, "\n", 1);

	indent -= d->opts->indent_lvl;
	emit_raw_value(d, indent, "}", 1);

	if (d->opts->block_mode)
		suffix_append(d, ",\n", 2);
	else
		suffix_append(d, ", ", 2);
}

/**
 * Handling self references. It is yaml specific
 * and I don't think we might even need it. Still
 * better to get noticed if something went in
 * an unexpected way.
 */
static bool
emit_anchor(struct lua_dumper *d, struct node *nd, int indent)
{
	const char *anchor = get_lua_anchor(d);
	if (anchor && !*anchor) {
		emit_node(d, nd, indent, "nil", 3);
		return true;
	}
	return false;
}

/**
 * Dump an array entry.
 */
static void
dump_array(struct lua_dumper *d, struct node *nd, int indent)
{
	indent = emit_brace_open(d, indent);
	if (emit_anchor(d, nd, indent))
		goto out;

	for (int i = 0; i < (int)nd->field.size; i++) {
		lua_rawgeti(d->L, -1, i + 1);
		struct node node = {
			.prev = nd,
			.mask = NODE_RVALUE,
		};
		dump_node(d, &node, indent);
		lua_pop(d->L, 1);
	}
out:
	emit_brace_close(d, indent);
}

/**
 * Dump a map entry.
 */
static void
dump_table(struct lua_dumper *d, struct node *nd, int indent)
{
	int index = 0;

	indent = emit_brace_open(d, indent);
	if (emit_anchor(d, nd, indent))
		goto out;

	/*
	 * In sake of speed we don't sort
	 * keys but provide them as is. Thus
	 * simply walk over keys and their
	 * values.
	 */
	lua_pushnil(d->L);
	while (lua_next(d->L, -2)) {
		lua_pushvalue(d->L, -2);
		struct node node_key = {
			.prev	= nd,
			.mask	= NODE_LVALUE | NODE_MAP_KEY,
			.index	= index++,
		};
		dump_node(d, &node_key, indent);
		lua_pop(d->L, 1);

		struct node node_val = {
			.key	= &node_key,
			.mask	= NODE_RVALUE | NODE_MAP_VALUE,
		};
		dump_node(d, &node_val, indent);
		lua_pop(d->L, 1);
	}
out:
	emit_brace_close(d, indent);
}

/**
 * Figure out if we need to decorate a map key
 * with square braces and quotes or can leave
 * it as a plain value.
 */
static void
decorate_key(struct node *nd, const char *str, size_t len)
{
	assert(nd->field.type == MP_STR);
	assert(nd->mask & NODE_MAP_KEY);

	/*
	 * We might need to put string keys
	 * to quotes and embrace them due to
	 * limitation of how to declare map keys
	 * (the output from serializer should be
	 * parsable if pasted back to a console).
	 */
	for (size_t i = 0; i < lengthof(lua_keywords); i++) {
		const char *k = lua_keywords[i];
		if (strcmp(k, str) == 0) {
			nd->mask |= NODE_EMBRACE | NODE_QUOTE;
			return;
		}
	}

	/*
	 * Plain keys may be alphanumerics with underscopes.
	 */
	for (size_t i = 0; i < len; i++) {
		if (isalnum(str[i]) != 0 || str[i] == '_')
			continue;
		nd->mask |= NODE_EMBRACE | NODE_QUOTE;
		return;
	}

	nd->mask &= ~NODE_QUOTE;
}

static int
emit_node(struct lua_dumper *d, struct node *nd, int indent,
	  const char *str, size_t len)
{
	struct luaL_field *field = &nd->field;

	if (str == NULL) {
		d->prev_nd_mask = nd->mask;
		return 0;
	}

	if (nd->mask & NODE_MAP_KEY) {
		/*
		 * In case if key is integer and matching
		 * the current position in the table we
		 * can simply skip it and print value only.
		 */
		if (nd->field.type == MP_INT ||
		    nd->field.type == MP_UINT) {
			if (nd->index == (int)field->ival) {
				d->noindent = false;
				return 0;
			} else {
				nd->mask |= NODE_EMBRACE;
			}
		} else if (nd->field.type == MP_STR) {
			decorate_key(nd, str, len);
		}
	}

	d->prev_nd_mask = nd->mask;
	emit_value(d, nd, indent, str, len);

	/*
	 * In sake of speed we do not lookahead
	 * for next lua nodes, instead just remember
	 * closing symbol in suffix buffer which we
	 * will flush on next emit.
	 */
	if (nd->mask & NODE_RVALUE) {
		if (d->opts->block_mode)
			suffix_append(d, ",\n", 2);
		else
			suffix_append(d, ", ", 2);
		d->noindent = false;
	} else if (nd->mask & NODE_LVALUE) {
		suffix_append(d, " = ", 3);
		d->noindent = true;
	}

	return 0;
}

/**
 * Dump a node.
 */
static int
dump_node(struct lua_dumper *d, struct node *nd, int indent)
{
	struct luaL_field *field = &nd->field;
	char buf[FPCONV_G_FMT_BUFSIZE];
	int ltype = lua_type(d->L, -1);
	const char *str = NULL;
	size_t len = 0;

	trace_node(d);

	/*
	 * We can exit early if an error
	 * already happened, no need to
	 * continue parsing.
	 */
	if (d->err != 0)
		return -1;

	memset(field, 0, sizeof(*field));
	luaL_checkfield(d->L, d->cfg, lua_gettop(d->L), field);

	switch (field->type) {
	case MP_NIL:
		if (ltype == LUA_TNIL) {
			static const char str_nil[] = "nil";
			str = str_nil;
			len = strlen(str_nil);
		} else {
			static const char str_null[] = "box.NULL";
			str = str_null;
			len = strlen(str_null);
		}
		break;
	case MP_UINT:
		snprintf(buf, sizeof(buf), "%" PRIu64, field->ival);
		len = strlen(buf);
		str = buf;
		break;
	case MP_INT:
		snprintf(buf, sizeof(buf), "%" PRIi64, field->ival);
		len = strlen(buf);
		str = buf;
		break;
	case MP_STR:
		nd->mask |= NODE_QUOTE;
		str = lua_tolstring(d->L, -1, &len);
		if (utf8_check_printable(str, len) == 1)
			break;
		/* fallthrough */
	case MP_BIN:
		nd->mask |= NODE_QUOTE;
		tobase64(d->L, -1);
		str = lua_tolstring(d->L, -1, &len);
		lua_pop(d->L, 1);
		break;
	case MP_ARRAY:
		dump_array(d, nd, indent);
		break;
	case MP_MAP:
		dump_table(d, nd, indent);
		break;
	case MP_BOOL:
		if (field->bval) {
			static const char str_true[] = "true";
			len = strlen(str_true);
			str = str_true;
		} else {
			static const char str_false[] = "false";
			len = strlen(str_false);
			str = str_false;
		}
		break;
	case MP_FLOAT:
		fpconv_g_fmt(buf, field->fval,
			     d->cfg->encode_number_precision);
		len = strlen(buf);
		str = buf;
		break;
	case MP_DOUBLE:
		fpconv_g_fmt(buf, field->dval,
			     d->cfg->encode_number_precision);
		len = strlen(buf);
		str = buf;
		break;
	case MP_EXT:
		switch (field->ext_type) {
		case MP_DECIMAL:
			nd->mask |= NODE_QUOTE;
			str = decimal_str(field->decval);
			len = strlen(str);
			break;
		case MP_UUID:
			nd->mask |= NODE_QUOTE;
			str = tt_uuid_str(field->uuidval);
			len = UUID_STR_LEN;
			break;
		default:
			d->err = EINVAL;
			snprintf(d->err_msg, sizeof(d->err_msg),
				 "serializer: Unknown field MP_EXT:%d type",
				 field->ext_type);
			len = strlen(d->err_msg);
			return -1;
		}
		break;
	default:
		d->err = EINVAL;
		snprintf(d->err_msg, sizeof(d->err_msg),
			 "serializer: Unknown field %d type",
			 field->type);
		len = strlen(d->err_msg);
		return -1;
	}

	return emit_node(d, nd, indent, str, len);
}

/**
 * Find references to tables, we use it
 * to find self references in tables.
 */
static void
find_references(struct lua_dumper *d)
{
	int newval;

	if (lua_type(d->L, -1) != LUA_TTABLE)
		return;

	/* Copy of a table for self refs */
	lua_pushvalue(d->L, -1);
	lua_rawget(d->L, d->anchortable_index);
	if (lua_isnil(d->L, -1))
		newval = 0;
	else if (!lua_toboolean(d->L, -1))
		newval = 1;
	else
		newval = -1;
	lua_pop(d->L, 1);

	if (newval != -1) {
		lua_pushvalue(d->L, -1);
		lua_pushboolean(d->L, newval);
		lua_rawset(d->L, d->anchortable_index);
	}

	if (newval != 0)
		return;

	/*
	 * Other values and keys in the table
	 */
	lua_pushnil(d->L);
	while (lua_next(d->L, -2) != 0) {
		find_references(d);
		lua_pop(d->L, 1);
		find_references(d);
	}
}

/**
 * Dump recursively from the root node.
 */
static int
dump_root(struct lua_dumper *d)
{
	struct node nd = {
		.mask = NODE_ROOT,
	};
	int ret;

	luaL_checkfield(d->L, d->cfg, lua_gettop(d->L), &nd.field);

	if (nd.field.type != MP_ARRAY || nd.field.size != 1) {
		d->err = EINVAL;
		snprintf(d->err_msg, sizeof(d->err_msg),
			 "serializer: unexpected data "
			 "(nd.field.size %d nd.field.type %d)",
			 nd.field.size, nd.field.type);
		return -1;
	}

	/*
	 * We don't need to show the newly generated
	 * table, instead dump the nested one which
	 * is the real value.
	 */
	lua_rawgeti(d->L, -1, 1);
	ret = dump_node(d, &nd, 0);
	lua_pop(d->L, 1);

	return (ret || d->err) ? -1 : 0;
}

/**
 * Encode data to Lua compatible form.
 */
int
lua_encode(lua_State *L, struct luaL_serializer *serializer,
	   lua_dumper_opts_t *opts)
{
	struct lua_dumper dumper = {
		.L	= L,
		.cfg	= serializer,
		.outputL= luaT_newthread(L),
		.opts	= opts,
	};

	if (!dumper.outputL)
		return luaL_error(L, "serializer: No free memory");

	luaL_buffinit(dumper.outputL, &dumper.luabuf);

	lua_newtable(L);

	dumper.anchortable_index = lua_gettop(L);
	dumper.anchor_number = 0;

	/* Push copy of arg we're processing */
	lua_pushvalue(L, 1);
	find_references(&dumper);

	if (dump_root(&dumper) != 0)
		goto out;

	/* Pop copied arg and anchor table */
	lua_pop(L, 2);

	luaL_pushresult(&dumper.luabuf);

	/* Move buffer to original thread */
	lua_xmove(dumper.outputL, L, 1);
	return 1;

out:
	errno = dumper.err;
	lua_pushnil(L);
	lua_pushstring(L, dumper.err_msg);
	return 2;
}

/**
 * Parse serializer options.
 */
void
lua_parse_opts(lua_State *L, lua_dumper_opts_t *opts)
{
	if (lua_gettop(L) < 2 || lua_type(L, -2) != LUA_TTABLE)
		luaL_error(L, "serializer: Wrong options format");

	memset(opts, 0, sizeof(*opts));

	lua_getfield(L, -2, "block");
	if (lua_isboolean(L, -1))
		opts->block_mode = lua_toboolean(L, -1);
	lua_pop(L, 1);

	lua_getfield(L, -2, "indent");
	if (lua_isnumber(L, -1))
		opts->indent_lvl = (int)lua_tonumber(L, -1);
	lua_pop(L, 1);
}

/**
 * Initialize Lua serializer.
 */
void
lua_serializer_init(struct lua_State *L)
{
	/*
	 * We don't export it as a module
	 * for a while, so the library
	 * is kept empty.
	 */
	static const luaL_Reg lualib[] = {
		{
			.name = NULL,
		},
	};

	serializer_lua = luaL_newserializer(L, NULL, lualib);
	serializer_lua->has_compact		= 1;
	serializer_lua->encode_invalid_numbers	= 1;
	serializer_lua->encode_load_metatables	= 1;
	serializer_lua->encode_use_tostring	= 1;
	serializer_lua->encode_invalid_as_nil	= 1;

	/*
	 * Keep a reference to this module so it
	 * won't be unloaded.
	 */
	lua_setfield(L, -2, "formatter_lua");
}
