/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2021, Tarantool AUTHORS, please see AUTHORS file.
 */

#include "box.h"
#include "cfg.h"
#include "func.h"
#include "iproto_constants.h"
#include "replication.h"
#include "space.h"
#include "tuple.h"
#include "txn.h"
#include "port.h"
#include "schema.h"
#include "space_upgrade.h"
#include "memtx_space.h"
#include "xrow.h"

const char *upgrade_status_strs[] = {
	"inprogress",
	"error",
	"test"
};

enum {
	/**
	 * Batch tuples to be upgraded in order to reduce number of yields.
	 */
#if defined(NDEBUG)
	UPGRADE_TX_BATCH_SIZE = 1024,
#else
	UPGRADE_TX_BATCH_SIZE = 10
#endif
};

/** Mode which was set on instance before launching upgrade. */
static bool was_ro = false;

static void
space_upgrade_change_ro(struct space_upgrade *upgrade, bool is_ro)
{
	cfg_setb("read_only", is_ro);
	box_set_ro();
	char str[UUID_STR_LEN + 1];
	tt_uuid_to_string(&upgrade->host_uuid, str);
	say_warn("Set read_only mode to %d during upgrade on replica %s",
		 is_ro, str);
	assert(box_is_ro() == is_ro);
}

/** Return true if _space_upgrade has any data in it. */
static bool
space_upgrade_has_more_than_one()
{
	struct space *_space_upgrade = space_by_id(BOX_SPACE_UPGRADE_ID);
	assert(_space_upgrade != NULL);
	struct index *pk = space_index(_space_upgrade, 0);
	assert(pk != NULL);
	return index_count(pk, ITER_ALL, NULL, 0) > 1;
}

void
space_upgrade_set_ro(struct space_upgrade *upgrade)
{
	/* Don't change read_only mode in case it's host. */
	if (tt_uuid_is_equal(&upgrade->host_uuid, &INSTANCE_UUID))
		return;
	/* Don't change read_only mode in case we are in TEST or ERROR mode. */
	if (upgrade->status != SPACE_UPGRADE_INPROGRESS)
		return;
	/*
	 * In case it is first entry in _space_upgrade - save current read-only
	 * status to restore it after all upgrades are finished.
	 */
	if (! space_upgrade_has_more_than_one()) {
		was_ro = box_is_ro();
	}
	space_upgrade_change_ro(upgrade, true);
}

void
space_upgrade_reset_ro(struct space_upgrade *upgrade)
{
	/* Don't change read_only mode in case it's host. */
	if (tt_uuid_is_equal(&upgrade->host_uuid, &INSTANCE_UUID))
		return;
	/* Don't change read_only mode in case we are in TEST or ERROR mode. */
	if (upgrade->status != SPACE_UPGRADE_INPROGRESS)
		return;
	/*
	 * In case we are going to remove last entry from _space_upgrade,
	 * we should restore read-only mode.
	 */
	if (! space_upgrade_has_more_than_one()) {
		assert(box_is_ro());
		if (! was_ro)
			space_upgrade_change_ro(upgrade, false);
	}
}

void
space_upgrade_delete(struct space_upgrade *upgrade)
{
	if (upgrade->format != NULL) {
		assert(upgrade->format->refs == 1);
		tuple_format_unref(upgrade->format);
	}
	free(upgrade);
}

/**
 * Function is lightweight variation of box_process_rw():
 * it fills-in ephemeral REPLACE request (it is required to process data
 * to WAL), begins/commits statement, creates new tuple from @a new_tuple
 * msgpack and replaces it to the all indexes.
 */
