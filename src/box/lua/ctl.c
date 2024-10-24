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
#include "box/lua/trigger.h"

#include "box/box.h"
#include "box/schema.h"
#include "box/engine.h"
#include "box/memtx_engine.h"
#include "box/raft.h"
#include "box/relay.h"
#include "box/replication.h"
#include "box/security.h"
#include "box/wal.h"

#include "core/event.h"

static int
lbox_ctl_wait_ro(struct lua_State *L)
{
	int index = lua_gettop(L);
	double timeout = TIMEOUT_INFINITY;
	if (index > 0)
		timeout = luaT_checknumber(L, 1);
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
		timeout = luaT_checknumber(L, 1);
	if (box_wait_ro(false, timeout) != 0)
		return luaT_error(L);
	return 0;
}

static int
lbox_ctl_on_shutdown(struct lua_State *L)
{
	return luaT_event_reset_trigger(L, 1, box_on_shutdown_event);
}

static int
lbox_ctl_on_schema_init(struct lua_State *L)
{
	struct event *event = event_get("box.ctl.on_schema_init", true);
	return luaT_event_reset_trigger(L, 1, event);
}

static int
lbox_ctl_on_recovery_state(struct lua_State *L)
{
	return luaT_event_reset_trigger(L, 1, box_on_recovery_state_event);
}
static int
lbox_ctl_on_election(struct lua_State *L)
{
	return luaT_event_reset_trigger(L, 1, box_raft_on_election_event);
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
lbox_ctl_make_bootstrap_leader(struct lua_State *L)
{
	if (box_make_bootstrap_leader() != 0)
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
		diag_set(IllegalParams, "function expected one argument");
		luaT_error(L);
	}

	double wait_time = luaT_checknumber(L, 1);
	if (wait_time <= 0) {
		diag_set(IllegalParams,
			 "on_shutdown timeout must be greater then zero");
		luaT_error(L);
	}

	on_shutdown_trigger_timeout = wait_time;
	return 0;
}

/**
 * Enable or disable security.iproto_lockdown option.
 */
static int
lbox_ctl_set_iproto_lockdown(struct lua_State *L)
{
#if defined(ENABLE_SECURITY)
	int index = lua_gettop(L);
	if (index != 1 || !lua_isboolean(L, 1)) {
		diag_set(IllegalParams,
			 "function expected one boolean argument");
		luaT_error(L);
	}
	bool new_val = lua_toboolean(L, 1);
	if (security_set_iproto_lockdown(new_val) != 0)
		return luaT_error(L);
#else
	diag_set(IllegalParams,
		 "box.ctl.iproto_lockdown() is available only in "
		 "Enterprise Edition builds.");
	luaT_error(L);
#endif
	return 0;
}

static int
lbox_ctl_wal_sync(struct lua_State *L)
{
	if (wal_sync(NULL))
		return luaT_error(L);
	return 0;
}

static int
lbox_ctl_deactivate_replica(struct lua_State *L)
{
	const char *uuid_str = luaT_checkstring(L, 1);
	struct tt_uuid uuid;
	if (tt_uuid_from_string(uuid_str, &uuid) != 0)
		luaT_error(L);
	struct replica *replica = replica_by_uuid(&uuid);
	if (replica == NULL)
		luaL_error(L, "Cannot deactivate replica: does not exist");
	if (tt_uuid_is_equal(&uuid, &INSTANCE_UUID))
		luaL_error(L, "Cannot deactivate replica: deactivation of "
			   "self is not allowed");
	if (relay_get_state(replica->relay) == RELAY_FOLLOW)
		luaL_error(L, "Cannot deactivate replica: it is connected");
	if (replica_clear_gc(replica) != 0)
		luaT_error(L);
	return 0;
}

static const struct luaL_Reg lbox_ctl_lib[] = {
	{"wait_ro", lbox_ctl_wait_ro},
	{"wait_rw", lbox_ctl_wait_rw},
	{"on_shutdown", lbox_ctl_on_shutdown},
	{"on_schema_init", lbox_ctl_on_schema_init},
	{"on_recovery_state", lbox_ctl_on_recovery_state},
	{"on_election", lbox_ctl_on_election},
	{"promote", lbox_ctl_promote},
	/* An old alias. */
	{"clear_synchro_queue", lbox_ctl_promote},
	{"demote", lbox_ctl_demote},
	{"make_bootstrap_leader", lbox_ctl_make_bootstrap_leader},
	{"is_recovery_finished", lbox_ctl_is_recovery_finished},
	{"set_on_shutdown_timeout", lbox_ctl_set_on_shutdown_timeout},
	{"iproto_lockdown", lbox_ctl_set_iproto_lockdown},
	{"wal_sync", lbox_ctl_wal_sync},
	{"deactivate_replica", lbox_ctl_deactivate_replica},
	{NULL, NULL}
};

void
box_lua_ctl_init(struct lua_State *L)
{
	luaL_findtable(L, LUA_GLOBALSINDEX, "box.ctl", 0);
	luaL_setfuncs(L, lbox_ctl_lib, 0);
	lua_pop(L, 1);
}
