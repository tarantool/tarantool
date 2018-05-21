/*
 * Copyright 2010-2016, Tarantool AUTHORS, please see AUTHORS file.
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
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "box/lua/info.h"

#include <ctype.h> /* tolower() */

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include "box/applier.h"
#include "box/relay.h"
#include "box/iproto.h"
#include "box/wal.h"
#include "box/replication.h"
#include "box/info.h"
#include "box/engine.h"
#include "box/vinyl.h"
#include "main.h"
#include "version.h"
#include "box/box.h"
#include "lua/utils.h"
#include "fiber.h"

static void
lbox_pushvclock(struct lua_State *L, const struct vclock *vclock)
{
	lua_createtable(L, 0, vclock_size(vclock));
	struct vclock_iterator it;
	vclock_iterator_init(&it, vclock);
	vclock_foreach(&it, replica) {
		lua_pushinteger(L, replica.id);
		luaL_pushuint64(L, replica.lsn);
		lua_settable(L, -3);
	}
	luaL_setmaphint(L, -1); /* compact flow */
}

static void
lbox_pushapplier(lua_State *L, struct applier *applier)
{
	lua_newtable(L);
	/* Get applier state in lower case */
	static char status[16];
	char *d = status;
	const char *s = applier_state_strs[applier->state] + strlen("APPLIER_");
	assert(strlen(s) < sizeof(status));
	while ((*(d++) = tolower(*(s++))));

	lua_pushstring(L, "status");
	lua_pushstring(L, status);
	lua_settable(L, -3);

	if (applier->reader) {
		lua_pushstring(L, "lag");
		lua_pushnumber(L, applier->lag);
		lua_settable(L, -3);

		lua_pushstring(L, "idle");
		lua_pushnumber(L, ev_monotonic_now(loop()) -
			       applier->last_row_time);
		lua_settable(L, -3);

		char name[FIBER_NAME_MAX];
		int total = uri_format(name, sizeof(name), &applier->uri, false);

		lua_pushstring(L, "peer");
		lua_pushlstring(L, name, total);
		lua_settable(L, -3);

		struct error *e = diag_last_error(&applier->reader->diag);
		if (e != NULL) {
			lua_pushstring(L, "message");
			lua_pushstring(L, e->errmsg);
			lua_settable(L, -3);
		}
	}
}

static void
lbox_pushrelay(lua_State *L, struct relay *relay)
{
	lua_newtable(L);
	lua_pushstring(L, "vclock");
	lbox_pushvclock(L, relay_vclock(relay));
	lua_settable(L, -3);
}

static void
lbox_pushreplica(lua_State *L, struct replica *replica)
{
	struct applier *applier = replica->applier;
	struct relay *relay = replica->relay;

	/* 16 is used to get the best visual experience in YAML output */
	lua_createtable(L, 0, 16);

	lua_pushstring(L, "id");
	lua_pushinteger(L, replica->id);
	lua_settable(L, -3);

	lua_pushstring(L, "uuid");
	lua_pushstring(L, tt_uuid_str(&replica->uuid));
	lua_settable(L, -3);

	lua_pushstring(L, "lsn");
	luaL_pushuint64(L, vclock_get(&replicaset.vclock, replica->id));
	lua_settable(L, -3);

	if (applier != NULL && applier->state != APPLIER_OFF) {
		lua_pushstring(L, "upstream");
		lbox_pushapplier(L, applier);
		lua_settable(L, -3);
	}

	if (relay_get_state(relay) == RELAY_FOLLOW) {
		lua_pushstring(L, "downstream");
		lbox_pushrelay(L, relay);
		lua_settable(L, -3);
	} else if (relay_get_state(relay) == RELAY_STOPPED) {
		lua_pushstring(L, "downstream");

		lua_newtable(L);
		lua_pushstring(L, "status");
		lua_pushstring(L, "stopped");
		lua_settable(L, -3);

		struct error *e = diag_last_error(relay_get_diag(relay));
		if (e != NULL) {
			lua_pushstring(L, "message");
			lua_pushstring(L, e->errmsg);
			lua_settable(L, -3);
		}

		lua_settable(L, -3);
	}
}

static int
lbox_info_replication(struct lua_State *L)
{
	lua_newtable(L); /* box.info.replication */

	/* Nice formatting */
	lua_newtable(L); /* metatable */
	lua_pushliteral(L, "mapping");
	lua_setfield(L, -2, "__serialize");
	lua_setmetatable(L, -2);

	replicaset_foreach(replica) {
		/* Applier hasn't received replica id yet */
		if (replica->id == REPLICA_ID_NIL)
			continue;

		lbox_pushreplica(L, replica);

		lua_rawseti(L, -2, replica->id);
	}

	return 1;
}

