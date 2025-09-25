#pragma once
/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2025, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "journal.h"
#include "xrow.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

/** Data collected precisely when all the prepared txns are committed. */
struct box_checkpoint {
	/**
	 * Full descriptor of the journal collected exactly when the last
	 * prepared transaction was written into the journal.
	 */
	struct journal_checkpoint journal;
	/**
	 * Full descriptor of the Raft state machine collected exactly when the
	 * last known synchronous txn was confirmed.
	 */
	struct raft_request raft_remote;
	/**
	 * Remote and local Raft checkpoints are intended for different things
	 * and have slightly different data.
	 */
	struct raft_request raft_local;
	/**
	 * Full descriptor of the txn limbo collected exactly when the last
	 * known synchronous txn was confirmed.
	 */
	struct synchro_request limbo;
};

/**
 * Wait until all the currently prepared txns are committed and collect all the
 * global transaction-related data at this exact moment. This function has no
 * after-effects on the instance and can even be executed by multiple fibers in
 * parallel.
 */
int
box_checkpoint_build_in_memory(struct box_checkpoint *out);

/**
 * Create the in-memory checkpoint + make it visible on disk as well. This
 * splits the journal into "before" and "after", reflected in the xlog files.
 * The output is a snapshot file which will be used for the future recovery.
 *
 * Only one on-disk checkpoint can be in progress.
 */
int
box_checkpoint_build_on_disk(struct box_checkpoint *out, bool is_scheduled);

/**
 * Extract the checkpoint data from the snapshot having exactly the provided
 * vclock.
 */
int
box_checkpoint_build_from_snapshot(struct box_checkpoint *out,
				   const struct vclock *vclock);

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
