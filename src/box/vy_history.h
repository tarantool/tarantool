#ifndef INCLUDES_TARANTOOL_BOX_VY_HISTORY_H
#define INCLUDES_TARANTOOL_BOX_VY_HISTORY_H
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
 * THIS SOFTWARE IS PROVIDED BY AUTHORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * AUTHORS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <assert.h>
#include <stdbool.h>
#include <small/rlist.h>

#include "iproto_constants.h"
#include "vy_stmt.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct mempool;

/** Key history. */
struct vy_history {
	/**
	 * List of statements sorted by LSN in descending order.
	 * Linked by vy_history_node::link.
	 */
	struct rlist stmts;
	/** Memory pool for vy_history_node allocations. */
	struct mempool *pool;
};

/** Key history node. */
struct vy_history_node {
	/** Link in a history list. */
	struct rlist link;
	/** History statement. Always referenced. */
	struct vy_entry entry;
};

/**
 * Initialize a history list. The 'pool' argument specifies
 * the memory pool to use for node allocations.
 */
static inline void
vy_history_create(struct vy_history *history, struct mempool *pool)
{
	history->pool = pool;
	rlist_create(&history->stmts);
}

/**
 * Return true if the history of a key contains terminal node in the end,
 * i.e. REPLACE of DELETE statement.
 */
static inline bool
vy_history_is_terminal(struct vy_history *history)
{
	if (rlist_empty(&history->stmts))
		return false;
	struct vy_history_node *node = rlist_last_entry(&history->stmts,
					struct vy_history_node, link);
	assert(vy_stmt_type(node->entry.stmt) == IPROTO_REPLACE ||
	       vy_stmt_type(node->entry.stmt) == IPROTO_DELETE ||
	       vy_stmt_type(node->entry.stmt) == IPROTO_INSERT ||
	       vy_stmt_type(node->entry.stmt) == IPROTO_UPSERT);
	return vy_stmt_type(node->entry.stmt) != IPROTO_UPSERT;
}

/**
 * Return the last (newest, having max LSN) statement of the given
 * key history or NULL if the history is empty.
 */
static inline struct vy_entry
vy_history_last_stmt(struct vy_history *history)
{
	if (rlist_empty(&history->stmts))
		return vy_entry_none();
	/* Newest statement is at the head of the list. */
	struct vy_history_node *node = rlist_first_entry(&history->stmts,
					struct vy_history_node, link);
	return node->entry;
}

/**
 * Append all statements of history @src to history @dst.
 */
static inline void
vy_history_splice(struct vy_history *dst, struct vy_history *src)
{
	assert(dst->pool == src->pool);
	rlist_splice_tail(&dst->stmts, &src->stmts);
}

/**
 * Append an (older) statement to a history list.
 * Returns 0 on success, -1 on memory allocation error.
 */
int
vy_history_append_stmt(struct vy_history *history, struct vy_entry entry);

/**
 * Release all statements stored in the given history and
 * reinitialize the history list.
 */
void
vy_history_cleanup(struct vy_history *history);

/**
 * Get a resultant statement from collected history.
 * If the resultant statement is a DELETE, the function
 * will return NULL unless @keep_delete flag is set.
 */
int
vy_history_apply(struct vy_history *history, struct key_def *cmp_def,
		 bool keep_delete, int *upserts_applied, struct vy_entry *ret);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* INCLUDES_TARANTOOL_BOX_VY_HISTORY_H */
