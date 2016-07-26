/*
 * Copyright 2010-2016, Tarantool AUTHORS, please see AUTHORS file.
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
#include "vinyl_engine.h"
#include "vinyl_index.h"
#include "vinyl_space.h"
#include "xrow.h"
#include "tuple.h"
#include "scoped_guard.h"
#include "txn.h"
#include "index.h"
#include "space.h"
#include "schema.h"
#include "port.h"
#include "request.h"
#include "iproto_constants.h"
#include "vinyl.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

VinylSpace::VinylSpace(Engine *e)
	:Handler(e)
{ }

void
VinylSpace::applySnapshotRow(struct space *space, struct request *request)
{
	assert(request->type == IPROTO_INSERT);
	assert(request->header != NULL);
	struct vinyl_env *env = ((VinylEngine *)space->handler->engine)->env;
	VinylIndex *index;

	/* Check field count in the tuple. */
	space_validate_tuple_raw(space, request->tuple);

	/* Check the tuple fields. */
	tuple_validate_raw(space->format, request->tuple);

	struct vinyl_tx *tx = vinyl_begin(env);
	if (tx == NULL)
		diag_raise();

	int64_t signature = request->header->lsn;
	for (uint32_t i = 0; i < space->index_count; ++i) {
		index = (VinylIndex *)space->index[i];
		if (vinyl_replace(tx, index->db, request->tuple,
				  request->tuple_end))
			diag_raise();
	}


	int rc = vinyl_prepare(env, tx);
	switch (rc) {
	case 0:
		if (vinyl_commit(env, tx, signature))
			panic("failed to commit vinyl transaction");
		return;
	case 1: /* rollback */
		vinyl_rollback(env, tx);
		/* must never happen during JOIN */
		tnt_raise(ClientError, ER_TRANSACTION_CONFLICT);
		return;
	case -1:
		vinyl_rollback(env, tx);
		diag_raise();
		return;
	default:
		unreachable();
	}
}

static int
vinyl_delete_from_all_indexes(struct space *space, struct tuple *tuple,
			      struct request *request, struct vinyl_tx *tx)
{
	uint32_t key_size;
	VinylIndex *index;
	uint32_t part_count;
	const char *key;
	for (uint32_t iid = 0; iid < space->index_count; ++iid) {
		index = (VinylIndex *)space->index[iid];
		if ((request->index_id == iid) && (request->key != NULL)) {
			key = request->key;
		} else {
			key = tuple_extract_key(tuple,
						vy_index_key_def(index->db),
						&key_size);
		}
		part_count = mp_decode_array(&key);
		if (vinyl_delete(tx, index->db, key, part_count) < 0)
			return 1;
	}
	return 0;
}

static void
vinyl_insert_in_one_idx_impl(VinylIndex *index, const char *tuple,
			     const char *tuple_end, uint32_t space_id,
			     struct vinyl_tx *tx)
{
	/*
	 * If the index is unique then the new tuple must not conflict with
	 * existing tuples. If the index is not unique conflict is impossible.
	 */
	if (index->key_def->opts.is_unique) {
		uint32_t key_len;
		struct key_def *vinyl_key_def = vy_index_key_def(index->db);
		const char *key;
		key = tuple_extract_key_raw(tuple, tuple_end,
					    vinyl_key_def, &key_len);
		mp_decode_array(&key); /* Skip array header. */
		struct tuple *found;
		vinyl_coget(tx, index->db, key,
			    vinyl_key_def->part_count,
			    &found);
		if (found) {
			struct space *space = space_by_id(space_id);
			tnt_raise(ClientError, ER_TUPLE_FOUND,
				  index_name(index), space_name(space));
		}
	}

	/* Tuple doesn't exists so it can be inserted. */
	if (vinyl_replace(tx, index->db, tuple,
			  tuple_end)) {
		diag_raise();
	}
}