static int
tuple_upgrade(struct space *space, struct tuple *old_tuple,
	      const char *new_tuple, const char *new_tuple_end)
{
	assert(space_is_memtx(space));
	struct txn *txn = in_txn();
	assert(txn != NULL);
	struct request request;
	memset(&request, 0, sizeof(request));
	request.type = IPROTO_REPLACE;
	request.space_id = space->def->id;
	request.tuple = new_tuple;
	request.tuple_end = new_tuple_end;
	size_t used = region_used(&fiber()->gc);
	if (txn_begin_stmt(txn, space, request.type) != 0)
		goto rollback;
	struct memtx_space *memtx_space = (struct memtx_space *) space;
	struct tuple *tuple = tuple_new(space->format, new_tuple, new_tuple_end);
	if (tuple == NULL) {
		txn_rollback_stmt(txn);
		goto rollback;
	}
	struct tuple *result;
	/* Set mode exactly to DUP_REPLACE to avoid PK modifications. */
	if (memtx_space->replace(space, old_tuple, tuple,
				 DUP_REPLACE, &result) != 0) {
		txn_rollback_stmt(txn);
		goto rollback;
	}
	if (txn_commit_stmt(txn, &request) != 0)
		goto rollback;
	return 0;
rollback:
	txn_abort(txn);
	region_truncate(&fiber()->gc, used);
	return -1;
}

/**
 * Invoke @a func with @a old_tuple as an argument. Function is expected to
 * return transformed tuple in the format of lua array.
 */
static int
upgrade_function_apply(struct space *space, struct func *func,
		       struct tuple *old_tuple, const char **new_data_begin,
		       const char **new_data_end)
{
	assert(old_tuple != NULL);
	assert(space->upgrade->func == func);
	*new_data_begin = NULL;
	*new_data_end = NULL;
	struct port out_port, in_port;
	port_c_create(&in_port);
	port_c_add_tuple(&in_port, old_tuple);
	int rc = func_call(func, &in_port, &out_port);
	port_destroy(&in_port);
	if (rc != 0) {
		diag_set(ClientError, ER_UPGRADE, space_name(space),
			 tt_sprintf("upgrade function has failed: %s",
				    diag_last_error(diag_get())->errmsg));
		port_destroy(&out_port);
		return -1;
	}
	uint32_t result_len = 0;
	*new_data_begin = port_get_msgpack(&out_port, &result_len);
	*new_data_end = *new_data_begin + result_len;
	/*
	 * Port Lua allocates memory for msgpack on the fiber's region
	 * so it is safe to destroy port.
	 */
	port_destroy(&out_port);
	/*
	 * Result of function call is expected to be an array. Even if UDF
	 * returns scalar or even no value at all - result of function's
	 * invocation is always wrapped into an array. Otherwise something went
	 * really wrong. In addition, for convenience sake (we are going to
	 * insert new tuple in a space), we assume that upgrade function
	 * returns array i.e. result should be if the form of "array of arrays":
	 * MP_ARRAY MP_ARRAY.
	 */
	assert(mp_typeof(**new_data_begin) == MP_ARRAY);
	uint32_t arr_sz = mp_decode_array(new_data_begin);
	if (arr_sz != 1 || mp_typeof(**new_data_begin) != MP_ARRAY) {
		diag_set(ClientError, ER_UPGRADE, space_name(space),
			"type of return value is expected to be array");
		return -1;
	}
	return 0;
}

/**
 * Function provides verification of three conditions:
 * 1. If new tuple satisfy new space format;
 * 2. If new tuple has the same PK;
 * 3. If new tuple remains unique (see comment in the body below).
 */
