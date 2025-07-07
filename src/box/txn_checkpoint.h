#pragma once
/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2025, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "xrow.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

/** Data collected precisely when all the prepared txns are committed. */
struct txn_checkpoint {
	/**
	 * VClock taken when the last known prepared txn was written to WAL. For
	 * non-synchronous txns it is equal to their commit moment.
	 */
	struct vclock journal_vclock;
	/**
	 * Full descriptor of the Raft state machine collected exactly when the
	 * last known synchronous txn was confirmed.
	 */
	struct raft_request raft_remote_checkpoint;
	/**
	 * Full descriptor of the txn limbo collected exactly when the last
	 * known synchronous txn was confirmed.
	 */
	struct synchro_request limbo_checkpoint;
	/** VClock of the limbo's state. */
	struct vclock limbo_vclock;
};

/**
 * Wait until all the currently prepared txns are committed and collect all the
 * global transaction-related data at this exact moment.
 */
int
txn_checkpoint_build(struct txn_checkpoint *out);

/**
 * Wait until all the prepared txns have been successfully written to the
 * journal. However there is not guarantee that they are going to be committed.
 * For synchronous txns just a journal write isn't enough.
 */
int
txn_persist_all_prepared(struct vclock *out);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */
