/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2022, Tarantool AUTHORS, please see AUTHORS file.
 */

#include "proc_name_cache.h"

#include <stdint.h>

#include "assoc.h"
#include "fiber.h"
#include "small/region.h"

enum {
	PROC_NAME_MAX = 64,
};

/* Procedure name hash table entry. */
struct proc_name_cache_entry {
	/* Demangled procedure name. */
	char name[PROC_NAME_MAX];
	/* Procedure offset. */
	uintptr_t offset;
};

/*
 * RAII wrapper around the procedure name cache and its region.
 */
struct ProcNameCache final {
public:
	ProcNameCache(ProcNameCache &other) = delete;

	ProcNameCache &operator=(ProcNameCache &other) = delete;

	static ProcNameCache &instance() noexcept
	{
		thread_local ProcNameCache singleton;
		return singleton;
	}

	mh_i64ptr_t *proc_name_cache;
	region proc_name_cache_entry_region;

private:
	ProcNameCache() noexcept : proc_name_cache{mh_i64ptr_new()},
				   proc_name_cache_entry_region{}
	{
		region_create(&proc_name_cache_entry_region, &cord()->slabc);
	}

	~ProcNameCache()
	{
		region_destroy(&proc_name_cache_entry_region);
		mh_i64ptr_delete(proc_name_cache);
	}
};

const char *
proc_name_cache_find(void *ip, uintptr_t *offs)
{
	mh_i64ptr_t *proc_name_cache =
		ProcNameCache::instance().proc_name_cache;
	mh_int_t k = mh_i64ptr_find(proc_name_cache, (uintptr_t)ip, nullptr);
	if (k == mh_end(proc_name_cache))
		return nullptr;
	void *val = mh_i64ptr_node(proc_name_cache, k)->val;
	auto *entry = static_cast<proc_name_cache_entry *>(val);
	*offs = entry->offset;
	return entry->name;
}

void
proc_name_cache_insert(void *ip, const char *name, uintptr_t offs)
{
	region *proc_name_cache_entry_region =
		&ProcNameCache::instance().proc_name_cache_entry_region;
	size_t sz;
	proc_name_cache_entry *entry =
		region_alloc_object(proc_name_cache_entry_region,
				    typeof(*entry), &sz);
	if (unlikely(entry == nullptr))
		return;
	entry->offset = offs;
	strlcpy(entry->name, name, PROC_NAME_MAX);
	mh_i64ptr_node_t node = {(uintptr_t)ip, entry};
	mh_i64ptr_t *proc_name_cache =
		ProcNameCache::instance().proc_name_cache;
	mh_i64ptr_put(proc_name_cache, &node, nullptr, nullptr);
}
