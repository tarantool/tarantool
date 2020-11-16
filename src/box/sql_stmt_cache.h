#ifndef INCLUDES_PREP_STMT_H
#define INCLUDES_PREP_STMT_H
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
#include <stdint.h>
#include <stdio.h>

#include "sql_ast.h"
#include "small/rlist.h"

#if defined(__cplusplus)
extern "C" {
#endif

struct sql_stmt;
struct mh_i64ptr_t;
struct info_handler;

struct stmt_cache_entry {
	/** Prepared statement itself. */
	struct sql_stmt *stmt;
	/**
	 * Link to the next entry. All statements are to be
	 * evicted on the next gc cycle.
	 */
	struct rlist link;
	/**
	 * Reference counter. If it is == 0, entry gets
	 * into GC queue.
	 */
	uint32_t refs;
};

/**
 * Global prepared statements holder.
 */
struct sql_stmt_cache {
	/** Size of memory currently occupied by prepared statements. */
	size_t mem_used;
	/** Max memory size that can be used for cache. */
	size_t mem_quota;
	/** Query id -> struct stmt_cahce_entry hash.*/
	struct mh_i32ptr_t *hash;
	/**
	 * After deallocation statements are not deleted, but
	 * moved to this list. GC process is triggered only
	 * when memory limit has reached. It allows to reduce
	 * workload on session's disconnect.
	 */
	struct rlist gc_queue;
	/**
	 * Last result of sql_stmt_cache_find() invocation.
	 * Since during processing prepared statement it
	 * may require to find the same statement several
	 * times.
	 */
	struct stmt_cache_entry *last_found;
	/**
	 * saved hash id for the last_found entry
	 */
	uint32_t last_id; // FIXME
};

/**
 * Initialize global cache for prepared statements. Called once
 * during database setup (in sql_init()).
 */
void
sql_stmt_cache_init(void);

/**
 * Store statistics concerning cache (current size and number
 * of statements in it) into info handler @h.
 */
void
sql_stmt_cache_stat(struct info_handler *h);

/**
 * Erase session local hash: unref statements belong to this
 * session and deallocate hash itself.
 * @hash is assumed to be member of struct session @sql_stmts.
 */
void
sql_session_stmt_hash_erase(struct mh_i32ptr_t *hash);

/**
 * Add entry corresponding to prepared statement with given ID
 * to session-local hash and increase its ref counter.
 * @hash is assumed to be member of struct session @sql_stmts.
 */
int
sql_session_stmt_hash_add_id(struct mh_i32ptr_t *hash, uint32_t stmt_id);

/**
 * Prepared statement ID is supposed to be hash value
 * of the original SQL query string.
 */
uint32_t
sql_stmt_calculate_id(const char *sql_str, size_t len);

/** Unref prepared statement entry in global holder. */
void
sql_stmt_unref(uint32_t stmt_id);

int
sql_stmt_cache_update(struct sql_stmt *old_stmt, struct sql_stmt *new_stmt);

/**
 * Save prepared statement to the prepared statement cache.
 * Account cache size change. If the cache is full (i.e. memory
 * quota is exceeded) diag error is raised. In case of success
 * return id of prepared statement via output parameter @id.
 */
int
sql_stmt_cache_insert(struct sql_stmt *stmt);

/** Find entry by SQL string. In case of search fails it returns NULL. */
struct sql_stmt *
sql_stmt_cache_find(uint32_t stmt_id);

struct stmt_cache_entry *
stmt_cache_find_entry(uint32_t stmt_id);

/** Set prepared cache size limit. */
int
sql_stmt_cache_set_size(size_t size);

#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif
