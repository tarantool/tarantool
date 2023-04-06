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
#include "trivia/util.h"

#include "box/lua/slab.h"
#include "lua/utils.h"
#include "lua/xml.h"
#include "lua/serializer.h" /* luaL_setmaphint */

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <lj_obj.h> /* internals: lua in box.runtime.info() */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if defined(HAVE_MALLOC_INFO)
# include <malloc.h>
#endif /* defined(HAVE_MALLOC_INFO) */

#include "small/small.h"
#include "small/quota.h"
#include "memory.h"
#include "box/engine.h"
#include "box/memtx_engine.h"
#include "box/allocator.h"
#include "box/tuple.h"

static int
small_stats_lua_cb(const void *stats, void *cb_ctx)
{
	const struct mempool_stats *mempool_stats =
		(const struct mempool_stats *)stats;
	/** Don't publish information about empty slabs. */
	if (mempool_stats->slabcount == 0)
		return 0;

	struct lua_State *L = (struct lua_State *) cb_ctx;

	/*
	 * Create a Lua table for every slab class. A class is
	 * defined by its item size.
	 */
	/** Assign next slab size to the next member of an array. */
	lua_pushnumber(L, lua_objlen(L, -1) + 1);
	lua_newtable(L);
	/**
	 * This is in fact only to force YaML flow "compact" for this
	 * table.
	 */
	luaL_setmaphint(L, -1);

	lua_pushstring(L, "mem_used");
	luaL_pushuint64(L, mempool_stats->totals.used);
	lua_settable(L, -3);

	lua_pushstring(L, "slab_size");
	luaL_pushuint64(L, mempool_stats->slabsize);
	lua_settable(L, -3);

	lua_pushstring(L, "mem_free");
	luaL_pushuint64(L, mempool_stats->totals.total -
			mempool_stats->totals.used);
	lua_settable(L, -3);

	lua_pushstring(L, "item_size");
	luaL_pushuint64(L, mempool_stats->objsize);
	lua_settable(L, -3);

	lua_pushstring(L, "slab_count");
	luaL_pushuint64(L, mempool_stats->slabcount);
	lua_settable(L, -3);

	lua_pushstring(L, "item_count");
	luaL_pushuint64(L, mempool_stats->objcount);
	lua_settable(L, -3);

	lua_settable(L, -3);
	return 0;
}

static int
lbox_slab_stats(struct lua_State *L)
{
	struct memtx_engine *memtx;
	memtx = (struct memtx_engine *)engine_by_name("memtx");

	struct allocator_stats totals;
	memset(&totals, 0, sizeof(totals));
	lua_newtable(L);
	/*
	 * List all slabs used for tuples and slabs used for
	 * indexes, with their stats.
	 */
	SmallAlloc::stats(&totals, small_stats_lua_cb, L);
	struct mempool_stats index_stats;
	mempool_stats(&memtx->index_extent_pool, &index_stats);
	small_stats_lua_cb(&index_stats, L);

	return 1;
}

