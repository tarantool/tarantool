/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2023, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "box/port.h"
#include "core/assoc.h"
#include "core/func_adapter.h"
#include "small/mempool.h"
#include "txn.h"

/** Global events, i.e. triggered by all transactions. */
struct event *txn_global_events[txn_event_id_MAX];

/**
 * Data used to create txn iterators.
 */
struct txn_iterator_data {
	/** First statement of the transaction. */
	struct txn_stmt *first_stmt;
	/**
	 * Iterate only over statements with the given space id.
	 * Or set to SPACE_ID_FILTER_ALL_SPACES for all spaces.
	 */
	int64_t space_id_filter;
};

/**
 * Iterator over transaction statements used in triggers.
 */
struct txn_port_c_iterator {
	/** Iterator next. */
	port_c_iterator_next_f next;
	/** Saved txn id. Is used to invalidate iterator. */
	uint32_t txn_id;
	/** Request number, starting from 1. */
	uint32_t req_num;
	/** Current statement of the transaction. */
	struct txn_stmt *stmt;
	/**
	 * Iterate only over statements with the given space id.
	 * Or set to SPACE_ID_FILTER_ALL_SPACES for all spaces.
	 */
	int64_t space_id_filter;
};

static_assert(
	sizeof(struct txn_port_c_iterator) <= sizeof(struct port_c_iterator),
	"The implementation should fit into abstract instance");

void
txn_event_trigger_init(void)
{
	/* Initialize global events. */
	static const char * const event_names[] = {
		"box.before_commit",
		"box.on_commit",
		"box.on_rollback",
	};
	static_assert(lengthof(txn_global_events) == lengthof(event_names),
		      "Every event must be covered with name");

	for (size_t i = 0; i < lengthof(txn_global_events); i++) {
		txn_global_events[i] = event_get(event_names[i], true);
		event_ref(txn_global_events[i]);
	}
}

void
txn_event_trigger_free(void)
{
	/* Destroy global events. */
	for (size_t i = 0; i < lengthof(txn_global_events); i++) {
		event_unref(txn_global_events[i]);
		txn_global_events[i] = NULL;
	}
}

void
txn_event_init(struct txn_event *txn_event)
{
	txn_event->mode = TXN_EVENT_NO_TRIGGERS;
	txn_event->space_id = 0;
	txn_event->space_event = NULL;
}

void
txn_event_add_space(struct txn *txn, struct space *space, int event_id)
{
	assert(space != NULL);
	assert(event_id >= 0 && event_id < txn_event_id_MAX);
	struct space_event *space_event = &space->txn_events[event_id];
	if (likely(!space_event_has_triggers(space_event)))
		return;

	struct txn_event *txn_event = &txn->txn_events[event_id];
	switch (txn_event->mode) {
	case TXN_EVENT_NO_TRIGGERS:
		txn_event->mode = TXN_EVENT_ONE_SPACE;
		txn_event->space_id = space_id(space);
		txn_event->space_event = space_event;
		break;
	case TXN_EVENT_ONE_SPACE:
		if (txn_event->space_id != space_id(space)) {
			txn_event->mode = TXN_EVENT_MULTIPLE_SPACES;
			txn_event->space_id = 0;
			txn_event->space_event = NULL;
		}
		break;
	case TXN_EVENT_MULTIPLE_SPACES:
		break;
	default:
		unreachable();
	}
}

enum { SPACE_ID_FILTER_ALL_SPACES = -1 };

/**
 * The iterator goes through every statement of the transaction.
 * Before accessing statements, iterator checks if it in the same transaction.
 * If the check fails, an error is returned.
 */
