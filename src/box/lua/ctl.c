/*
 * Copyright 2010-2018, Tarantool AUTHORS, please see AUTHORS file.
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
#include "box/lua/ctl.h"

#include <tarantool_ev.h>

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include "lua/utils.h"
#include "lua/trigger.h"

#include "box/box.h"
#include "box/schema.h"
#include "box/engine.h"
#include "box/memtx_engine.h"

static int
lbox_ctl_wait_ro(struct lua_State *L)
{
	int index = lua_gettop(L);
	double timeout = TIMEOUT_INFINITY;
	if (index > 0)
		timeout = luaL_checknumber(L, 1);
	if (box_wait_ro(true, timeout) != 0)
		return luaT_error(L);
	return 0;
}

static int
lbox_ctl_wait_rw(struct lua_State *L)
{
	int index = lua_gettop(L);
	double timeout = TIMEOUT_INFINITY;
	if (index > 0)
		timeout = luaL_checknumber(L, 1);
	if (box_wait_ro(false, timeout) != 0)
		return luaT_error(L);
	return 0;
}

static int
lbox_ctl_on_shutdown(struct lua_State *L)
{
	return lbox_trigger_reset(L, 2, &box_on_shutdown_trigger_list,
				  NULL, NULL);
}

static int
lbox_ctl_on_schema_init(struct lua_State *L)
{
	return lbox_trigger_reset(L, 2, &on_schema_init, NULL, NULL);
}

static int
lbox_ctl_promote(struct lua_State *L)
{
	if (box_promote() != 0)
		return luaT_error(L);
	return 0;
}

static int
lbox_ctl_demote(struct lua_State *L)
{
	if (box_demote() != 0)
		return luaT_error(L);
	return 0;
}

static int
lbox_ctl_is_recovery_finished(struct lua_State *L)
{
	struct memtx_engine *memtx;
	memtx = (struct memtx_engine *)engine_by_name("memtx");
	lua_pushboolean(L, (memtx ?
		(memtx->state < MEMTX_OK ? 0 : 1) : 0));
	return 1;
}

static int
lbox_ctl_set_on_shutdown_timeout(struct lua_State *L)
{
	int index = lua_gettop(L);
	if (index != 1) {
		lua_pushstring(L, "function expected one argument");
		lua_error(L);
	}

	double wait_time = luaL_checknumber(L, 1);
	if (wait_time <= 0) {
		lua_pushstring(L, "on_shutdown timeout must be greater "
			       "then zero");
		lua_error(L);
	}

	on_shutdown_trigger_timeout = wait_time;
	return 0;
}

static const struct luaL_Reg lbox_ctl_lib[] = {
	{"wait_ro", lbox_ctl_wait_ro},
	{"wait_rw", lbox_ctl_wait_rw},
	{"on_shutdown", lbox_ctl_on_shutdown},
	{"on_schema_init", lbox_ctl_on_schema_init},
	{"promote", lbox_ctl_promote},
	/* An old alias. */
	{"clear_synchro_queue", lbox_ctl_promote},
	{"demote", lbox_ctl_demote},
	{"is_recovery_finished", lbox_ctl_is_recovery_finished},
	{"set_on_shutdown_timeout", lbox_ctl_set_on_shutdown_timeout},
	{NULL, NULL}
};

void
box_lua_ctl_init(struct lua_State *L)
{
	luaL_register_module(L, "box.ctl", lbox_ctl_lib);
	lua_pop(L, 1);
}
