/*
 *
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
#include "stat.h"

#include <string.h>
#include <rmean.h>

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include "box/box.h"
#include "box/iproto.h"
#include "box/engine.h"
#include "box/vinyl.h"
#include "box/sql.h"
#include "box/memtx_engine.h"
#include "info/info.h"
#include "lua/info.h"
#include "lua/utils.h"

extern struct rmean *rmean_box;
extern struct rmean *rmean_error;
extern struct rmean *rmean_tx_wal_bus;

/**
 * Function gets table by it's @name from the table which located
 * at the top of the lua stack. Then adds 'current' field to this
 * table.
 */
static void
inject_current_stat(struct lua_State *L, const char *name, size_t val)
{
	lua_pushstring(L, name);
	lua_rawget(L, -2);
	lua_pushstring(L, "current");
	lua_pushnumber(L, val);
	lua_rawset(L, -3);
	lua_pop(L, 1);
}

static void
inject_iproto_stats(struct lua_State *L, struct iproto_stats *stats)
{
	inject_current_stat(L, "CONNECTIONS", stats->connections);
	inject_current_stat(L, "STREAMS", stats->streams);
	inject_current_stat(L, "REQUESTS", stats->requests);
	inject_current_stat(L, "REQUESTS_IN_PROGRESS",
			    stats->requests_in_progress);
	inject_current_stat(L, "REQUESTS_IN_STREAM_QUEUE",
			    stats->requests_in_stream_queue);
}

static void
fill_stat_item(struct lua_State *L, int rps, int64_t total)
{
	lua_pushstring(L, "rps");
	lua_pushnumber(L, rps);
	lua_settable(L, -3);

	lua_pushstring(L, "total");
	lua_pushnumber(L, total);
	lua_settable(L, -3);
}

static int
set_stat_item(const char *name, int rps, int64_t total, void *cb_ctx)
{
	struct lua_State *L = (struct lua_State *) cb_ctx;

	lua_pushstring(L, name);
	lua_newtable(L);

	fill_stat_item(L, rps, total);

	lua_settable(L, -3);

	return 0;
}

/**
 * A stat_foreach() callback used to handle access to e.g.
 * box.stats.DELETE.
 */
static int
seek_stat_item(const char *name, int rps, int64_t total, void *cb_ctx)
{
	struct lua_State *L = (struct lua_State *) cb_ctx;
	if (strcmp(name, lua_tostring(L, -1)) != 0)
		return 0;

	lua_newtable(L);
	fill_stat_item(L, rps, total);

	return 1;
}

/**
 * Returns false if a box.stat item should be excluded from the output.
 */
static bool
filter_box_stat_item(const char *name)
{
	return strcmp(name, "OK") != 0 &&
	       strcmp(name, "CALL_16") != 0 &&
	       strcmp(name, "NOP") != 0;
}

static int
set_box_stat_item(const char *name, int rps, int64_t total, void *cb_ctx)
{
	return filter_box_stat_item(name) ?
	       set_stat_item(name, rps, total, cb_ctx) : 0;
}

static int
seek_box_stat_item(const char *name, int rps, int64_t total, void *cb_ctx)
{
	return filter_box_stat_item(name) ?
	       seek_stat_item(name, rps, total, cb_ctx) : 0;
}

static int
lbox_stat_index(struct lua_State *L)
{
	luaL_checkstring(L, -1);
	int res = rmean_foreach(rmean_box, seek_box_stat_item, L);
	if (res)
		return res;
	return rmean_foreach(rmean_error, seek_stat_item, L);
}

static int
lbox_stat_call(struct lua_State *L)
{
	lua_newtable(L);
	rmean_foreach(rmean_box, set_box_stat_item, L);
	rmean_foreach(rmean_error, set_stat_item, L);
	return 1;
}

static int
lbox_stat_vinyl(struct lua_State *L)
{
	struct info_handler h;
	luaT_info_handler_create(&h, L);
	struct engine *vinyl = engine_by_name("vinyl");
	assert(vinyl != NULL);
	vinyl_engine_stat(vinyl, &h);
	return 1;
}

/* box.stat.memtx() */
static int
lbox_stat_memtx(struct lua_State *L)
{
	struct info_handler h;
	luaT_info_handler_create(&h, L);
	struct engine *memtx = engine_by_name("memtx");
	assert(memtx != NULL);
	memtx_engine_stat((struct memtx_engine *)memtx, &h);
	return 1;
}

/* box.stat.memtx.tx() */
static int
lbox_stat_memtx_tx(struct lua_State *L)
{
	lbox_stat_memtx(L);
	lua_getfield(L, 1, "tx");
	return 1;
}

static int
lbox_stat_reset(struct lua_State *L)
{
	(void)L;
	box_reset_stat();
	iproto_reset_stat();
	return 0;
}

/**
 * Push a table with a network metric to a Lua stack.
 *
 * Expects one argument with a name of a needed metric. The pushed
 * table contains some subset of 'total', 'rps', 'current' fields.
 *
 * Metrics are the same as in lbox_stat_net_call().
 */
static int
lbox_stat_net_index(struct lua_State *L)
{
	const char *key = luaL_checkstring(L, -1);
	if (iproto_rmean_foreach(seek_stat_item, L) == 0)
		return 0;

	struct iproto_stats stats;
	iproto_stats_get(&stats);
	if (strcmp(key, "CONNECTIONS") == 0) {
		lua_pushstring(L, "current");
		lua_pushnumber(L, stats.connections);
		lua_rawset(L, -3);
	} else if (strcmp(key, "STREAMS") == 0) {
		lua_pushstring(L, "current");
		lua_pushnumber(L, stats.streams);
		lua_rawset(L, -3);
	} else if (strcmp(key, "REQUESTS") == 0) {
		lua_pushstring(L, "current");
		lua_pushnumber(L, stats.requests);
		lua_rawset(L, -3);
	} else if (strcmp(key, "REQUESTS_IN_PROGRESS") == 0) {
		lua_pushstring(L, "current");
		lua_pushnumber(L, stats.requests_in_progress);
		lua_rawset(L, -3);
	} else if (strcmp(key, "REQUESTS_IN_STREAM_QUEUE") == 0) {
		lua_pushstring(L, "current");
		lua_pushnumber(L, stats.requests_in_stream_queue);
		lua_rawset(L, -3);
	}
	return 1;
}