static int
lbox_info_id(struct lua_State *L)
{
	/*
	 * Self can be NULL during bootstrap: entire box.info
	 * bundle becomes available soon after entering box.cfg{}
	 * and replication bootstrap relies on this as it looks
	 * at box.info.status.
	 */
	struct replica *self = replica_by_uuid(&INSTANCE_UUID);
	if (self != NULL && self->id != REPLICA_ID_NIL) {
		lua_pushinteger(L, self->id);
	} else {
		luaL_pushnull(L);
	}
	return 1;
}

static int
lbox_info_uuid(struct lua_State *L)
{
	lua_pushlstring(L, tt_uuid_str(&INSTANCE_UUID), UUID_STR_LEN);
	return 1;
}

static int
lbox_info_lsn(struct lua_State *L)
{
	/* See comments in lbox_info_id */
	struct replica *self = replica_by_uuid(&INSTANCE_UUID);
	if (self != NULL && self->id != REPLICA_ID_NIL) {
		luaL_pushint64(L, vclock_get(&replicaset.vclock, self->id));
	} else {
		luaL_pushint64(L, -1);
	}
	return 1;
}

static int
lbox_info_signature(struct lua_State *L)
{
	luaL_pushint64(L, vclock_sum(&replicaset.vclock));
	return 1;
}

static int
lbox_info_ro(struct lua_State *L)
{
	lua_pushboolean(L, box_is_ro());
	return 1;
}

/*
 * Tarantool 1.6.x compat
 */
static int
lbox_info_server(struct lua_State *L)
{
	lua_createtable(L, 0, 2);
	lua_pushliteral(L, "id");
	lbox_info_id(L);
	lua_settable(L, -3);
	lua_pushliteral(L, "uuid");
	lbox_info_uuid(L);
	lua_settable(L, -3);
	lua_pushliteral(L, "lsn");
	lbox_info_lsn(L);
	lua_settable(L, -3);
	lua_pushliteral(L, "ro");
	lbox_info_ro(L);
	lua_settable(L, -3);
	return 1;
}

static int
lbox_info_vclock(struct lua_State *L)
{
	lbox_pushvclock(L, &replicaset.vclock);
	return 1;
}

static int
lbox_info_status(struct lua_State *L)
{
	lua_pushstring(L, box_status());
	return 1;
}

static int
lbox_info_uptime(struct lua_State *L)
{
	lua_pushnumber(L, (unsigned)tarantool_uptime() + 1);
	return 1;
}

static int
lbox_info_pid(struct lua_State *L)
{
	lua_pushnumber(L, getpid());
	return 1;
}

static int
lbox_info_cluster(struct lua_State *L)
{
	lua_createtable(L, 0, 2);
	lua_pushliteral(L, "uuid");
	lua_pushlstring(L, tt_uuid_str(&REPLICASET_UUID), UUID_STR_LEN);
	lua_settable(L, -3);
	return 1;
}

static int
lbox_info_memory_call(struct lua_State *L)
{
	struct engine_memory_stat stat;
	engine_memory_stat(&stat);

	lua_pushstring(L, "data");
	luaL_pushuint64(L, stat.data);
	lua_settable(L, -3);

	lua_pushstring(L, "index");
	luaL_pushuint64(L, stat.index);
	lua_settable(L, -3);

	lua_pushstring(L, "cache");
	luaL_pushuint64(L, stat.cache);
	lua_settable(L, -3);

	lua_pushstring(L, "tx");
	luaL_pushuint64(L, stat.tx);
	lua_settable(L, -3);

	lua_pushstring(L, "net");
	luaL_pushuint64(L, iproto_mem_used());
	lua_settable(L, -3);

	lua_pushstring(L, "lua");
	lua_pushinteger(L, G(L)->gc.total);
	lua_settable(L, -3);

	return 1;
}

static int
lbox_info_memory(struct lua_State *L)
{
	lua_newtable(L);

	lua_newtable(L); /* metatable */

	lua_pushstring(L, "__call");
	lua_pushcfunction(L, lbox_info_memory_call);
	lua_settable(L, -3);

	lua_setmetatable(L, -2);
	return 1;
}

static void
luaT_info_begin(struct info_handler *info)
{
	lua_State *L = (lua_State *) info->ctx;
	lua_newtable(L);
}

static void
luaT_info_end(struct info_handler *info)
{
	(void) info;
}

static void
luaT_info_begin_table(struct info_handler *info, const char *key)
{
	lua_State *L = (lua_State *) info->ctx;
	lua_pushstring(L, key);
	lua_newtable(L);
}

static void
luaT_info_end_table(struct info_handler *info)
{
	lua_State *L = (lua_State *) info->ctx;
	lua_settable(L, -3);
}

static void
luaT_info_append_double(struct info_handler *info,
			const char *key, double value)
{
	lua_State *L = (lua_State *) info->ctx;
	lua_pushstring(L, key);
	lua_pushnumber(L, value);
	lua_settable(L, -3);
}

static void
luaT_info_append_int(struct info_handler *info, const char *key,
		     int64_t value)
{
	lua_State *L = (lua_State *) info->ctx;
	lua_pushstring(L, key);
	luaL_pushint64(L, value);
	lua_settable(L, -3);
}

