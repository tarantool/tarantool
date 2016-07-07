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
	VinylIndex *index = (VinylIndex *)index_find(space, 0);
	struct vinyl_env *env = index->env;

	/* Check field count in tuple */
	space_validate_tuple_raw(space, request->tuple);

	/* Check tuple fields */
	tuple_validate_raw(space->format, request->tuple);

	struct vinyl_tuple *tuple = vinyl_tuple_from_data(index->db,
		request->tuple, request->tuple_end);
	if (tuple == NULL)
		diag_raise();
	auto tuple_guard = make_scoped_guard([=]{
		vinyl_tuple_unref(index->db, tuple);
	});

	struct vinyl_tx *tx = vinyl_begin(env);
	if (tx == NULL)
		vinyl_raise();

	int64_t signature = request->header->lsn;

	if (vinyl_replace(tx, index->db, tuple) != 0)
		vinyl_raise();

	int rc = vinyl_prepare(env, tx);
	switch (rc) {
	case 0:
		if (vinyl_commit(env, tx, signature))
			panic("failed to commit vinyl transaction");
		return;
	case 1: /* rollback */
	case 2: /* lock */
		vinyl_rollback(env, tx);
		/* must never happen during JOIN */
		tnt_raise(ClientError, ER_TRANSACTION_CONFLICT);
		return;
	case -1:
		vinyl_rollback(env, tx);
		vinyl_raise();
		return;
	default:
		unreachable();
	}
}

struct tuple *
VinylSpace::executeReplace(struct txn*,
			  struct space *space,
			  struct request *request)
{
	VinylIndex *index = (VinylIndex *)index_find(space, 0);

	/* Check field count in tuple */
	space_validate_tuple_raw(space, request->tuple);

	/* Check tuple fields */
	tuple_validate_raw(space->format, request->tuple);

	struct vinyl_tuple *tuple = vinyl_tuple_from_data(index->db,
		request->tuple, request->tuple_end);
	if (tuple == NULL)
		diag_raise();
	auto tuple_guard = make_scoped_guard([=]{
		vinyl_tuple_unref(index->db, tuple);
	});

	/* unique constraint */
	if (request->type == IPROTO_INSERT) {
		enum dup_replace_mode mode = DUP_REPLACE_OR_INSERT;
		VinylEngine *engine =
			(VinylEngine *)space->handler->engine;
		if (engine->recovery_complete)
			mode = DUP_INSERT;
		if (mode == DUP_INSERT) {
			struct tuple *found = index->findByKey(tuple);
			if (found) {
				/*
				 * tuple is destroyed on the next call to
				 * box_tuple_XXX() API. See box_tuple_ref()
				 * comments.
				 */
				tnt_raise(ClientError, ER_TUPLE_FOUND,
						  index_name(index), space_name(space));
			}
		}
	}

	/* replace */
	struct vinyl_tx *tx = (struct vinyl_tx *)(in_txn()->engine_tx);
	int rc = vinyl_replace(tx, index->db, tuple);
	if (rc == -1)
		vinyl_raise();

	return NULL;
}

struct tuple *
VinylSpace::executeDelete(struct txn*, struct space *space,
                           struct request *request)
{
	VinylIndex *index = (VinylIndex *)
		index_find_unique(space, request->index_id);
	const char *key = request->key;
	uint32_t part_count = mp_decode_array(&key);
	primary_key_validate(index->key_def, key, part_count);

	struct vinyl_tuple *vinyl_key = vinyl_tuple_from_key_data(index->db,
		key, part_count, VINYL_EQ);
	if (vinyl_key == NULL)
		diag_raise();
	auto tuple_guard = make_scoped_guard([=]{
		vinyl_tuple_unref(index->db, vinyl_key);
	});

	/* remove */
	struct vinyl_tx *tx = (struct vinyl_tx *)(in_txn()->engine_tx);
	int rc = vinyl_delete(tx, index->db, vinyl_key);
	if (rc == -1)
		vinyl_raise();
	return NULL;
}

struct tuple *
VinylSpace::executeUpdate(struct txn*, struct space *space,
                           struct request *request)
{
	/* Try to find the tuple by unique key */
	VinylIndex *index = (VinylIndex *)
		index_find_unique(space, request->index_id);
	const char *key = request->key;
	uint32_t part_count = mp_decode_array(&key);
	primary_key_validate(index->key_def, key, part_count);
	struct tuple *old_tuple = index->findByKey(key, part_count);

	if (old_tuple == NULL)
		return NULL;

	/* Vinyl always yields a zero-ref tuple, GC it here. */
	TupleRef old_ref(old_tuple);

	/* Do tuple update */
	struct tuple *new_tuple =
		tuple_update(space->format,
		             region_aligned_alloc_xc_cb,
		             &fiber()->gc,
		             old_tuple, request->tuple,
		             request->tuple_end,
		             request->index_base);
	TupleRef ref(new_tuple);

	space_validate_tuple(space, new_tuple);
	space_check_update(space, old_tuple, new_tuple);

	struct vinyl_tuple *tuple = vinyl_tuple_from_data(index->db,
		new_tuple->data, new_tuple->data + new_tuple->bsize);
	if (tuple == NULL)
		diag_raise();
	auto tuple_guard = make_scoped_guard([=]{
		vinyl_tuple_unref(index->db, tuple);
	});

	/* replace */
	struct vinyl_tx *tx = (struct vinyl_tx *)(in_txn()->engine_tx);
	int rc = vinyl_replace(tx, index->db, tuple);
	if (rc == -1)
		vinyl_raise();
	return NULL;
}

void
VinylSpace::executeUpsert(struct txn*, struct space *space,
                           struct request *request)
{
	VinylIndex *index = (VinylIndex *)index_find(space, request->index_id);

	/* Check field count in tuple */
	space_validate_tuple_raw(space, request->tuple);

	/* Check tuple fields */
	tuple_validate_raw(space->format, request->tuple);

	struct vinyl_tx *tx = (struct vinyl_tx *)(in_txn()->engine_tx);
	int rc = vinyl_upsert(tx, index->db, request->tuple, request->tuple_end,
			     request->ops, request->ops_end,
			     request->index_base);
	if (rc == -1)
		vinyl_raise();
}