static int
lbox_slab_info(struct lua_State *L)
{
	struct memtx_engine *memtx;
	memtx = (struct memtx_engine *)engine_by_name("memtx");

	struct allocator_stats stats;
	memset(&stats, 0, sizeof(stats));

	/*
	 * List all slabs used for tuples and slabs used for
	 * indexes, with their stats.
	 */
	lua_newtable(L);
	allocators_stats(&stats);
	struct mempool_stats index_stats;
	mempool_stats(&memtx->index_extent_pool, &index_stats);

	double ratio;
	char ratio_buf[32];

	ratio = 100 * ((double) (stats.small.used + stats.sys.used)
		/ ((double) (stats.small.total + stats.sys.total) + 0.0001));
	snprintf(ratio_buf, sizeof(ratio_buf), "%0.2lf%%", ratio);

	/** How much address space has been already touched */
	lua_pushstring(L, "items_size");
	luaL_pushuint64(L, stats.small.total + stats.sys.total);
	lua_settable(L, -3);
	/**
	 * How much of this formatted address space is used for
	 * actual data.
	 */
	lua_pushstring(L, "items_used");
	luaL_pushuint64(L, stats.small.used + stats.sys.used);
	lua_settable(L, -3);

	/*
	 * Fragmentation factor for tuples. Don't account indexes,
	 * even if they are fragmented, there is nothing people
	 * can do about it.
	 */
	lua_pushstring(L, "items_used_ratio");
	lua_pushstring(L, ratio_buf);
	lua_settable(L, -3);

	/** How much address space has been already touched
	 * (tuples and indexes) */
	lua_pushstring(L, "arena_size");
	/*
	 * We could use totals.total + index_stats.total here,
	 * but this would not account for slabs which are sitting
	 * in slab cache or in the arena, available for reuse.
	 * Make sure a simple formula:
	 * items_used_ratio > 0.9 && arena_used_ratio > 0.9 &&
	 * quota_used_ratio > 0.9 work as an indicator
	 * for reaching Tarantool memory limit.
	 */
	size_t arena_size = memtx->arena.used;
	luaL_pushuint64(L, arena_size);
	lua_settable(L, -3);
	/**
	 * How much of this formatted address space is used for
	 * data (tuples and indexes).
	 */
	lua_pushstring(L, "arena_used");
	/** System allocator does not use arena. */
	luaL_pushuint64(L, stats.small.used + index_stats.totals.used);
	lua_settable(L, -3);

	ratio = 100 * ((double) (stats.small.used + index_stats.totals.used)
		       / (double) arena_size);
	snprintf(ratio_buf, sizeof(ratio_buf), "%0.1lf%%", ratio);

	lua_pushstring(L, "arena_used_ratio");
	lua_pushstring(L, ratio_buf);
	lua_settable(L, -3);

	/*
	 * This is pretty much the same as
	 * box.cfg.slab_alloc_arena, but in bytes
	 */
	lua_pushstring(L, "quota_size");
	luaL_pushuint64(L, quota_total(&memtx->quota));
	lua_settable(L, -3);

	/*
	 * How much quota has been booked - reflects the total
	 * size of slabs in various slab caches.
	 */
	lua_pushstring(L, "quota_used");
	luaL_pushuint64(L, quota_used(&memtx->quota));
	lua_settable(L, -3);

	/**
	 * This should be the same as arena_size/arena_used, however,
	 * don't trust totals in the most important monitoring
	 * factor, it's the quota that give you OOM error in the
	 * end of the day.
	 */
	ratio = 100 * ((double) quota_used(&memtx->quota) /
		 ((double) quota_total(&memtx->quota) + 0.0001));
	snprintf(ratio_buf, sizeof(ratio_buf), "%0.2lf%%", ratio);

	lua_pushstring(L, "quota_used_ratio");
	lua_pushstring(L, ratio_buf);
	lua_settable(L, -3);

	return 1;
}

static int
lbox_runtime_info(struct lua_State *L)
{
	lua_newtable(L);

	lua_pushstring(L, "used");
	luaL_pushuint64(L, runtime.used);
	lua_settable(L, -3);

	lua_pushstring(L, "maxalloc");
	luaL_pushuint64(L, quota_total(runtime.quota));
	lua_settable(L, -3);

	/*
	 * Lua GC heap size
	 */
	lua_pushstring(L, "lua");
	lua_pushinteger(L, G(L)->gc.total);
	lua_settable(L, -3);

	luaL_pushuint64(L, tuple_runtime_memory_used());
	lua_setfield(L, -2, "tuple");

	return 1;
}

static int
lbox_slab_check(MAYBE_UNUSED struct lua_State *L)
{
	slab_cache_check(SmallAlloc::get_alloc()->cache);
	return 0;
}

/*
 * Decodes and returns the XML document returned by malloc_info() as is.
 *
 * This is an internal function that isn't supposed to be called by users,
 * but it may be useful for developers.
 *
 * Returns an empty table if malloc_info() isn't supported by the system.
 * Raises a Lua error if it fails to retrieve or parse malloc_info() output.
 */
static int
lbox_malloc_internal_info(struct lua_State *L)
{
#if defined(HAVE_MALLOC_INFO)
	char *buf = NULL;
	size_t buf_size = 0;
	FILE *fp = open_memstream(&buf, &buf_size);
	if (fp == NULL) {
		diag_set(SystemError, "failed to open memory stream");
		return luaT_error(L);
	}
	if (malloc_info(0, fp) != 0) {
		diag_set(SystemError, "failed to get malloc info");
		fclose(fp);
		free(buf);
		return luaT_error(L);
	}
	fclose(fp);
	lua_pushlstring(L, buf, buf_size);
	free(buf);
	return luaT_xml_decode(L);
#else /* !defined(HAVE_MALLOC_INFO) */
	lua_newtable(L);
	return 1;
#endif /* defined(HAVE_MALLOC_INFO) */
}

/*
 * Returns the malloc memory usage information in a table
 *
 *   {
 *     size = <total allocated>,
 *     used = <actually used>,
 *   }
 *
 * (all numbers are in bytes).
 *
 * The information is retrieved with malloc_info(). If it isn't supported by
 * the system or its format is unknown, {size = 0, used = 0} is returned.
 *
 * This function never raises.
 */
