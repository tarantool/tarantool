/*
 * Copyright 2010-2019, Tarantool AUTHORS, please see AUTHORS file.
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
#include "sql_stmt_cache.h"

#include "assoc.h"
#include "error.h"
#include "execute.h"
#include "diag.h"
#include "info/info.h"

static struct sql_stmt_cache sql_stmt_cache;

void
sql_stmt_cache_init(void)
{
	sql_stmt_cache.hash = mh_i32ptr_new();
	if (sql_stmt_cache.hash == NULL)
		panic("out of memory");
	sql_stmt_cache.mem_quota = 0;
	sql_stmt_cache.mem_used = 0;
	rlist_create(&sql_stmt_cache.gc_queue);
}

void
sql_stmt_cache_stat(struct info_handler *h)
{
	info_begin(h);
	info_table_begin(h, "cache");
	info_append_int(h, "size", sql_stmt_cache.mem_used);
	uint32_t entry_count = 0;
	mh_int_t i;
	mh_foreach(sql_stmt_cache.hash, i)
		entry_count++;
	info_append_int(h, "stmt_count", entry_count);
	info_table_end(h);
	info_end(h);
}

static size_t
sql_cache_entry_sizeof(struct sql_stmt *stmt)
{
	return sql_stmt_est_size(stmt) + sizeof(struct stmt_cache_entry);
}

static void
sql_cache_entry_delete(struct stmt_cache_entry *entry)
{
	assert(entry->refs == 0);
	assert(! sql_stmt_busy(entry->stmt));
	sql_stmt_finalize(entry->stmt);
	TRASH(entry);
	free(entry);
}

/**
 * Remove statement entry from cache: remove from LRU list and
 * account cache size changes, then release occupied memory.
 * At the moment of calling this function, entry is already
 * removed from hash.
 */
static void
sql_stmt_cache_delete(struct stmt_cache_entry *entry)
{
	if (sql_stmt_cache.last_found == entry)
		sql_stmt_cache.last_found = NULL;
	rlist_del(&entry->link);
	sql_cache_entry_delete(entry);
}

struct stmt_cache_entry *
stmt_cache_find_entry(uint32_t stmt_id)
{
	if (sql_stmt_cache.last_found != NULL &&
	    sql_stmt_cache.last_id == stmt_id)
		return sql_stmt_cache.last_found;

	/* Fallthrough to slow hash search. */
	struct mh_i32ptr_t *hash = sql_stmt_cache.hash;
	mh_int_t stmt = mh_i32ptr_find(hash, stmt_id, NULL);
	if (stmt == mh_end(hash))
		return NULL;
	struct stmt_cache_entry *entry = mh_i32ptr_node(hash, stmt)->val;
	if (entry == NULL)
		return NULL;

	sql_stmt_cache.last_found = entry;
	sql_stmt_cache.last_id = stmt_id;

	return entry;
}

static void
sql_stmt_cache_gc(void)
{
	struct stmt_cache_entry *entry, *next;
	rlist_foreach_entry_safe(entry, &sql_stmt_cache.gc_queue, link, next)
		sql_stmt_cache_delete(entry);
	assert(rlist_empty(&sql_stmt_cache.gc_queue));
}

/**
 * Allocate new cache entry containing given prepared statement.
 * Add it to the LRU cache list. Account cache size enlargement.
 */
static struct stmt_cache_entry *
sql_cache_entry_new(struct sql_stmt *stmt)
{
	struct stmt_cache_entry *entry = malloc(sizeof(*entry));
	if (entry == NULL) {
		diag_set(OutOfMemory, sizeof(*entry), "malloc",
			 "struct stmt_cache_entry");
		return NULL;
	}
	entry->stmt = stmt;
	entry->link = (struct rlist) { NULL, NULL };
	entry->refs = 0;
	return entry;
}

/**
 * Return true if used memory (accounting new entry) for SQL
 * prepared statement cache does not exceed the limit.
 */
static bool
sql_cache_check_new_entry_size(size_t size)
{
	return (sql_stmt_cache.mem_used + size <= sql_stmt_cache.mem_quota);
}

static void
sql_stmt_cache_entry_unref(struct stmt_cache_entry *entry)
{
	assert((int64_t)entry->refs - 1 >= 0);
	if (--entry->refs == 0) {
		/*
		 * Remove entry from hash and add it to gc queue.
		 * Resources are to be released in the nearest
		 * GC cycle (see sql_stmt_cache_insert()).
		 */
		struct sql_stmt_cache *cache = &sql_stmt_cache;
		const char *sql_str = sql_stmt_query_str(entry->stmt);
		uint32_t stmt_id = sql_stmt_calculate_id(sql_str,
							 strlen(sql_str));
		mh_int_t i = mh_i32ptr_find(cache->hash, stmt_id, NULL);
		assert(i != mh_end(cache->hash));
		mh_i32ptr_del(cache->hash, i, NULL);
		rlist_add(&sql_stmt_cache.gc_queue, &entry->link);
		sql_stmt_cache.mem_used -= sql_cache_entry_sizeof(entry->stmt);
		if (sql_stmt_cache.last_found == entry)
			sql_stmt_cache.last_found = NULL;
	}
}