static int
txn_iterator_next(struct port_c_iterator *it, struct port *out, bool *is_eof)
{
	struct txn_port_c_iterator *txn_it =
		(struct txn_port_c_iterator *)it;
	struct txn_stmt *stmt = txn_it->stmt;

	struct txn *txn = in_txn();
	if (txn == NULL || txn->id != txn_it->txn_id) {
		diag_set(ClientError, ER_CURSOR_NO_TRANSACTION);
		return -1;
	}

	/* Skip unwanted spaces if space id filter is set. */
	if (txn_it->space_id_filter != SPACE_ID_FILTER_ALL_SPACES) {
		while (stmt != NULL &&
		       space_id(stmt->space) != txn_it->space_id_filter) {
			stmt = stailq_next_entry(stmt, next);
		}
	}

	if (stmt == NULL) {
		*is_eof = true;
		return 0;
	}

	*is_eof = false;
	port_c_create(out);
	/*
	 * The iterator returns 4 values:
	 *  1. An ordinal request number;
	 *  2. The old value of the tuple;
	 *  3. The new value of the tuple;
	 *  4. The ID of the space.
	 */
	port_c_add_number(out, txn_it->req_num++);
	if (stmt->old_tuple != NULL)
		port_c_add_tuple(out, stmt->old_tuple);
	else
		port_c_add_null(out);
	if (stmt->new_tuple != NULL)
		port_c_add_tuple(out, stmt->new_tuple);
	else
		port_c_add_null(out);
	port_c_add_number(out, space_id(stmt->space));

	txn_it->stmt = stailq_next_entry(stmt, next);
	return 0;
}

static void
txn_iterator_create(void *base_data, struct port_c_iterator *it)
{
	struct txn_iterator_data *data = (struct txn_iterator_data *)base_data;
	struct txn_port_c_iterator *txn_it =
		(struct txn_port_c_iterator *)it;
	txn_it->req_num = 1;
	txn_it->stmt = data->first_stmt;
	txn_it->space_id_filter = data->space_id_filter;
	struct txn *txn = in_txn();
	assert(txn != NULL);
	txn_it->txn_id = txn->id;
	txn_it->next = txn_iterator_next;
}

/**
 * Run triggers registered in `txn' for the `event'.
 * `stmt' points to the first statement of a transaction or a savepoint.
 * `can_abort' is set to true if the trigger can abort the transaction.
 * `space_id' is passed to the iterator to filter spaces by the ID.
 */
static int
run_triggers_general(struct txn *txn, struct txn_stmt *stmt,
		     struct event *event, bool can_abort, int64_t space_id)
{
	int rc = 0;
	const char *name = NULL;
	struct func_adapter *trigger = NULL;
	struct event_trigger_iterator it;
	event_trigger_iterator_create(&it, event);

	struct txn_iterator_data data = {
		.first_stmt = stmt,
		.space_id_filter = space_id,
	};

	/*
	 * The trigger-functions has one parameter - an iterator over the
	 * statements of the transaction.
	 */
	struct port args;
	port_c_create(&args);
	port_c_add_iterable(&args, &data, txn_iterator_create);

	while (rc == 0 && event_trigger_iterator_next(&it, &trigger, &name)) {
		if (can_abort) {
			/*
			 * The transaction could be aborted while the previous
			 * trigger was running (e.g. if the trigger-function
			 * yielded or failed).
			 */
			rc = txn_check_can_continue(txn);
			if (rc != 0)
				break;
		}
		rc = func_adapter_call(trigger, &args, NULL);
	}
	port_destroy(&args);
	event_trigger_iterator_destroy(&it);
	return rc;
}

/**
 * Run triggers registered in `txn' for the `space_event'.
 * Each space event includes 2 lists of triggers: bound by id, bound by name.
 * `stmt' points to the first statement of a transaction or a savepoint.
 * `can_abort' is set to true if the trigger can abort the transaction.
 * `space_id' is passed to the iterator to filter spaces by the ID.
 */
static int
run_triggers_of_single_space(struct txn *txn, struct txn_stmt *stmt,
			     struct space_event *space_event, bool can_abort,
			     int64_t space_id)
{
	struct event *events[] = {
		space_event->by_id,
		space_event->by_name,
	};
	/*
	 * Since the triggers can yield (even if it is not allowed), a space
	 * can be dropped while executing one of the trigger lists and all the
	 * events will be deleted - reference them to prevent use-after-free.
	 */
	for (size_t i = 0; i < lengthof(events); i++)
		event_ref(events[i]);
	int rc = 0;
	for (size_t i = 0; i < lengthof(events) && rc == 0; i++) {
		rc = run_triggers_general(txn, stmt, events[i], can_abort,
					  space_id);
	}
	for (size_t i = 0; i < lengthof(events); i++)
		event_unref(events[i]);
	return rc;
}