static int
lbox_malloc_info(struct lua_State *L)
{
	int version = 0;
	uint64_t total = 0;
	uint64_t available = 0;
	lua_pushcfunction(L, lbox_malloc_internal_info);
	if (luaT_call(L, 0, 1) != 0)
		goto out;
	/*
	 * The XML document name returned by malloc_info is expected to be
	 * "malloc" so the document content should be in malloc[1].
	 */
	assert(lua_istable(L, -1));
	lua_getfield(L, -1, "malloc");
	if (!lua_istable(L, -1))
		goto out;
	lua_rawgeti(L, -1, 1);
	if (!lua_istable(L, -1))
		goto out;
	/*
	 * First, check the malloc_info version. The only known version is 1.
	 * It's pointless to proceed if the version is different.
	 */
	lua_getfield(L, -1, "version");
	version = lua_tonumber(L, -1);
	lua_pop(L, 1);
	if (version != 1)
		goto out;
	/*
	 * Extract the size of used memory. Even though the document version is
	 * valid, we still need to be careful accessing it.
	 */
	assert(lua_istable(L, -1));
	lua_getfield(L, -1, "system");
	if (lua_istable(L, -1)) {
		lua_pushnil(L);
		for (; lua_next(L, -2) != 0; lua_pop(L, 1)) {
			if (!lua_istable(L, -1))
				continue; /* skip XML attributes */
			lua_getfield(L, -1, "type");
			const char *key = lua_tostring(L, -1);
			lua_pop(L, 1);
			lua_getfield(L, -1, "size");
			uint64_t size = luaL_touint64(L, -1);
			lua_pop(L, 1);
			if (key == NULL) {
				continue;
			} else if (strcmp(key, "current") == 0) {
				total += size;
			}
		}
	}
	lua_pop(L, 1);
	lua_getfield(L, -1, "total");
	if (lua_istable(L, -1)) {
		lua_pushnil(L);
		for (; lua_next(L, -2) != 0; lua_pop(L, 1)) {
			if (!lua_istable(L, -1))
				continue; /* skip XML attributes */
			lua_getfield(L, -1, "type");
			const char *key = lua_tostring(L, -1);
			lua_pop(L, 1);
			lua_getfield(L, -1, "size");
			uint64_t size = luaL_touint64(L, -1);
			lua_pop(L, 1);
			if (key == NULL) {
				continue;
			} else if (strcmp(key, "mmap") == 0) {
				total += size;
			} else if (strcmp(key, "fast") == 0 ||
				   strcmp(key, "rest") == 0) {
				available += size;
			}
		}
	}
	lua_pop(L, 1);
out:
	/* Return memory usage information. */
	lua_newtable(L);
	luaL_pushuint64(L, total);
	lua_setfield(L, -2, "size");
	luaL_pushuint64(L, total - available);
	lua_setfield(L, -2, "used");
	return 1;
}

/** Initialize box.slab package. */
void
box_lua_slab_init(struct lua_State *L)
{
	lua_getfield(L, LUA_GLOBALSINDEX, "box");
	lua_pushstring(L, "slab");
	lua_newtable(L);

	lua_pushstring(L, "info");
	lua_pushcfunction(L, lbox_slab_info);
	lua_settable(L, -3);

	lua_pushstring(L, "stats");
	lua_pushcfunction(L, lbox_slab_stats);
	lua_settable(L, -3);

	lua_pushstring(L, "check");
	lua_pushcfunction(L, lbox_slab_check);
	lua_settable(L, -3);

	lua_settable(L, -3); /* box.slab */

	lua_pushstring(L, "runtime");
	lua_newtable(L);

	lua_pushstring(L, "info");
	lua_pushcfunction(L, lbox_runtime_info);
	lua_settable(L, -3);

	lua_settable(L, -3); /* box.runtime */

	lua_pushstring(L, "malloc");
	lua_newtable(L);

	lua_pushstring(L, "info");
	lua_pushcfunction(L, lbox_malloc_info);
	lua_settable(L, -3);

	lua_pushstring(L, "internal");
	lua_newtable(L);

	lua_pushstring(L, "info");
	lua_pushcfunction(L, lbox_malloc_internal_info);
	lua_settable(L, -3);

	lua_settable(L, -3); /* box.malloc.internal */
	lua_settable(L, -3); /* box.malloc */

	lua_pop(L, 1); /* box. */
}
