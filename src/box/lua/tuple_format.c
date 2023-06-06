/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2023, Tarantool AUTHORS, please see AUTHORS file.
 */

#include "box/lua/tuple_format.h"

#include "box/tuple.h"
#include "box/tuple_format.h"

#include "lua/msgpack.h"
#include "lua/utils.h"

#include "mpstream/mpstream.h"

static const char *tuple_format_typename = "box.tuple.format";

struct tuple_format *
luaT_check_tuple_format(struct lua_State *L, int narg)
{
	return *(struct tuple_format **)luaL_checkudata(L, narg,
							tuple_format_typename);
}

static int
lbox_tuple_format_gc(struct lua_State *L)
{
	struct tuple_format *format = luaT_check_tuple_format(L, 1);
	tuple_format_unref(format);
	return 0;
}

/*
 * Creates a cdata object for tuple format and pushes it onto Lua stack.
 */
static int
luaT_push_tuple_format(struct lua_State *L, struct tuple_format *format)
{
	struct tuple_format **ptr = lua_newuserdata(L, sizeof(*ptr));
	*ptr = format;
	tuple_format_ref(format);
	luaL_getmetatable(L, tuple_format_typename);
	lua_setmetatable(L, -2);
	return 1;
}

/*
 * Creates a new tuple format from a format clause (can be omitted). The format
 * clause is a Lua table (the same as the one passed to `format`
 * method of space objects): it is encoded into MsgPack to reuse existing
 * field definition decoding (see also `space_def_new_from_tuple`). Throws a Lua
 * exception on failure.
 *
 * In some cases (formats received over IPROTO or formats for read views) we
 * only need to get the 'name' field options and ignore the rest, hence the
 * `names_only` flag is provided.
 */
static int
lbox_tuple_format_new(struct lua_State *L)
{
	int top = lua_gettop(L);
	(void)top;
	assert((1 <= top && 2 >= top) && lua_istable(L, 1));
	struct region *region = &fiber()->gc;
	size_t region_svp = region_used(region);
	struct mpstream stream;
	mpstream_init(&stream, region, region_reserve_cb, region_alloc_cb,
		      luamp_error, L);
	if (luamp_encode(L, luaL_msgpack_default, &stream, 1) != 0) {
		region_truncate(region, region_svp);
		return luaT_error(L);
	}
	mpstream_flush(&stream);
	size_t format_data_len = region_used(region) - region_svp;
	const char *format_data = xregion_join(region, format_data_len);
	bool names_only = lua_toboolean(L, 2);
	struct tuple_format *format =
		runtime_tuple_format_new(format_data, format_data_len,
					 names_only);
	region_truncate(region, region_svp);
	if (format == NULL)
		return luaT_error(L);
	return luaT_push_tuple_format(L, format);
}

void
box_lua_tuple_format_init(struct lua_State *L)
{
	const struct luaL_Reg lbox_tuple_format_meta[] = {
		{"__gc", lbox_tuple_format_gc},
		{NULL, NULL}
	};
	luaL_register_type(L, tuple_format_typename, lbox_tuple_format_meta);

	const struct luaL_Reg box_tuple_formatlib_internal[] = {
		{"new", lbox_tuple_format_new},
		{NULL, NULL}
	};
	luaL_findtable(L, LUA_GLOBALSINDEX, "box.internal", 1);
	lua_createtable(L, 0, 1);
	luaL_setfuncs(L, box_tuple_formatlib_internal, 0);
	lua_setfield(L, -2, "tuple_format");
	lua_pop(L, 1);
}