/**
 * Run triggers registered in `txn' for the `event_id'.
 * Used in the case when there is more than one space touched by `txn', which
 * have triggers for the `event_id' event.
 * `stmt' points to the first statement of a transaction or a savepoint.
 * `can_abort' is set to true if the trigger can abort the transaction.
 */
static int
run_triggers_of_multi_spaces(struct txn *txn, struct txn_stmt *stmt,
			     int event_id, bool can_abort)
{
	/*
	 * Collect all spaces with triggers into the map.
	 */
	struct txn_stmt *first_stmt = stmt;
	struct mh_i32ptr_t *spaces = mh_i32ptr_new();
	while (stmt != NULL) {
		struct space_event *e = &stmt->space->txn_events[event_id];
		if (space_event_has_triggers(e)) {
			struct mh_i32ptr_node_t node = {
				space_id(stmt->space),
				stmt->space
			};
			mh_i32ptr_put(spaces, &node, NULL, NULL);
		}
		stmt = stailq_next_entry(stmt, next);
	}
	/*
	 * Run triggers for spaces from the map.
	 */
	int rc = 0;
	mh_int_t i;
	mh_foreach(spaces, i) {
		uint32_t space_id = mh_i32ptr_node(spaces, i)->key;
		struct space *space = mh_i32ptr_node(spaces, i)->val;
		struct space_event *event = &space->txn_events[event_id];
		rc = run_triggers_of_single_space(txn, first_stmt, event,
						  can_abort, space_id);
		if (rc != 0)
			goto out;
	}
out:
	mh_i32ptr_delete(spaces);
	return rc;
}

/**
 * Run triggers, set for the `event_id' on spaces that are modified by the
 * transaction `txn'.
 * `stmt' points to the first statement of a transaction or a savepoint.
 * `can_abort' is set to true if the trigger can abort the transaction.
 */
static int
run_triggers_of_spaces(struct txn *txn, struct txn_stmt *stmt, int event_id,
		       bool can_abort)
{
	assert(event_id >= 0 && event_id < txn_event_id_MAX);
	struct txn_event *txn_event = &txn->txn_events[event_id];

	if (txn_event->mode == TXN_EVENT_ONE_SPACE) {
		if (run_triggers_of_single_space(
				txn, stmt, txn_event->space_event, can_abort,
				txn_event->space_id) != 0)
			return -1;
	} else if (txn_event->mode == TXN_EVENT_MULTIPLE_SPACES) {
		if (run_triggers_of_multi_spaces(
				txn, stmt, event_id, can_abort) != 0)
			return -1;
	}
	return 0;
}

/**
 * Run all `event_id' triggers: global and registered in the `txn'.
 * `stmt' points to the first statement of a transaction or a savepoint.
 * `can_abort' is set to true if the trigger can abort the transaction.
 */
static int
run_triggers(struct txn *txn, struct txn_stmt *stmt, int event_id,
	     bool can_abort)
{
	/*
	 * Run triggers, set on spaces that are modified by the transaction.
	 */
	if (run_triggers_of_spaces(txn, stmt, event_id, can_abort) != 0)
		return -1;
	/*
	 * Run global triggers.
	 */
	struct event *event = txn_global_events[event_id];
	return run_triggers_general(txn, stmt, event, can_abort,
				    SPACE_ID_FILTER_ALL_SPACES);
}

int
txn_event_before_commit_run_triggers(struct txn *txn)
{
	struct txn_stmt *stmt = txn_first_stmt(txn);
	return run_triggers(txn, stmt, TXN_EVENT_BEFORE_COMMIT, true);
}

int
txn_event_on_commit_run_triggers(struct txn *txn)
{
	struct txn_stmt *stmt = txn_first_stmt(txn);
	return run_triggers(txn, stmt, TXN_EVENT_ON_COMMIT, false);
}

int
txn_event_on_rollback_run_triggers(struct txn *txn)
{
	struct txn_stmt *stmt = txn_first_stmt(txn);
	return run_triggers(txn, stmt, TXN_EVENT_ON_ROLLBACK, false);
}

int
txn_event_on_rollback_to_svp_run_triggers(struct txn *txn,
					  struct stailq *stmts)
{
	struct txn_stmt *stmt;
	stmt = stailq_first_entry(stmts, struct txn_stmt, next);
	return run_triggers(txn, stmt, TXN_EVENT_ON_ROLLBACK, false);
}
