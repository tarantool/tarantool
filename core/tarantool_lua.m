/*
 * Copyright (C) 2011 Yuriy Vostrikov
 * Copyright (C) 2011 Konstantin Osipov
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
#include "tarantool.h"
/* use a full path to avoid clashes with system lua */
#include "third_party/luajit/src/lua.h"
#include "third_party/luajit/src/lauxlib.h"
#include "third_party/luajit/src/lualib.h"

#include "pickle.h"
#include "fiber.h"
#include TARANTOOL_CONFIG

struct lua_State *tarantool_L;

/** Pack our BER integer into luaL_Buffer */
static void
luaL_addvarint32(luaL_Buffer *b, u32 u32)
{
	char varint_buf[sizeof(u32)+1];
	struct tbuf tbuf = { .len = 0, .size = sizeof(varint_buf),
		.data = varint_buf };
	write_varint32(&tbuf, u32);
	luaL_addlstring(b, tbuf.data, tbuf.len);
}

/* Convert box.pack() format specifier to Tarantool
 * binary protocol UPDATE opcode
 */
static char format_to_opcode(char format)
{
	switch (format) {
	case '=': return 0;
	case '+': return 1;
	case '&': return 2;
	case '|': return 3;
	case '^': return 4;
	default: return format;
	}
}

/**
 * To use Tarantool/Box binary protocol primitives from Lua, we
 * need a way to pack Lua variables into a binary representation.
 * We do it by exporting a helper function
 *
 * box.pack(format, args...)
 *
 * which takes the format, which is very similar to Perl 'pack'
 * format, and a list of arguments, and returns a binary string
 * which has the arguments packed according to the format.
 *
 * For example, a typical SELECT packet packs in Lua like this:
 *
 * pkt = box.pack("iiiiiip", -- pack format
 *                         0, -- space id
 *                         0, -- index id
 *                         0, -- offset
 *                         2^32, -- limit
 *                         1, -- number of SELECT arguments
 *                         1, -- tuple cardinality
 *                         key); -- the key to use for SELECT
 *
 * @sa doc/box-protocol.txt, binary protocol description
 * @todo: implement box.unpack(format, str), for testing purposes
 */
static int
lbox_pack(struct lua_State *L)
{
	luaL_Buffer b;
	const char *format = luaL_checkstring(L, 1);
	int i = 2; /* first arg comes second */
	int nargs = lua_gettop(L);
	u32 u32buf;
	size_t size;
	const char *str;

	luaL_buffinit(L, &b);

	while (*format) {
		if (i > nargs)
			luaL_error(L, "box.pack: argument count does not match the format");
		switch (*format) {
		/* signed and unsigned 32-bit integers */
		case 'I':
		case 'i':
		{
			u32buf = lua_tointeger(L, i);
			luaL_addlstring(&b, (char *) &u32buf, sizeof(u32));
			break;
		}
		/* Perl 'pack' BER-encoded integer */
		case 'w':
			luaL_addvarint32(&b, lua_tointeger(L, i));
			break;
		/* A sequence of bytes */
		case 'A':
		case 'a':
			str = luaL_checklstring(L, i, &size);
			luaL_addlstring(&b, str, size);
			break;
		case 'P':
		case 'p':
			if (lua_type(L, i) == LUA_TNUMBER) {
				u32buf= (u32) lua_tointeger(L, i);
				str = (char *) &u32buf;
				size = sizeof(u32);
			} else {
				str = luaL_checklstring(L, i, &size);
			}
			luaL_addvarint32(&b, size);
			luaL_addlstring(&b, str, size);
			break;
		case '=': /* update tuple set foo=bar */
		case '+': /* set field+=val */
		case '&': /* set field&=val */
		case '|': /* set field|=val */
		case '^': /* set field^=val */
			u32buf= (u32) lua_tointeger(L, i); /* field no */
			luaL_addlstring(&b, (char *) &u32buf, sizeof(u32));
			luaL_addchar(&b, format_to_opcode(*format));
			break;
		default:
			luaL_error(L, "box.pack: unsupported pack "
				   "format specifier '%c'", *format);
		} /* end switch */
		i++;
		format++;
	}
	luaL_pushresult(&b);
	return 1;
}