static void
vinyl_insert_in_one_idx(struct space *space, struct request *request,
			struct vinyl_tx *tx)
{
	assert(request->type == IPROTO_INSERT);
	assert(space->index_count == 1);
	VinylIndex *index = (VinylIndex *)index_find(space, 0);

	vinyl_insert_in_one_idx_impl(index, request->tuple, request->tuple_end,
				     request->space_id, tx);
}

static void
vinyl_insert_in_all_indexes(struct space *space, struct request *request,
			    struct vinyl_tx *tx)
{
	assert(request->type == IPROTO_INSERT);
	/* Check if there are at least one index. */
	index_find(space, 0);
	for (uint32_t iid = 0; iid < space->index_count; ++iid) {
		vinyl_insert_in_one_idx_impl((VinylIndex *)space->index[iid],
					     request->tuple,
					     request->tuple_end,
					     request->space_id, tx);
	}
}

static void
vinyl_replace_in_one_idx(struct space *space, struct request *request,
			 struct vinyl_tx *tx)
{
	VinylEngine *engine = (VinylEngine *)space->handler->engine;
	assert((request->type == IPROTO_REPLACE) ||
	       (!engine->recovery_complete));
	assert(space->index_count == 1);
	VinylIndex *index = (VinylIndex *)index_find(space, 0);
	if (vinyl_replace(tx, index->db, request->tuple,
			  request->tuple_end) < 0) {
		diag_raise();
	}
}

static void
vinyl_replace_in_all_indexes(struct space *space, struct request *request,
			     struct vinyl_tx *tx)
{
	VinylEngine *engine = (VinylEngine *)space->handler->engine;
	assert((request->type == IPROTO_REPLACE) ||
	       (!engine->recovery_complete));
	struct tuple *full_tuple;
	VinylIndex *index = (VinylIndex *)index_find(space, 0);
	uint32_t key_size;
	const char *key = tuple_extract_key_raw(request->tuple,
						request->tuple_end,
						index->key_def,
						&key_size);
	uint32_t part_count = mp_decode_array(&key);
	/* If the request type is replace then delete the old tuple. */
	vinyl_coget(tx, index->db, key, part_count, &full_tuple);
	if (full_tuple &&
	    vinyl_delete_from_all_indexes(space, full_tuple, request, tx)) {
		diag_raise();
	}
	for (uint32_t i = 0; i < space->index_count; ++i) {
		index = (VinylIndex *)space->index[i];
		vinyl_insert_in_one_idx_impl(index, request->tuple,
					     request->tuple_end,
					     request->space_id, tx);
	}
}

/*
 * Four cases:
 *  - insert in one index
 *  - insert in multiple indexes
 *  - replace in one index
 *  - replace in multiple indexes.
 */
struct tuple *
VinylSpace::executeReplace(struct txn*,
			   struct space *space,
			   struct request *request)
{
	assert(request->index_id == 0);
	/* Check field count in the tuple. */
	space_validate_tuple_raw(space, request->tuple);

	/* Check the tuple fields. */
	tuple_validate_raw(space->format, request->tuple);
	struct vinyl_tx *tx = (struct vinyl_tx *)(in_txn()->engine_tx);
	VinylEngine *engine = (VinylEngine *)space->handler->engine;

	if ((request->type == IPROTO_INSERT) && engine->recovery_complete) {
		if (space->index_count == 1) {
			/*
			 * Get function for insert in space with
			 * single index.
			 */
			vinyl_insert_in_one_idx(space, request, tx);
		} else {
			/*
			 * Get function for insert in space with
			 * multiple indexes.
			 */
			 vinyl_insert_in_all_indexes(space, request, tx);
		}
	} else {
		if (space->index_count == 1) {
			/*
			 * Get function for replace in space with
			 * single index.
			 */
			vinyl_replace_in_one_idx(space, request, tx);
		} else {
			/*
			 * Get function for replace in space with
			 * multiple indexes.
			 */
			 vinyl_replace_in_all_indexes(space, request, tx);
		}
	}

