#pragma once
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

#include <stdbool.h>

#include "small/rlist.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

/**
 * Global flag that enables mvcc engine.
 * If set, memtx starts to apply statements through txm history mechanism
 * and tx manager itself transaction reads in order to detect conflicts.
 */
extern bool memtx_tx_manager_use_mvcc_engine;

/**
 * Record that links two transactions, breaker and victim.
 * See memtx_tx_cause_conflict for details.
 */
struct tx_conflict_tracker {
	/** TX that aborts victim on commit. */
	struct txn *breaker;
	/** TX that will be aborted on breaker's commit. */
	struct txn *victim;
	/** Link in breaker->conflict_list. */
	struct rlist in_conflict_list;
	/** Link in victim->conflicted_by_list. */
	struct rlist in_conflicted_by_list;
};

/**
 * Initialize memtx transaction manager.
 */
void
memtx_tx_manager_init();

/**
 * Free resources of memtx transaction manager.
 */
void
memtx_tx_manager_free();

/**
 * Notify TX manager that if transaction @a breaker is committed then the
 * transaction @a victim must be aborted due to conflict. It is achieved
 * by adding corresponding entry (of tx_conflict_tracker type) to @a breaker
 * conflict list. In case there's already such entry, then move it to the head
 * of the list in order to optimize next invocations of this function.
 * For example: there's two rw transaction in progress, one have read
 * some value while the second is about to overwrite it. If the second
 * is committed first, the first must be aborted.
 * @return 0 on success, -1 on memory error.
 */
int
memtx_tx_cause_conflict(struct txn *breaker, struct txn *victim);

/**
 * Handle conflict when @a breaker transaction is prepared.
 * The conflict is happened if @a victim have read something that @a breaker
 * overwrites.
 * If @a victim is read-only or hasn't made any changes, it should be sent
 * to read view, in which is will not see @a breaker.
 * Otherwise @a victim must be marked as conflicted and aborted on occasion.
 */
void
memtx_tx_handle_conflict(struct txn *breaker, struct txn *victim);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */
