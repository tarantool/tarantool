/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2024, Tarantool AUTHORS, please see AUTHORS file.
 */

#include "arrow_record_batch.h"

#include "core/arrow_record_batch.h"
#include "lua/utils.h"

static const char *arrow_record_batch_typename = "arrow_record_batch";

struct arrow_record_batch *
luaT_new_arrow_record_batch(struct lua_State *L)
{
	struct arrow_record_batch *arrow = lua_newuserdata(L, sizeof(*arrow));
	memset(arrow, 0, sizeof(*arrow));
	luaL_getmetatable(L, arrow_record_batch_typename);
	lua_setmetatable(L, -2);
	return arrow;
}

struct arrow_record_batch *
luaT_check_arrow_record_batch(struct lua_State *L, int idx)
{
	return luaL_checkudata(L, idx, arrow_record_batch_typename);
}

/**
 * Releases an arrow record batch.
 */
static int
larrow_record_batch_gc(struct lua_State *L)
{
	struct arrow_record_batch *arrow = luaT_check_arrow_record_batch(L, 1);
	if (arrow->schema.release != NULL)
		arrow->schema.release(&arrow->schema);
	if (arrow->array.release != NULL)
		arrow->array.release(&arrow->array);
	return 0;
}

/**
 * Returns the arrow record batch object type name.
 */
static int
larrow_record_batch_serialize(struct lua_State *L)
{
	luaT_check_arrow_record_batch(L, 1);
	lua_pushstring(L, arrow_record_batch_typename);
	return 1;
}

/**
 * Returns the arrow record batch object type name.
 */
static int
larrow_record_batch_tostring(struct lua_State *L)
{
	luaT_check_arrow_record_batch(L, 1);
	lua_pushstring(L, arrow_record_batch_typename);
	return 1;
}

void
tarantool_lua_arrow_record_batch_init(struct lua_State *L)
{
	const struct luaL_Reg larrow_record_batch_meta[] = {
		{"__gc", larrow_record_batch_gc},
		{"__serialize", larrow_record_batch_serialize},
		{"__tostring", larrow_record_batch_tostring},
		{NULL, NULL}
	};
	luaL_register_type(L, arrow_record_batch_typename,
			   larrow_record_batch_meta);
}