static int
lbox_unpack(struct lua_State *L)
{
	const char *format = luaL_checkstring(L, 1);
	int i = 2; /* first arg comes second */
	int nargs = lua_gettop(L);
	u32 u32buf;

	while (*format) {
		if (i > nargs)
			luaL_error(L, "box.unpack: argument count does not match the format");
		switch (*format) {
		case 'i':
			u32buf = * (u32 *) lua_tostring(L, i);
			lua_pushnumber(L, u32buf);
			break;
		default:
			luaL_error(L, "box.unpack: unsupported pack "
				   "format specifier '%c'", *format);
		} /* end switch */
		i++;
		format++;
	}
	return i-2;
}
/** A descriptor for box.tbuf object methods */

static const struct luaL_reg boxlib[] = {
	{"pack", lbox_pack},
	{"unpack", lbox_unpack},
	{NULL, NULL}
};

/*
 * lua_tostring does no use __tostring metamethod, and it has
 * to be called if we want to print Lua userdata correctly.
 */
const char *
tarantool_lua_tostring(struct lua_State *L, int index)
{
	if (index < 0) /* we need an absolute index */
		index = lua_gettop(L) + index + 1;
	lua_getglobal(L, "tostring");
	lua_pushvalue(L, index);
	lua_call(L, 1, 1); /* pops both "tostring" and its argument */
	lua_replace(L, index);
	return lua_tostring(L, index);
}

/*
 * Convert Lua stack to YAML and append to
 * the given tbuf.
 */
static void
tarantool_lua_printstack_yaml(struct lua_State *L, struct tbuf *out)
{
	int top = lua_gettop(L);
	for (int i = 1; i <= top; i++)
		tbuf_printf(out, " - %s\r\n", tarantool_lua_tostring(L, i));
}

/*
 * A helper to serialize arguments of 'print' Lua built-in
 * to tbuf.
 */
static void
tarantool_lua_printstack(struct lua_State *L, struct tbuf *out)
{
	int top = lua_gettop(L);
	for (int i = 1; i <= top; i++)
		tbuf_printf(out, "%s", tarantool_lua_tostring(L, i));
}

/**
 * Redefine lua 'print' built-in to print either to the log file
 * (when Lua is used inside a module) or back to the user (for the
 * administrative console).
 *
 * When printing to the log file, we use 'say_info'.  To print to
 * the administrative console, we simply append everything to the
 * 'out' buffer, which is flushed to network at the end of every
 * administrative command.
 *
 * Note: administrative console output must be YAML-compatible.
 * If this is * done automatically, the result is ugly, so we
 * don't do it. A creator of Lua procedures has to do it herself.
 * Best we can do here is to add a trailing \r\n if it's
 * forgotten.
 */
static int
lbox_print(struct lua_State *L)
{
	lua_pushthread(L);
	lua_gettable(L, LUA_REGISTRYINDEX);
	struct tbuf *out = (struct tbuf *) lua_topointer(L, -1);
	lua_pop(L, 1); /* pop 'out' */

	if (out) { /* Administrative console */
		tarantool_lua_printstack(L, out);
		/* Courtesy: append YAML's \r\n if it's not already there */
		if (out->len < 2 || tbuf_str(out)[out->len-1] != '\n')
			tbuf_printf(out, "\r\n");
	} else { /* Add a message to the server log */
		out = tbuf_alloc(fiber->gc_pool);
		tarantool_lua_printstack(L, out);
		say_info("%s", tbuf_str(out));
	}
	return 0;
}

struct lua_State *
tarantool_lua_init()
{
	lua_State *L = luaL_newstate();
	if (L == NULL)
		return L;
	luaL_openlibs(L);
	luaL_register(L, "box", boxlib);
	lua_register(L, "print", lbox_print);
	tarantool_lua_load_cfg(L, &cfg);
	L = mod_lua_init(L);
	lua_settop(L, 0); /* clear possible left-overs of init */
	return L;
}

/* Remember the output of the administrative console in the
 * registry, to use with 'print'.
 */
