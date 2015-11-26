/*
 * Copyright 2010-2015, Tarantool AUTHORS, please see AUTHORS file.
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
#include "box/lua/misc.h"

#include "fiber.h" /* fiber->gc() */
#include <small/region.h>
#include "lua/utils.h"
#include "lua/msgpack.h"

#include "box/box.h"
#include "box/port.h"
#include "box/lua/tuple.h"

/** {{{ Miscellaneous utils **/

char *
lbox_encode_tuple_on_gc(lua_State *L, int idx, size_t *p_len)
{
	struct region *gc = &fiber()->gc;
	size_t used = region_used(gc);
	struct mpstream stream;
	mpstream_init(&stream, gc, region_reserve_cb, region_alloc_cb,
			luamp_error, L);
	luamp_encode_tuple(L, luaL_msgpack_default, &stream, idx);
	mpstream_flush(&stream);
	*p_len = region_used(gc) - used;
	return (char *) region_join_xc(gc, *p_len);
}

/* }}} */

/** {{{ Lua/C implementation of index:select(): used only by Sophia **/

struct port_lua
{
	struct port_vtab *vtab;
	struct lua_State *L;
	size_t size; /* for port_lua_add_tuple */
};

static inline struct port_lua *
port_lua(struct port *port) { return (struct port_lua *) port; }

static void
port_lua_table_add_tuple(struct port *port, struct tuple *tuple)
{
	lua_State *L = port_lua(port)->L;
	lbox_pushtuple(L, tuple);
	lua_rawseti(L, -2, ++port_lua(port)->size);
}

/** Add all tuples to a Lua table. */
void
port_lua_table_create(struct port_lua *port, struct lua_State *L)
{
	static struct port_vtab port_lua_vtab = {
		port_lua_table_add_tuple,
		null_port_eof,
	};
	port->vtab = &port_lua_vtab;
	port->L = L;
	port->size = 0;
	/* The destination table to append tuples to. */
	lua_newtable(L);
}

static int
lbox_select(lua_State *L)
{
	if (lua_gettop(L) != 6 || !lua_isnumber(L, 1) || !lua_isnumber(L, 2) ||
		!lua_isnumber(L, 3) || !lua_isnumber(L, 4) || !lua_isnumber(L, 5)) {
		return luaL_error(L, "Usage index:select(iterator, offset, "
				  "limit, key)");
	}

	uint32_t space_id = lua_tointeger(L, 1);
	uint32_t index_id = lua_tointeger(L, 2);
	int iterator = lua_tointeger(L, 3);
	uint32_t offset = lua_tointeger(L, 4);
	uint32_t limit = lua_tointeger(L, 5);

	size_t key_len;
	const char *key = lbox_encode_tuple_on_gc(L, 6, &key_len);

	/* TODO: This code definetly leaks without EXTERNAL_UNWIND  */
	int top = lua_gettop(L);
	struct port_lua port;
	port_lua_table_create(&port, L);
	if (box_select((struct port *) &port, space_id, index_id, iterator,
			offset, limit, key, key + key_len) != 0)
		return lbox_error(L);
	return lua_gettop(L) - top;
}

/* }}} */

void
box_lua_misc_init(struct lua_State *L)
{
	static const struct luaL_reg boxlib_internal[] = {
		{"select", lbox_select},
		{NULL, NULL}
	};

	luaL_register(L, "box.internal", boxlib_internal);
	lua_pop(L, 1);
}
