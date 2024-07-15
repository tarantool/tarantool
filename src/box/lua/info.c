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
#include "lua/info.h"

#include <ctype.h> /* tolower() */
#include <unistd.h>

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include "box/applier.h"
#include "box/relay.h"
#include "box/iproto.h"
#include "box/wal.h"
#include "box/replication.h"
#include "info/info.h"
#include "box/gc.h"
#include "box/engine.h"
#include "box/vinyl.h"
#include "box/sql_stmt_cache.h"
#include "main.h"
#include "version.h"
#include "box/box.h"
#include "box/raft.h"
#include "box/txn_limbo.h"
#include "box/schema.h"
#include "box/node_name.h"
#include "lua/utils.h"
#include "lua/serializer.h" /* luaL_setmaphint */
#include "fiber.h"
#include "sio.h"
#include "tt_strerror.h"
#include "tweaks.h"

/**
 * In 3.0.0 the meaning of box.info.cluster changed to something not related. In
 * the major release it was allowed to make the new behaviour the default one,
 * but since the change can be very breaking for some people, it still can be
 * reverted.
 */
static bool box_info_cluster_new_meaning = true;
TWEAK_BOOL(box_info_cluster_new_meaning);

/**
 * Known upper limits for a hostname (without a zero-terminating
 * byte):
 *
 * sysconf(_SC_HOST_NAME_MAX) == 64 on Linux.
 * sysconf(_SC_HOST_NAME_MAX) == 255 on macOS.
 * sysconf(_SC_HOST_NAME_MAX) == 255 on BSD.
 *
 * The constant value is used to simplify the code.
 */
enum { TT_HOST_NAME_MAX = 255 };

static inline void
lbox_push_replication_error_message(struct lua_State *L, struct error *e,
				    int idx)
{
	lua_pushstring(L, "message");
	lua_pushstring(L, e->errmsg);
	lua_settable(L, idx - 2);
	if (e->saved_errno == 0)
		return;
	lua_pushstring(L, "system_message");
	lua_pushstring(L, tt_strerror(e->saved_errno));
	lua_settable(L, idx - 2);
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

	if (applier->fiber != NULL) {
		lua_pushstring(L, "lag");
		lua_pushnumber(L, applier->lag);
		lua_settable(L, -3);

		lua_pushstring(L, "idle");
		lua_pushnumber(L, ev_monotonic_now(loop()) -
			       applier->last_row_time);
		lua_settable(L, -3);

		char name[APPLIER_SOURCE_MAXLEN];
		int total = uri_format(name, sizeof(name), &applier->uri, false);
		/*
		 * total can be greater than sizeof(name) if
		 * name has insufficient length. Terminating
		 * zero is ignored by lua_pushlstring.
		 */
		total = MIN(total, (int)sizeof(name) - 1);
		lua_pushstring(L, "peer");
		lua_pushlstring(L, name, total);
		lua_settable(L, -3);

		struct error *e = diag_last_error(&applier->diag);
		if (e != NULL)
			lbox_push_replication_error_message(L, e, -1);
	}
}

