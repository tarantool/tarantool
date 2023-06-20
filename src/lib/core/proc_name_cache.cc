/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2022, Tarantool AUTHORS, please see AUTHORS file.
 */

#include "assoc.h"
#include "fiber.h"
#include "proc_name_cache.h"

#include "small/region.h"

enum {
	PROC_NAME_MAX = 64,
};

/* Procedure name hash table entry. */
struct proc_name_cache_entry {
	/* Demangled procedure name. */
	char name[PROC_NAME_MAX];
	/* Procedure offset. */
	unw_word_t offset;
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

private:
	ProcNameCache() noexcept : proc_name_cache{mh_i64ptr_new()}
	{
	}

	~ProcNameCache()
	{
		mh_int_t i;
		mh_foreach(proc_name_cache, i)
			free(mh_i64ptr_node(proc_name_cache, i)->val);
		mh_i64ptr_delete(proc_name_cache);
	}
};

const char *
proc_name_cache_find(unw_word_t ip, unw_word_t *offs)
{
	mh_i64ptr_t *proc_name_cache =
		ProcNameCache::instance().proc_name_cache;
	mh_int_t k = mh_i64ptr_find(proc_name_cache, ip, nullptr);
	if (k == mh_end(proc_name_cache))
		return nullptr;
	void *val = mh_i64ptr_node(proc_name_cache, k)->val;
	auto *entry = static_cast<proc_name_cache_entry *>(val);
	*offs = entry->offset;
	return entry->name;
}

void
proc_name_cache_insert(unw_word_t ip, const char *name, unw_word_t offs)
{
	auto entry = static_cast<proc_name_cache_entry *>(xmalloc(
		sizeof(proc_name_cache_entry)));
	entry->offset = offs;
	strlcpy(entry->name, name, PROC_NAME_MAX);
	mh_i64ptr_node_t node = {ip, entry};
	mh_i64ptr_t *proc_name_cache =
		ProcNameCache::instance().proc_name_cache;
	mh_i64ptr_put(proc_name_cache, &node, nullptr, nullptr);
}
