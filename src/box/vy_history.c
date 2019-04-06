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
#include "vy_history.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <small/mempool.h>
#include <small/rlist.h>

#include "diag.h"
#include "tuple.h"
#include "iproto_constants.h"
#include "vy_stmt.h"
#include "vy_upsert.h"

int
vy_history_append_stmt(struct vy_history *history, struct vy_entry entry)
{
	assert(history->pool->objsize == sizeof(struct vy_history_node));
	struct vy_history_node *node = mempool_alloc(history->pool);
	if (node == NULL) {
		diag_set(OutOfMemory, sizeof(*node), "mempool",
			 "struct vy_history_node");
		return -1;
	}
	node->is_refable = vy_stmt_is_refable(entry.stmt);
	if (node->is_refable)
		tuple_ref(entry.stmt);
	node->entry = entry;
	rlist_add_tail_entry(&history->stmts, node, link);
	return 0;
}

void
vy_history_cleanup(struct vy_history *history)
{
	struct vy_history_node *node, *tmp;
	rlist_foreach_entry_safe(node, &history->stmts, link, tmp) {
		if (node->is_refable)
			tuple_unref(node->entry.stmt);
		mempool_free(history->pool, node);
	}
	rlist_create(&history->stmts);
}

int
vy_history_apply(struct vy_history *history, struct key_def *cmp_def,
		 bool keep_delete, int *upserts_applied, struct vy_entry *ret)
{
	*ret = vy_entry_none();
	*upserts_applied = 0;
	if (rlist_empty(&history->stmts))
		return 0;

	struct vy_entry curr = vy_entry_none();
	struct vy_history_node *node = rlist_last_entry(&history->stmts,
					struct vy_history_node, link);
	if (vy_history_is_terminal(history)) {
		if (!keep_delete &&
		    vy_stmt_type(node->entry.stmt) == IPROTO_DELETE) {
			/*
			 * Ignore terminal delete unless the caller
			 * explicitly asked to keep it.
			 */
		} else if (!node->is_refable) {
			curr.hint = node->entry.hint;
			curr.stmt = vy_stmt_dup(node->entry.stmt);
			if (curr.stmt == NULL)
				return -1;
		} else {
			curr = node->entry;
			tuple_ref(curr.stmt);
		}
		node = rlist_prev_entry_safe(node, &history->stmts, link);
	}
	while (node != NULL) {
		struct vy_entry entry = vy_entry_apply_upsert(node->entry, curr,
							      cmp_def, true);
		++*upserts_applied;
		if (curr.stmt != NULL)
			tuple_unref(curr.stmt);
		if (entry.stmt == NULL)
			return -1;
		curr = entry;
		node = rlist_prev_entry_safe(node, &history->stmts, link);
	}
	*ret = curr;
	return 0;
}