void
sql_session_stmt_hash_erase(struct mh_i32ptr_t *hash)
{
	if (hash == NULL)
		return;
	mh_int_t i;
	struct stmt_cache_entry *entry;
	mh_foreach(hash, i) {
		entry = (struct stmt_cache_entry *)
			mh_i32ptr_node(hash, i)->val;
		sql_stmt_cache_entry_unref(entry);
	}
	mh_i32ptr_delete(hash);
}

int
sql_session_stmt_hash_add_id(struct mh_i32ptr_t *hash, uint32_t stmt_id)
{
	struct stmt_cache_entry *entry = stmt_cache_find_entry(stmt_id);
	const struct mh_i32ptr_node_t id_node = { stmt_id, entry };
	struct mh_i32ptr_node_t *old_node = NULL;
	mh_int_t i = mh_i32ptr_put(hash, &id_node, &old_node, NULL);
	if (i == mh_end(hash)) {
		diag_set(OutOfMemory, 0, "mh_i32ptr_put", "mh_i32ptr_node");
		return -1;
	}
	assert(old_node == NULL);
	entry->refs++;
	return 0;
}

uint32_t
sql_stmt_calculate_id(const char *sql_str, size_t len)
{
	return mh_strn_hash(sql_str, len);
}

void
sql_stmt_unref(uint32_t stmt_id)
{
	struct stmt_cache_entry *entry = stmt_cache_find_entry(stmt_id);
	assert(entry != NULL);
	sql_stmt_cache_entry_unref(entry);
}

int
sql_stmt_cache_update(struct sql_stmt *old_stmt, struct sql_stmt *new_stmt)
{
	const char *sql_str = sql_stmt_query_str(old_stmt);
	uint32_t stmt_id = sql_stmt_calculate_id(sql_str, strlen(sql_str));
	struct stmt_cache_entry *entry = stmt_cache_find_entry(stmt_id);
	sql_stmt_finalize(entry->stmt);
	entry->stmt = new_stmt;
	return 0;
}

int
sql_stmt_cache_insert(struct sql_stmt *stmt)
{
	assert(stmt != NULL);
	struct sql_stmt_cache *cache = &sql_stmt_cache;
	size_t new_entry_size = sql_cache_entry_sizeof(stmt);

	if (! sql_cache_check_new_entry_size(new_entry_size))
		sql_stmt_cache_gc();
	/*
	 * Test memory limit again. Raise an error if it is
	 * still overcrowded.
	 */
	if (! sql_cache_check_new_entry_size(new_entry_size)) {
		diag_set(ClientError, ER_SQL_PREPARE, "Memory limit for SQL "\
			"prepared statements has been reached. Please, deallocate "\
			"active statements or increase SQL cache size.");
		return -1;
	}
	struct mh_i32ptr_t *hash = cache->hash;
	struct stmt_cache_entry *entry = sql_cache_entry_new(stmt);
	if (entry == NULL)
		return -1;
	const char *sql_str = sql_stmt_query_str(stmt);
	uint32_t stmt_id = sql_stmt_calculate_id(sql_str, strlen(sql_str));
	assert(stmt_cache_find_entry(stmt_id) == NULL);
	const struct mh_i32ptr_node_t id_node = { stmt_id, entry };
	struct mh_i32ptr_node_t *old_node = NULL;
	mh_int_t i = mh_i32ptr_put(hash, &id_node, &old_node, NULL);
	if (i == mh_end(hash)) {
		sql_cache_entry_delete(entry);
		diag_set(OutOfMemory, 0, "mh_i32ptr_put", "mh_i32ptr_node");
		return -1;
	}
	assert(old_node == NULL);
	sql_stmt_cache.mem_used += sql_cache_entry_sizeof(stmt);
	return 0;
}

struct sql_stmt *
sql_stmt_cache_find(uint32_t stmt_id)
{
	struct stmt_cache_entry *entry = stmt_cache_find_entry(stmt_id);
	if (entry == NULL)
		return NULL;
	return entry->stmt;
}

int
sql_stmt_cache_set_size(size_t size)
{
	if (sql_stmt_cache.mem_used > size)
		sql_stmt_cache_gc();
	if (sql_stmt_cache.mem_used > size) {
		diag_set(ClientError, ER_SQL_PREPARE, "Can't reduce memory "\
			 "limit for SQL prepared statements: please, deallocate "\
			 "active statements");
		return -1;
	}
	sql_stmt_cache.mem_quota = size;
	return 0;
}
