/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2023, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include <stdint.h>
#include "salad/stailq.h"

#ifdef __cplusplus
extern "C" {
#endif

/** ID of a transactional event. */
enum txn_event_id {
	TXN_EVENT_BEFORE_COMMIT = 0,
	TXN_EVENT_ON_COMMIT = 1,
	TXN_EVENT_ON_ROLLBACK = 2,
	txn_event_id_MAX
};

/**
 * How many spaces modified by the transaction have registered triggers for the
 * given event.
 */
enum txn_event_mode {
	/** There are no spaces with triggers. */
	TXN_EVENT_NO_TRIGGERS,
	/** One space has triggers. */
	TXN_EVENT_ONE_SPACE,
	/** Several spaces with triggers. */
	TXN_EVENT_MULTIPLE_SPACES
};

/**
 * Event triggers, registered for the spaces that are modified by the given
 * transaction. This structure is used to optimize the case when there is only
 * one space in the transaction, which has triggers registered for this event.
 * If there are more such spaces, a loop over all txn statements is required to
 * run the triggers (see run_triggers_for_txn_event()).
 */
struct txn_event {
	/** How many spaces have registered triggers. */
	enum txn_event_mode mode;
	/** ID of the space for TXN_EVENT_ONE_SPACE mode. */
	uint32_t space_id;
	/** Cached space event for TXN_EVENT_ONE_SPACE mode. */
	struct space_event *space_event;
};

struct space;
struct txn;
struct txn_savepoint;

/** Initialize the "txn event trigger" subsystem. */
void
txn_event_trigger_init(void);

/** Destroy the "txn event trigger" subsystem. */
void
txn_event_trigger_free(void);

/** Initialize the `txn_event' structure. */
void
txn_event_init(struct txn_event *txn_event);

/**
 * Save event `txn_event_id' from the `space' in `txn' events.
 * For details see the description of the `txn_event' structure.
 */
void
txn_event_add_space(struct txn *txn, struct space *space, int txn_event_id);

/** Run `box.before_commit' event triggers. */
int
txn_event_before_commit_run_triggers(struct txn *txn);

/** Run `box.on_commit' event triggers. */
int
txn_event_on_commit_run_triggers(struct txn *txn);

/**
 * Run `box.on_rollback' event triggers on transaction rollback.
 * The list of statements in `txn' is expected to be reversed.
 */
int
txn_event_on_rollback_run_triggers(struct txn *txn);

/**
 * Run `box.on_rollback' event triggers on rollback to savepoint.
 * The list of statements `stmts' is expected to be reversed.
 */
int
txn_event_on_rollback_to_svp_run_triggers(struct txn *txn,
					  struct stailq *stmts);

#ifdef __cplusplus
} /* extern "C" */
#endif