static int
tuple_upgrade_check_tuple(struct space *space, struct tuple *old_tuple,
			  const char *new_tuple, const char *new_tuple_end)
{
	assert(space_is_memtx(space));
	struct tuple_format *new_format = space->upgrade->format;
	assert(new_format != NULL);
	/*
	 * tuple_new() checks that converted tuple satisfies new format in
	 * terms of field types.
	 */
	struct tuple *tuple = tuple_new(new_format, new_tuple, new_tuple_end);
	if (tuple == NULL)
		return -1;
	int rc = 0;
	/* Check that PK isn't changed. */
	struct key_def *pk_key_def = space->index[0]->def->key_def;
	/* Upgrade operation is not supported for multikey indexes. */
	assert(!pk_key_def->is_multikey);
	if (tuple_compare(old_tuple, HINT_NONE, tuple, HINT_NONE,
			  pk_key_def) != 0) {
		diag_set(ClientError, ER_CANT_UPDATE_PRIMARY_KEY,
			 space->index[0]->def->name, space_name(space));
		rc = -1;
		goto finish;
	}
	/* Now check that unique constraints will be met. We consider that
	 * new key is OK in terms of uniqueness in case after upgrade it
	 * doesn't conflict with OLD keys (except the case when key remains
	 * unchanged). Imagine space contains three tuples and unique index
	 * covering them:
	 * [1] -upgrade-> [4] -- it is OK: 4 is unique (among old keys)
	 * [2] -upgrade-> [3] -- it is not OK: key 3 already exists
	 * [3] -upgrade-> [x] -- OK in case x >= 4 or x <= 0
	 */
	for (uint32_t i = 1; i < space->index_count; ++i) {
		struct index *idx = space->index[i];
		if (! idx->def->opts.is_unique)
			continue;
		struct key_def *key_def = idx->def->key_def;
		uint32_t new_key_size = 0;
		const char *new_key =
			tuple_extract_key(tuple, key_def, MULTIKEY_NONE,
					  &new_key_size);
		if (new_key == NULL) {
			rc = -1;
			goto finish;
		}
		uint32_t part_count = mp_decode_array(&new_key);
		assert(part_count == key_def->part_count);
		struct tuple *found = 0;
		if (index_get(idx, new_key, part_count, &found) != 0) {
			rc = -1;
			goto finish;
		}
		if (found != NULL) {
			if (tuple_compare(found, HINT_NONE, tuple, HINT_NONE,
					  pk_key_def) != 0) {
				diag_set(ClientError, ER_TUPLE_FOUND,
					 idx->def->name,
					 space_name(space),
					 tuple_str(found),
					 tuple_str(tuple));
				rc = -1;
				goto finish;
			}
		}
	}
finish:
	/* New tuple must be deleted. */
	assert(tuple->local_refs == 0);
	tuple_delete(tuple);
	return rc;
}

/** Execute _space_upgrade:delete({space_id}) request. */
static void
space_upgrade_delete_entry(uint32_t space_id)
{
	struct error *last_err = diag_last_error(diag_get());
	if (boxk(IPROTO_DELETE, BOX_SPACE_UPGRADE_ID, "[%u]", space_id) != 0) {
		struct error *e = diag_last_error(diag_get());
		say_error("Failed to delete upgrade entry: %s", e->errmsg);
		/* Restore original error. */
		diag_set_error(diag_get(), last_err);
	}
}

/** Execute _space_upgrade:update(space_id, {{'=', 2, status}}) request. */
static void
space_upgrade_update_entry_status(uint32_t space_id, enum space_upgrade_status status)
{
	struct error *last_err = diag_last_error(diag_get());
	if (boxk(IPROTO_UPDATE, BOX_SPACE_UPGRADE_ID, "[%u][[%s%u%s]]",
		    space_id, "=", BOX_SPACE_UPGRADE_FIELD_STATUS,
		    upgrade_status_strs[status]) != 0) {
		struct error *e = diag_last_error(diag_get());
		say_error("Failed to update upgrade status: %s", e->errmsg);
		/* Restore original error. */
		diag_set_error(diag_get(), last_err);
	}
}