/**
 * Push a table of network metrics to a Lua stack.
 *
 * Metrics and their fields are:
 *
 * - SENT (packets): total, rps;
 * - RECEIVED (packets): total, rps;
 * - CONNECTIONS: total, rps, current;
 * - STREAMS: total, rps, current;
 * - REQUESTS: total, rps, current;
 * - REQUESTS_IN_PROGRESS: total, rps, current;
 * - REQUESTS_IN_STREAM_QUEUE: total, rps, current.
 *
 * These fields have the following meaning:
 *
 * - total -- amount of events since start;
 * - rps -- amount of events per second, mean over last 5 seconds;
 * - current -- amount of resources currently hold (say, number of
 *   open connections).
 */
static int
lbox_stat_net_call(struct lua_State *L)
{
	lua_newtable(L);
	iproto_rmean_foreach(set_stat_item, L);
	struct iproto_stats stats;
	iproto_stats_get(&stats);
	inject_iproto_stats(L, &stats);
	return 1;
}

/**
 * Same as `lbox_stat_net_index` but for thread with given id.
 */
static int
lbox_stat_net_thread_index(struct lua_State *L)
{
	const int thread_id = luaL_checkinteger(L, -1) - 1;
	if (thread_id < 0 || thread_id >= iproto_threads_count)
		return 0;

	lua_newtable(L);
	iproto_thread_rmean_foreach(thread_id, set_stat_item, L);
	struct iproto_stats stats;
	iproto_thread_stats_get(&stats, thread_id);
	inject_iproto_stats(L, &stats);
	return 1;
}

/**
 * Same as `lbox_stat_net_call` but for thread with given id.
 */
static int
lbox_stat_net_thread_call(struct lua_State *L)
{
	struct iproto_stats stats;
	lua_newtable(L);
	for (int thread_id = 0; thread_id < iproto_threads_count; thread_id++) {
		lua_newtable(L);
		iproto_thread_rmean_foreach(thread_id, set_stat_item, L);
		iproto_thread_stats_get(&stats, thread_id);
		inject_iproto_stats(L, &stats);
		lua_rawseti(L, -2, thread_id + 1);
	}
	return 1;
}

static int
lbox_stat_sql(struct lua_State *L)
{
	struct info_handler info;
	luaT_info_handler_create(&info, L);
	sql_debug_info(&info);
	return 1;
}

static const struct luaL_Reg lbox_stat_meta [] = {
	{"__index", lbox_stat_index},
	{"__call",  lbox_stat_call},
	{NULL, NULL}
};

static const struct luaL_Reg lbox_stat_net_meta [] = {
	{"__index", lbox_stat_net_index},
	{"__call",  lbox_stat_net_call},
	{NULL, NULL}
};

static const struct luaL_Reg lbox_stat_net_thread_meta [] = {
	{"__index", lbox_stat_net_thread_index},
	{"__call",  lbox_stat_net_thread_call},
	{NULL, NULL}
};

/*
 * Memtx transaction statistics can be obtained not only as box.stat.memtx().tx
 * but also by calling box.stat.memtx.tx(). This is required only for backward
 * compatibility. Please don't add new functions to the box.stat.memtx table.
 */
static const struct luaL_Reg lbox_stat_memtx_funcs[] = {
	{"tx", lbox_stat_memtx_tx},
	{NULL, NULL}
};

static const struct luaL_Reg lbox_stat_memtx_meta[] = {
	{"__call", lbox_stat_memtx},
	{NULL, NULL}
};

/** Initialize box.stat package. */
void
box_lua_stat_init(struct lua_State *L)
{
	static const struct luaL_Reg statlib [] = {
		{"vinyl", lbox_stat_vinyl},
		{"reset", lbox_stat_reset},
		{"sql", lbox_stat_sql},
		{NULL, NULL}
	};

	luaL_findtable(L, LUA_GLOBALSINDEX, "box.stat", 0);
	luaL_setfuncs(L, statlib, 0);

	lua_newtable(L);
	luaL_setfuncs(L, lbox_stat_meta, 0);
	lua_setmetatable(L, -2);
	lua_pop(L, 1); /* stat module */

	luaL_findtable(L, LUA_GLOBALSINDEX, "box.stat.net", 0);
	lua_newtable(L);
	luaL_setfuncs(L, lbox_stat_net_meta, 0);
	lua_setmetatable(L, -2);
	lua_pop(L, 1); /* stat net module */

	luaL_findtable(L, LUA_GLOBALSINDEX, "box.stat.net.thread", 0);
	lua_newtable(L);
	luaL_setfuncs(L, lbox_stat_net_thread_meta, 0);
	lua_setmetatable(L, -2);
	lua_pop(L, 1); /* stat net module */

	luaL_findtable(L, LUA_GLOBALSINDEX, "box.stat.memtx", 0);
	luaL_setfuncs(L, lbox_stat_memtx_funcs, 0);
	lua_newtable(L);
	luaL_setfuncs(L, lbox_stat_memtx_meta, 0);
	lua_setmetatable(L, -2);
	lua_pop(L, 1); /* stat memtx module */
}
