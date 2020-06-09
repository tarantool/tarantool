/*
 * Copyright 2010-2020, Tarantool AUTHORS, please see AUTHORS file.
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
#include "memtx_tx.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include "txn.h"

struct tx_manager
{
	/**
	 * List of all transactions that are in a read view.
	 * New transactions are added to the tail of this list,
	 * so the list is ordered by rv_psn.
	 */
	struct rlist read_view_txs;
};

/** That's a definition, see declaration for description. */
bool memtx_tx_manager_use_mvcc_engine = false;

/** The one and only instance of tx_manager. */
static struct tx_manager txm;

void
memtx_tx_manager_init()
{
	rlist_create(&txm.read_view_txs);
}

void
memtx_tx_manager_free()
{
}

int
memtx_tx_cause_conflict(struct txn *breaker, struct txn *victim)
{
	struct tx_conflict_tracker *tracker = NULL;
	struct rlist *r1 = breaker->conflict_list.next;
	struct rlist *r2 = victim->conflicted_by_list.next;
	while (r1 != &breaker->conflict_list &&
	       r2 != &victim->conflicted_by_list) {
		tracker = rlist_entry(r1, struct tx_conflict_tracker,
		in_conflict_list);
		assert(tracker->breaker == breaker);
		if (tracker->victim == victim)
			break;
		tracker = rlist_entry(r2, struct tx_conflict_tracker,
		in_conflicted_by_list);
		assert(tracker->victim == victim);
		if (tracker->breaker == breaker)
			break;
		tracker = NULL;
		r1 = r1->next;
		r2 = r2->next;
	}
	if (tracker != NULL) {
		/*
		 * Move to the beginning of a list
		 * for a case of subsequent lookups.
		 */
		rlist_del(&tracker->in_conflict_list);
		rlist_del(&tracker->in_conflicted_by_list);
	} else {
		size_t size;
		tracker = region_alloc_object(&victim->region,
		struct tx_conflict_tracker,
		&size);
		if (tracker == NULL) {
			diag_set(OutOfMemory, size, "tx region",
				 "conflict_tracker");
			return -1;
		}
		tracker->breaker = breaker;
		tracker->victim = victim;
	}
	rlist_add(&breaker->conflict_list, &tracker->in_conflict_list);
	rlist_add(&victim->conflicted_by_list, &tracker->in_conflicted_by_list);
	return 0;
}

void
memtx_tx_handle_conflict(struct txn *breaker, struct txn *victim)
{
	assert(breaker->psn != 0);
	if (victim->status != TXN_INPROGRESS) {
		/* Was conflicted by somebody else. */
		return;
	}
	if (stailq_empty(&victim->stmts)) {
		/* Send to read view. */
		victim->status = TXN_IN_READ_VIEW;
		victim->rv_psn = breaker->psn;
		rlist_add_tail(&txm.read_view_txs, &victim->in_read_view_txs);
	} else {
		/* Mark as conflicted. */
		victim->status = TXN_CONFLICTED;
	}
}