int
space_upgrade_test(uint32_t space_id)
{
	struct space *space = space_by_id(space_id);
	assert(space_is_memtx(space));
	assert(space->upgrade != NULL);
	assert(space->upgrade->status == SPACE_UPGRADE_TEST);

	struct index *pk = space_index(space, 0);
	if (pk == NULL)
		return 0;
	struct iterator *it = index_create_iterator(pk, ITER_ALL, NULL, 0);
	if (it == NULL)
		return -1;
	struct func *convert = space->upgrade->func;
	int rc = 0;
	struct tuple *tuple;
	size_t processed_tuples = 0;
	uint32_t used = region_used(&fiber()->gc);
	while (true) {
		rc = iterator_next(it, &tuple);
		if (rc != 0 || tuple == NULL)
			break;
		const char *new_tuple_data = NULL;
		const char *new_tuple_data_end = NULL;
		if (upgrade_function_apply(space, convert, tuple,
					   &new_tuple_data,
					   &new_tuple_data_end) != 0) {
			rc = -1;
			break;
		}
		if (tuple_upgrade_check_tuple(space, tuple, new_tuple_data,
					      new_tuple_data_end) != 0) {
			rc = -1;
			break;
		}
		/*
		 * During test process we still allow new tuples to be
		 * inserted - in this case their format won't be checked.
		 * It's OK by design - error will be raised during real
		 * upgrade.
		 */
		if (++processed_tuples % UPGRADE_TX_BATCH_SIZE == 0) {
			say_info_ratelimited("Total number of verified tuples "
					     "of space %s: %lu",
					     space_name(space),
					     processed_tuples);
			tuple_ref(tuple);
			fiber_sleep(0);
			tuple_unref(tuple);
		}
	}
	iterator_delete(it);
	space_upgrade_delete_entry(space_id);
	region_truncate(&fiber()->gc, used);
	return rc;
}

int
space_upgrade(uint32_t space_id)
{
	struct space *space = space_by_id(space_id);
	assert(space_is_memtx(space));
	assert(space->upgrade != NULL);
	assert(space_is_being_upgraded(space));

	struct index *pk = space_index(space, 0);
	/* No indexes - nothing to upgrade. */
	if (pk == NULL)
		return 0;
	struct iterator *it = index_create_iterator(pk, ITER_ALL, NULL, 0);
	if (it == NULL)
		return -1;
	struct func *convert = space->upgrade->func;
	int rc = 0;
	struct tuple *tuple;
	size_t processed_tuples = 0;
	/*
	 * Memtx in most cases aborts tx in case of yield. Anyway
	 * check that there's no active tx.
	 */
	assert(in_txn() == NULL);
	struct txn *txn = NULL;
	size_t used = region_used(&fiber()->gc);
	while (true) {
		txn = (in_txn() != NULL) ? in_txn() : txn_begin();
		if (txn == NULL) {
			rc = -1;
			break;
		}
		rc = iterator_next(it, &tuple);
		if (rc != 0 || tuple == NULL)
			break;
		const char *new_tuple_data = NULL;
		const char *new_tuple_data_end = NULL;
		if (upgrade_function_apply(space, convert, tuple,
					   &new_tuple_data,
					   &new_tuple_data_end) != 0) {
			rc = -1;
			break;
		}
		if (tuple_upgrade(space, tuple, new_tuple_data,
				  new_tuple_data_end) != 0) {
			rc = -1;
			break;
		}
		/*
		 * If new tuples are inserted during yield then they'll have
		 * new format and upgrade is not required for them.
		 */
		if (++processed_tuples % UPGRADE_TX_BATCH_SIZE == 0) {
			tuple_ref(tuple);
			/*
			 * At this point we may commit extra entries in case
			 * transaction was started before upgrade:
			 * begin()
			 * s:replace({1}) -- To be committed.
			 * s:upgrade()
			 * So let's at least print warning.
			 */
			say_info_ratelimited("Total number of processed tuples "
					     "by upgrade of space %s: %lu",
					     space_name(space),
					     processed_tuples);
			rc = txn_commit(txn);
			region_truncate(&fiber()->gc, used);
			tuple_unref(tuple);
			ERROR_INJECT_YIELD(ERRINJ_SPACE_UPGRADE_DELAY);
		}
	}
	iterator_delete(it);
	if (in_txn() != NULL) {
		if (txn_commit(in_txn()) != 0)
			rc = -1;
		region_truncate(&fiber()->gc, used);
	}
	if (rc != 0)
		space_upgrade_update_entry_status(space_id, SPACE_UPGRADE_ERROR);
	else
		space_upgrade_delete_entry(space_id);
	return rc;
}