static void
luaT_info_append_str(struct info_handler *info, const char *key,
		     const char *value)
{
	lua_State *L = (lua_State *) info->ctx;
	lua_pushstring(L, key);
	lua_pushstring(L, value);
	lua_settable(L, -3);
}

void
luaT_info_handler_create(struct info_handler *h, struct lua_State *L)
{
	static struct info_handler_vtab lua_vtab = {
		.begin = luaT_info_begin,
		.end = luaT_info_end,
		.begin_table = luaT_info_begin_table,
		.end_table = luaT_info_end_table,
		.append_int = luaT_info_append_int,
		.append_str = luaT_info_append_str,
		.append_double = luaT_info_append_double
	};
	h->vtab = &lua_vtab;
	h->ctx = L;
}

static int
lbox_info_vinyl_call(struct lua_State *L)
{
	struct info_handler h;
	luaT_info_handler_create(&h, L);
	struct vinyl_engine *vinyl;
	vinyl = (struct vinyl_engine *)engine_by_name("vinyl");
	assert(vinyl != NULL);
	vinyl_engine_info(vinyl, &h);
	return 1;
}

static int
lbox_info_vinyl(struct lua_State *L)
{
	lua_newtable(L);

	lua_newtable(L); /* metatable */

	lua_pushstring(L, "__call");
	lua_pushcfunction(L, lbox_info_vinyl_call);
	lua_settable(L, -3);

	lua_setmetatable(L, -2);

	return 1;
}

static const struct luaL_Reg lbox_info_dynamic_meta[] = {
	{"id", lbox_info_id},
	{"uuid", lbox_info_uuid},
	{"lsn", lbox_info_lsn},
	{"signature", lbox_info_signature},
	{"vclock", lbox_info_vclock},
	{"ro", lbox_info_ro},
	{"replication", lbox_info_replication},
	{"status", lbox_info_status},
	{"uptime", lbox_info_uptime},
	{"pid", lbox_info_pid},
	{"cluster", lbox_info_cluster},
	{"memory", lbox_info_memory},
	{"vinyl", lbox_info_vinyl},
	{NULL, NULL}
};

static const struct luaL_Reg lbox_info_dynamic_meta_v16[] = {
	{"server", lbox_info_server},
	{NULL, NULL}
};

/** Evaluate box.info.* function value and push it on the stack. */
static int
lbox_info_index(struct lua_State *L)
{
	lua_pushvalue(L, -1);			/* dup key */
	lua_gettable(L, lua_upvalueindex(1));   /* table[key] */

	if (!lua_isfunction(L, -1)) {
		/* No such key. Leave nil is on the stack. */
		return 1;
	}

	lua_call(L, 0, 1);
	lua_remove(L, -2);
	return 1;
}

/** Push a bunch of compile-time or start-time constants into a Lua table. */
static void
lbox_info_init_static_values(struct lua_State *L)
{
	/* tarantool version */
	lua_pushstring(L, "version");
	lua_pushstring(L, tarantool_version());
	lua_settable(L, -3);
}

/**
 * When user invokes box.info(), return a table of key/value
 * pairs containing the current info.
 */
static int
lbox_info_call(struct lua_State *L)
{
	lua_newtable(L);
	lbox_info_init_static_values(L);
	for (int i = 0; lbox_info_dynamic_meta[i].name; i++) {
		lua_pushstring(L, lbox_info_dynamic_meta[i].name);
		lbox_info_dynamic_meta[i].func(L);
		lua_settable(L, -3);
	}

	/* Tarantool 1.6.x compat */
	lua_newtable(L);
	lua_newtable(L);
	for (int i = 0; lbox_info_dynamic_meta_v16[i].name; i++) {
		lua_pushstring(L, lbox_info_dynamic_meta_v16[i].name);
		lbox_info_dynamic_meta_v16[i].func(L);
		lua_settable(L, -3);
	}
	lua_setfield(L, -2, "__index");
	lua_setmetatable(L, -2);

	return 1;
}

/** Initialize box.info package. */
void
box_lua_info_init(struct lua_State *L)
{
	static const struct luaL_Reg infolib [] = {
		{NULL, NULL}
	};

	luaL_register_module(L, "box.info", infolib);

	lua_newtable(L);		/* metatable for info */

	lua_pushstring(L, "__index");

	lua_newtable(L); /* table for __index */
	luaL_register(L, NULL, lbox_info_dynamic_meta);
	luaL_register(L, NULL, lbox_info_dynamic_meta_v16);
	lua_pushcclosure(L, lbox_info_index, 1);
	lua_settable(L, -3);

	lua_pushstring(L, "__call");
	lua_pushcfunction(L, lbox_info_call);
	lua_settable(L, -3);

	lua_pushstring(L, "__serialize");
	lua_pushcfunction(L, lbox_info_call);
	lua_settable(L, -3);

	lua_setmetatable(L, -2);

	lbox_info_init_static_values(L);

	lua_pop(L, 1); /* info module */
}
