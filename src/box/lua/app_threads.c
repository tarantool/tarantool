/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2026, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "box/lua/app_threads.h"

#include <lua.h>
#include <stdint.h>

#include "lua/init.h"
#include "box/lua/call.h"

void
app_thread_lua_init(void)
{
	tarantool_lua_init_minimal();
	box_lua_call_init(tarantool_L);
	tarantool_lua_postinit(tarantool_L);
}

void
app_thread_lua_free(void)
{
	lua_close(tarantool_L);
	tarantool_L = NULL;
}

int
app_thread_lua_call(const char *name, uint32_t name_len,
		    struct port *args, struct port *ret)
{
	return box_lua_call(name, name_len, args, ret);
}

int
app_thread_lua_eval(const char *expr, uint32_t expr_len,
		    struct port *args, struct port *ret)
{
	return box_lua_eval(expr, expr_len, args, ret);
}