	struct tuple *new_tuple = tuple_new(space->format, request->tuple,
					    request->tuple_end);
	/* GC the new tuple if there is an exception below. */
	TupleRef ref(new_tuple);

	return tuple_bless(new_tuple);
}

struct tuple *
VinylSpace::executeDelete(struct txn*, struct space *space,
                          struct request *request)
{
	VinylIndex *index = (VinylIndex *)index_find_unique(space,
							    request->index_id);

	/* Find full tuple in the index. */
	const char *key = request->key;
	uint32_t part_count = mp_decode_array(&key);
	primary_key_validate(index->key_def, key, part_count);

	struct tuple *full_tuple = NULL;
	struct vinyl_tx *tx = (struct vinyl_tx *)(in_txn()->engine_tx);
	if (space->index_count > 1) {
		if (vinyl_coget(tx, index->db, key,
				part_count, &full_tuple) != 0) {
			diag_raise();
			return NULL;
		}
		if (full_tuple &&
		    vinyl_delete_from_all_indexes(space, full_tuple,
						  request, tx))
			diag_raise();
	} else {
		if (vinyl_delete(tx, index->db, key, part_count) < 0)
			diag_raise();
	}
	return NULL;
}

struct tuple *
VinylSpace::executeUpdate(struct txn*, struct space *space,
                          struct request *request)
{
	uint32_t index_id = request->index_id;
	/* Find full tuple in the index. */
	struct tuple *old_full_tuple = NULL;
	VinylIndex *index = (VinylIndex *)index_find_unique(space, index_id);
	struct vinyl_tx *tx = (struct vinyl_tx *)(in_txn()->engine_tx);
	const char *key = request->key;
	uint32_t part_count = mp_decode_array(&key);
	primary_key_validate(index->key_def, key, part_count);

	vinyl_coget(tx, index->db, key, part_count, &old_full_tuple);
	if (old_full_tuple == NULL) {
		return NULL;
	}
	TupleRef old_ref(old_full_tuple);
	struct tuple *new_tuple;
	new_tuple = tuple_update(space->format, region_aligned_alloc_xc_cb,
				 &fiber()->gc, old_full_tuple, request->tuple,
				 request->tuple_end, request->index_base);
	TupleRef result_ref(new_tuple);
	space_validate_tuple(space, new_tuple);
	space_check_update(space, old_full_tuple, new_tuple);

	uint32_t key_size;
	for (uint32_t iid = 0; iid < space->index_count; ++iid) {
		index = (VinylIndex *)space->index[iid];
		if (iid != index_id) {
			key = tuple_extract_key(old_full_tuple, index->key_def,
						&key_size);
		} else {
			key = request->key;
		}
		part_count = mp_decode_array(&key);
		if (vinyl_delete(tx, index->db, key, part_count) < 0) {
			diag_raise();
			return NULL;
		}
		vinyl_insert_in_one_idx_impl(index, new_tuple->data,
					     new_tuple->data + new_tuple->bsize,
					     request->space_id, tx);
	}
	return tuple_bless(new_tuple);
}

void
VinylSpace::executeUpsert(struct txn*, struct space *space,
                           struct request *request)
{
	assert(request->index_id == 0);
	VinylIndex *index = (VinylIndex *)index_find(space, request->index_id);

	/* Check field count in tuple */
	space_validate_tuple_raw(space, request->tuple);

	/* Check tuple fields */
	tuple_validate_raw(space->format, request->tuple);

	struct vinyl_tx *tx = (struct vinyl_tx *)(in_txn()->engine_tx);
	for (uint32_t i = 0; i < space->index_count; ++i) {
		index = (VinylIndex *)space->index[i];
		if (vinyl_upsert(tx, index->db, request->tuple,
				 request->tuple_end, request->ops,
				 request->ops_end, request->index_base) < 0) {
			diag_raise();
		}
	}
}