static void
lbox_pushrelay(lua_State *L, struct relay *relay)
{
	lua_newtable(L);
	lua_pushstring(L, "status");

	switch(relay_get_state(relay)) {
	case RELAY_FOLLOW:
		lua_pushstring(L, "follow");
		lua_settable(L, -3);
		lua_pushstring(L, "vclock");
		luaT_pushvclock(L, relay_vclock(relay));
		lua_settable(L, -3);
		lua_pushstring(L, "idle");
		lua_pushnumber(L, ev_monotonic_now(loop()) -
			       relay_last_row_time(relay));
		lua_settable(L, -3);
		lua_pushstring(L, "lag");
		lua_pushnumber(L, relay_txn_lag(relay));
		lua_settable(L, -3);
		break;
	case RELAY_STOPPED:
	{
		lua_pushstring(L, "stopped");
		lua_settable(L, -3);

		struct error *e = diag_last_error(relay_get_diag(relay));
		if (e != NULL)
			lbox_push_replication_error_message(L, e, -1);
		break;
	}
	default: unreachable();
	}
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
	luaT_pushuuidstr(L, &replica->uuid);
	lua_settable(L, -3);

	if (*replica->name == 0)
		luaL_pushnull(L);
	else
		lua_pushstring(L, replica->name);
	lua_setfield(L, -2, "name");

	lua_pushstring(L, "lsn");
	luaL_pushuint64(L, vclock_get(instance_vclock, replica->id));
	lua_settable(L, -3);

	if (applier != NULL && applier->state != APPLIER_OFF) {
		lua_pushstring(L, "upstream");
		lbox_pushapplier(L, applier);
		lua_settable(L, -3);
	}

	if (relay_get_state(relay) != RELAY_OFF) {
		lua_pushstring(L, "downstream");
		lbox_pushrelay(L, relay);
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
lbox_info_replication_anon_call(struct lua_State *L)
{
	lua_newtable(L);

	/* Metatable. */
	lua_newtable(L);
	lua_pushliteral(L, "mapping");
	lua_setfield(L, -2, "__serialize");
	lua_setmetatable(L, -2);

	replicaset_foreach(replica) {
		if (!replica->anon)
			continue;

		luaT_pushuuidstr(L, &replica->uuid);
		lbox_pushreplica(L, replica);

		lua_settable(L, -3);
	}

	return 1;
}

static int
lbox_info_replication_anon(struct lua_State *L)
{
	/*
	 * Make the .replication_anon field callable in order to
	 * not flood the output with possibly lots of anonymous
	 * replicas on box.info call.
	 */
	lua_newtable(L);

	lua_pushliteral(L, "count");
	lua_pushinteger(L, replicaset.anon_count);
	lua_settable(L, -3);

	/* Metatable. */
	lua_newtable(L);

	lua_pushstring(L, "__call");
	lua_pushcfunction(L, lbox_info_replication_anon_call);
	lua_settable(L, -3);

	lua_setmetatable(L, -2);
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
	if (self != NULL &&
	    (self->id != REPLICA_ID_NIL || cfg_replication_anon)) {
		lua_pushinteger(L, self->id);
	} else {
		luaL_pushnull(L);
	}
	return 1;
}

static int
lbox_info_uuid(struct lua_State *L)
{
	luaT_pushuuidstr(L, &INSTANCE_UUID);
	return 1;
}

/** box.info.name. */
static int
lbox_info_name(struct lua_State *L)
{
	if (*INSTANCE_NAME == 0)
		luaL_pushnull(L);
	else
		lua_pushstring(L, INSTANCE_NAME);
	return 1;
}

static int
lbox_info_lsn(struct lua_State *L)
{
	/* See comments in lbox_info_id */
	struct replica *self = replica_by_uuid(&INSTANCE_UUID);
	if (self != NULL &&
	    (self->id != REPLICA_ID_NIL || cfg_replication_anon)) {
		luaL_pushint64(L, vclock_get(box_vclock, self->id));
	} else {
		luaL_pushint64(L, -1);
	}
	return 1;
}

static int
lbox_info_signature(struct lua_State *L)
{
	luaL_pushint64(L, vclock_sum(box_vclock));
	return 1;
}

static int
lbox_info_ro(struct lua_State *L)
{
	lua_pushboolean(L, box_is_ro());
	return 1;
}

static int
lbox_info_ro_reason(struct lua_State *L)
{
	/* Even if NULL, it works like lua_pushnil(), so this is fine. */
	lua_pushstring(L, box_ro_reason());
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
	luaT_pushvclock(L, box_vclock);
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

/** box.info.replicaset. */
static int
lbox_info_replicaset(struct lua_State *L)
{
	lua_createtable(L, 0, 2);
	lua_pushliteral(L, "uuid");
	luaT_pushuuidstr(L, &REPLICASET_UUID);
	lua_settable(L, -3);
	if (*REPLICASET_NAME == 0)
		luaL_pushnull(L);
	else
		lua_pushstring(L, REPLICASET_NAME);
	lua_setfield(L, -2, "name");
	return 1;
}

/** box.info.cluster. */
static int
lbox_info_cluster(struct lua_State *L)
{
	if (!box_info_cluster_new_meaning)
		return lbox_info_replicaset(L);
	lua_createtable(L, 0, 1);
	if (*CLUSTER_NAME == 0)
		luaL_pushnull(L);
	else
		lua_pushstring(L, CLUSTER_NAME);
	lua_setfield(L, -2, "name");
	return 1;
}

static int
lbox_info_memory_call(struct lua_State *L)
{
	if (box_check_configured() != 0)
		return luaT_error(L);

	struct engine_memory_stat stat;
	engine_memory_stat(&stat);

	lua_createtable(L, 0, 6);

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

	struct iproto_stats stats;
	iproto_stats_get(&stats);
	lua_pushstring(L, "net");
	luaL_pushuint64(L, stats.mem_used);
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

static int
lbox_info_gc_call(struct lua_State *L)
{
	if (box_check_configured() != 0)
		return luaT_error(L);

	int count;

	lua_newtable(L);

	lua_pushstring(L, "vclock");
	luaT_pushvclock(L, &gc.vclock);
	lua_settable(L, -3);

	lua_pushstring(L, "signature");
	luaL_pushint64(L, vclock_sum(&gc.vclock));
	lua_settable(L, -3);

	lua_pushstring(L, "checkpoint_is_in_progress");
	lua_pushboolean(L, gc.checkpoint_is_in_progress);
	lua_settable(L, -3);

	lua_pushstring(L, "is_paused");
	lua_pushboolean(L, gc.is_paused);
	lua_settable(L, -3);

	lua_pushstring(L, "wal_retention_vclock");
	struct vclock retention_vclock;
	wal_get_retention_vclock(&retention_vclock);
	if (vclock_is_set(&retention_vclock))
		luaT_pushvclock(L, &retention_vclock);
	else
		luaL_pushnull(L);
	lua_settable(L, -3);

	lua_pushstring(L, "checkpoints");
	lua_newtable(L);

	count = 0;
	struct gc_checkpoint *checkpoint;
	gc_foreach_checkpoint(checkpoint) {
		lua_createtable(L, 0, 2);

		lua_pushstring(L, "vclock");
		luaT_pushvclock(L, &checkpoint->vclock);
		lua_settable(L, -3);

		lua_pushstring(L, "signature");
		luaL_pushint64(L, vclock_sum(&checkpoint->vclock));
		lua_settable(L, -3);

		lua_pushstring(L, "references");
		lua_newtable(L);
		int ref_idx = 0;
		struct gc_checkpoint_ref *ref;
		gc_foreach_checkpoint_ref(ref, checkpoint) {
			lua_pushstring(L, ref->name);
			lua_rawseti(L, -2, ++ref_idx);
		}
		lua_settable(L, -3);

		lua_rawseti(L, -2, ++count);
	}
	lua_settable(L, -3);

	lua_pushstring(L, "consumers");
	lua_newtable(L);

	struct gc_consumer_iterator consumers;
	gc_consumer_iterator_init(&consumers);

	count = 0;
	struct gc_consumer *consumer;
	while ((consumer = gc_consumer_iterator_next(&consumers)) != NULL) {
		lua_createtable(L, 0, 3);

		lua_pushstring(L, "name");
		lua_pushstring(L, consumer->name);
		lua_settable(L, -3);

		lua_pushstring(L, "vclock");
		luaT_pushvclock(L, &consumer->vclock);
		lua_settable(L, -3);

		lua_pushstring(L, "signature");
		luaL_pushint64(L, vclock_sum(&consumer->vclock));
		lua_settable(L, -3);

		lua_rawseti(L, -2, ++count);
	}
	lua_settable(L, -3);

	return 1;
}

static int
lbox_info_gc(struct lua_State *L)
{
	lua_newtable(L);

	lua_newtable(L); /* metatable */

	lua_pushstring(L, "__call");
	lua_pushcfunction(L, lbox_info_gc_call);
	lua_settable(L, -3);

	lua_setmetatable(L, -2);
	return 1;
}

static int
lbox_info_vinyl_call(struct lua_State *L)
{
	if (box_check_configured() != 0)
		return luaT_error(L);

	struct info_handler h;
	luaT_info_handler_create(&h, L);
	struct engine *vinyl = engine_by_name("vinyl");
	assert(vinyl != NULL);
	vinyl_engine_stat(vinyl, &h);
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

static int
lbox_info_sql_call(struct lua_State *L)
{
	if (box_check_configured() != 0)
		return luaT_error(L);

	struct info_handler h;
	luaT_info_handler_create(&h, L);
	sql_stmt_cache_stat(&h);

	return 1;
}

static int
lbox_info_sql(struct lua_State *L)
{
	lua_newtable(L);
	lua_newtable(L); /* metatable */
	lua_pushstring(L, "__call");
	lua_pushcfunction(L, lbox_info_sql_call);
	lua_settable(L, -3);

	lua_setmetatable(L, -2);
	return 1;
}

static int
lbox_info_listen(struct lua_State *L)
{
	int count = iproto_addr_count();
	if (count == 0) {
		lua_pushnil(L);
		return 1;
	}
	char addrbuf[SERVICE_NAME_MAXLEN];
	if (count == 1) {
		lua_pushstring(L, iproto_addr_str(addrbuf, 0));
		return 1;
	}
	lua_createtable(L, count, 0);
	for (int i = 0; i < count; i++) {
		lua_pushstring(L, iproto_addr_str(addrbuf, i));
		lua_rawseti(L, -2, i + 1);
	}
	return 1;
}

static int
lbox_info_election(struct lua_State *L)
{
	struct raft *raft = box_raft();
	lua_createtable(L, 0, 4);
	lua_pushstring(L, raft_state_str(raft->state));
	lua_setfield(L, -2, "state");
	luaL_pushuint64(L, raft->volatile_term);
	lua_setfield(L, -2, "term");
	lua_pushinteger(L, raft->volatile_vote);
	lua_setfield(L, -2, "vote");
	lua_pushinteger(L, raft->leader);
	lua_setfield(L, -2, "leader");
	if (raft_is_enabled(raft)) {
		if (raft->leader != 0) {
			char *leader_name = replica_by_id(raft->leader)->name;
			if (*leader_name == 0)
				luaL_pushnull(L);
			else
				lua_pushstring(L, leader_name);
			lua_setfield(L, -2, "leader_name");
		}
		lua_pushnumber(L, raft_leader_idle(raft));
		lua_setfield(L, -2, "leader_idle");
	}
	return 1;
}

static int
lbox_info_synchro(struct lua_State *L)
{
	lua_createtable(L, 0, 2);

	/* Quorum value may be evaluated via formula */
	lua_pushinteger(L, replication_synchro_quorum);
	lua_setfield(L, -2, "quorum");

	/* Queue information. */
	struct txn_limbo *queue = &txn_limbo;
	lua_createtable(L, 0, 3);
	lua_pushnumber(L, queue->len);
	lua_setfield(L, -2, "len");
	lua_pushnumber(L, queue->owner_id);
	lua_setfield(L, -2, "owner");
	lua_pushboolean(L, latch_is_locked(&queue->promote_latch));
	lua_setfield(L, -2, "busy");
	luaL_pushuint64(L, queue->promote_greatest_term);
	lua_setfield(L, -2, "term");
	if (queue->len == 0) {
		lua_pushnumber(L, 0);
	} else {
		struct txn_limbo_entry *oldest_entry =
			txn_limbo_first_entry(queue);
		double now = fiber_clock();
		lua_pushnumber(L, now - oldest_entry->insertion_time);
	}
	lua_setfield(L, -2, "age");
	lua_pushnumber(L, queue->confirm_lag);
	lua_setfield(L, -2, "confirm_lag");
	lua_setfield(L, -2, "queue");

	return 1;
}

static int
lbox_schema_version(struct lua_State *L)
{
	luaL_pushuint64(L, box_schema_version());
	return 1;
}

/** gethostname() Lua interface inside box.info. */
static int
lbox_info_hostname(struct lua_State *L)
{
	char buffer[TT_HOST_NAME_MAX + 1];
	int rc = gethostname(buffer, sizeof(buffer));
	if (rc != 0) {
		say_warn_ratelimited("failed to get hostname: %s",
				     tt_strerror(errno));
		lua_pushnil(L);
		return 1;
	}
	lua_pushstring(L, buffer);
	return 1;
}

static int
lbox_info_config(struct lua_State *L)
{
	/* require('config'):info('v2') */
	lua_getglobal(L, "require");
	lua_pushliteral(L, "config");
	if (lua_pcall(L, 1, 1, 0) != 0)
		goto error;
	/* Stack: config. */
	lua_getfield(L, -1, "info");
	/* Stack: config, config.info. */
	lua_insert(L, -2);
	/* Stack: config.info, config. */
	lua_pushliteral(L, "v2");
	/* Stack: config.info, config, 'v2'. */
	if (lua_pcall(L, 2, 1, 0) != 0)
		goto error;
	return 1;

error:
	/*
	 * An error shouldn't occur by construction.
	 *
	 * However, box.info() is an important call and we
	 * shouldn't fail it in any circumstances, including a
	 * problem in the config:info() implementation.
	 *
	 * So, we don't raise an error here and place it to the
	 * result instead.
	 */
	lua_newtable(L);
	lua_insert(L, -2);
	lua_setfield(L, -2, "error");
	return 1;
}

static const struct luaL_Reg lbox_info_dynamic_meta[] = {
	{"id", lbox_info_id},
	{"uuid", lbox_info_uuid},
	{"name", lbox_info_name},
	{"lsn", lbox_info_lsn},
	{"signature", lbox_info_signature},
	{"vclock", lbox_info_vclock},
	{"ro", lbox_info_ro},
	{"ro_reason", lbox_info_ro_reason},
	{"replication", lbox_info_replication},
	{"replication_anon", lbox_info_replication_anon},
	{"replicaset", lbox_info_replicaset},
	{"status", lbox_info_status},
	{"uptime", lbox_info_uptime},
	{"pid", lbox_info_pid},
	{"cluster", lbox_info_cluster},
	{"memory", lbox_info_memory},
	{"gc", lbox_info_gc},
	{"vinyl", lbox_info_vinyl},
	{"sql", lbox_info_sql},
	{"listen", lbox_info_listen},
	{"election", lbox_info_election},
	{"synchro", lbox_info_synchro},
	{"schema_version", lbox_schema_version},
	{"hostname", lbox_info_hostname},
	{"config", lbox_info_config},
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
	lua_pushstring(L, "package");
	lua_pushstring(L, tarantool_package());
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
	luaL_findtable(L, LUA_GLOBALSINDEX, "box.info", 0);
	lua_newtable(L);		/* metatable for info */

	lua_pushstring(L, "__index");

	lua_newtable(L); /* table for __index */
	luaL_setfuncs(L, lbox_info_dynamic_meta, 0);
	luaL_setfuncs(L, lbox_info_dynamic_meta_v16, 0);
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
