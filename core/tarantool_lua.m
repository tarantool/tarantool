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
 * pkt = box.pack("uuuuuup", -- pack format
 *                         0, -- namespace id
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

	luaL_buffinit(L, &b);

	while (*format) {
		switch (*format) {
		/* signed and unsigned 32-bit integers */
		case 'I':
		case 'i':
		{
			u32 u32 = luaL_checkinteger(L, i);
			luaL_addlstring(&b, (const char *)&u32, sizeof(u32));
			break;
		}
		/* Perl 'pack' BER-encoded integer */
		case 'w':
			luaL_addvarint32(&b, luaL_checkinteger(L, i));
			break;
		/* A sequence of bytes */
		case 'A':
		case 'a':
		{
			size_t size;
			const char *str = luaL_checklstring(L, i, &size);
			luaL_addlstring(&b, str, size);
			break;
		}
		case 'P':
		case 'p':
		{
			size_t size;
			const char *str = luaL_checklstring(L, i, &size);
			luaL_addvarint32(&b, size);
			luaL_addlstring(&b, str, size);
			break;
		}
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

/** A descriptor for box.tbuf object methods */

static const struct luaL_reg boxlib[] = {
	{"pack", lbox_pack},
	{NULL, NULL}
};

/*
 * lua_tostring does no use __tostring metamethod, and it has
 * to be called if we want to print Lua userdata correctly.
 */
static const char *
tarantool_lua_tostring(struct lua_State *L, int index)
{
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
	if (L) {
		luaL_openlibs(L);
		luaL_register(L, "box", boxlib);
		lua_register(L, "print", lbox_print);
		L = mod_lua_init(L);
	}
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
		return luaL_dostring(L, str);
	}
	return lua_pcall(L, 0, LUA_MULTRET, 0);
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
