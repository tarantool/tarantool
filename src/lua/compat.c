/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2022-2022, Tarantool AUTHORS, please see AUTHORS file.
 */

#include <lua/compat.h>

static const struct luaL_Reg internal_compat[] = {
	{NULL, NULL},
};

int
tarantool_lua_compat_init(struct lua_State *L)
{
	luaL_register(L, "internal.compat", internal_compat);
	return 1;
}
