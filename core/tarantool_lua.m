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


struct lua_State *
tarantool_lua_init()
{
	lua_State *L = luaL_newstate();
	if (L) {
		luaL_openlibs(L);
		luaL_register(L, "box", boxlib);
		L = mod_lua_init(L);
	}
	return L;
}

void
tarantool_lua(struct lua_State *L,
	      struct tbuf *out, const char *str)
{
	lua_pushstring(L, "out");
	lua_pushlightuserdata(L, (void *) out);
	lua_settable(L, LUA_REGISTRYINDEX);
	int r = luaL_dostring(L, str);
	if (r) {
		/* Make sure the output is YAMLish */
		tbuf_printf(out, "error: '%s'\r\n",
			    luaL_gsub(L, lua_tostring(L, -1),
				      "'", "''"));
		lua_pop(L, lua_gettop(L));
	}
}