static void
tarantool_lua_set_out(struct lua_State *L, struct tbuf *out)
{
	lua_pushthread(L);
	if (out)
		lua_pushlightuserdata(L, (void *) out);
	else
		lua_pushnil(L);
	lua_settable(L, LUA_REGISTRYINDEX);
}

/**
 * Attempt to append 'return ' before the chunk: if the chunk is
 * an expression, this pushes results of the expression onto the
 * stack. If the chunk is a statement, it won't compile. In that
 * case try to run the original string.
 */
static int
tarantool_lua_dostring(struct lua_State *L, const char *str)
{
	struct tbuf *buf = tbuf_alloc(fiber->gc_pool);
	tbuf_printf(buf, "%s%s", "return ", str);
	int r = luaL_loadstring(L, tbuf_str(buf));
	if (r) {
		lua_pop(L, 1); /* pop the error message */
		r = luaL_loadstring(L, str);
		if (r)
			return r;
	}
	@try {
		lua_call(L, 0, LUA_MULTRET);
	} @catch (ClientError *e) {
		lua_pushstring(L, e->errmsg);
		return 1;
	}
	return 0;
}

void
tarantool_lua(struct lua_State *L,
	      struct tbuf *out, const char *str)
{
	tarantool_lua_set_out(L, out);
	int r = tarantool_lua_dostring(L, str);
	tarantool_lua_set_out(L, NULL);
	if (r) {
		/* Make sure the output is YAMLish */
		tbuf_printf(out, "error: '%s'\r\n",
			    luaL_gsub(L, lua_tostring(L, -1),
				      "'", "''"));
	}
	else {
		tarantool_lua_printstack_yaml(L, out);
	}
	lua_settop(L, 0); /* clear the stack from return values. */
}

/**
 * Check if the given literal is a number/boolean or string
 * literal. A string literal needs quotes.
 */
static bool is_string(const char *str)
{
	if (strcmp(str, "true") == 0 || strcmp(str, "false") == 0)
	    return false;
	char *endptr;
	(void) strtod(str, &endptr);
	return *endptr != '\0';
}

/*
 * Make a new configuration available in Lua.
 * We could perhaps make Lua bindings to access the C
 * structure in question, but for now it's easier and just
 * as functional to convert the given configuration to a Lua
 * table and export the table into Lua.
 */
void tarantool_lua_load_cfg(struct lua_State *L, struct tarantool_cfg *cfg)
{
	luaL_Buffer b;
	char *key, *value;

	luaL_buffinit(L, &b);
	tarantool_cfg_iterator_t *i = tarantool_cfg_iterator_init();
	luaL_addstring(&b,
"box.cfg = {}\n"
"setmetatable(box.cfg, {})\n"
"box.space = {}\n"
"setmetatable(box.space, getmetatable(box.cfg))\n"
"getmetatable(box.space).__index = function(table, index)\n"
"  table[index] = {}\n"
"  setmetatable(table[index], getmetatable(table))\n"
"  return rawget(table, index)\n"
"end\n");
	while ((key = tarantool_cfg_iterator_next(i, cfg, &value)) != NULL) {
		if (value == NULL)
			continue;
		char *quote = is_string(value) ? "'" : "";
		if (strchr(key, '.') == NULL) {
			lua_pushfstring(L, "box.cfg.%s = %s%s%s\n",
					key, quote, value, quote);
			luaL_addstring(&b, lua_tostring(L, -1));
			lua_pop(L, 1);
		} else if (strncmp(key, "space", strlen("space")) == 0) {
			lua_pushfstring(L, "box.%s = %s%s%s\n",
					key, quote, value, quote);
			luaL_addstring(&b, lua_tostring(L, -1));
			lua_pop(L, 1);
		}
		free(value);
	}
	luaL_addstring(&b,
"getmetatable(box.cfg).__newindex = function(table, index)\n"
"  error('Attempt to modify a read-only table')\n"
"end\n"
"getmetatable(box.cfg).__index = nil\n");
	luaL_pushresult(&b);
	if (luaL_loadstring(L, lua_tostring(L, -1)) == 0)
		lua_pcall(L, 0, 0, 0);
	lua_pop(L, 1);
}
