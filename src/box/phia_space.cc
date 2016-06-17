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
#include "phia_engine.h"
#include "phia_index.h"
#include "phia_space.h"
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
#include "phia.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

PhiaSpace::PhiaSpace(Engine *e)
	:Handler(e)
{ }

void
PhiaSpace::applySnapshotRow(struct space *space, struct request *request)
{
	assert(request->type == IPROTO_INSERT);
	assert(request->header != NULL);
	PhiaIndex *index = (PhiaIndex *)index_find(space, 0);

	/* Check field count in tuple */
	space_validate_tuple_raw(space, request->tuple);

	/* Check tuple fields */
	tuple_validate_raw(space->format, request->tuple);

	struct phia_tuple *tuple = phia_tuple_from_data(index->db,
		request->tuple, request->tuple_end);
	if (tuple == NULL)
		diag_raise();
	auto tuple_guard = make_scoped_guard([=]{
		phia_tuple_unref(index->db, tuple);
	});

	struct phia_tx *tx = phia_begin(index->env);
	if (tx == NULL)
		phia_raise();

	int64_t signature = request->header->lsn;

	if (phia_replace(tx, index->db, tuple) != 0)
		phia_raise();

	int rc = phia_prepare(tx);
	switch (rc) {
	case 0:
		if (phia_commit(tx, signature))
			panic("failed to commit phia transaction");
		return;
	case 1: /* rollback */
	case 2: /* lock */
		phia_rollback(tx);
		/* must never happen during JOIN */
		tnt_raise(ClientError, ER_TRANSACTION_CONFLICT);
		return;
	case -1:
		phia_rollback(tx);
		phia_raise();
		return;
	default:
		unreachable();
	}
}

struct tuple *
PhiaSpace::executeReplace(struct txn*,
			  struct space *space,
			  struct request *request)
{
	PhiaIndex *index = (PhiaIndex *)index_find(space, 0);

	/* Check field count in tuple */
	space_validate_tuple_raw(space, request->tuple);

	/* Check tuple fields */
	tuple_validate_raw(space->format, request->tuple);

	struct phia_tuple *tuple = phia_tuple_from_data(index->db,
		request->tuple, request->tuple_end);
	if (tuple == NULL)
		diag_raise();
	auto tuple_guard = make_scoped_guard([=]{
		phia_tuple_unref(index->db, tuple);
	});

	/* unique constraint */
	if (request->type == IPROTO_INSERT) {
		enum dup_replace_mode mode = DUP_REPLACE_OR_INSERT;
		PhiaEngine *engine =
			(PhiaEngine *)space->handler->engine;
		if (engine->recovery_complete)
			mode = DUP_INSERT;
		if (mode == DUP_INSERT) {
			struct tuple *found = index->findByKey(tuple);
			if (found) {
				tuple_delete(found);
				tnt_raise(ClientError, ER_TUPLE_FOUND,
						  index_name(index), space_name(space));
			}
		}
	}

	/* replace */
	struct phia_tx *tx = (struct phia_tx *)(in_txn()->engine_tx);
	int rc = phia_replace(tx, index->db, tuple);
	if (rc == -1)
		phia_raise();

	return NULL;
}

struct tuple *
PhiaSpace::executeDelete(struct txn*, struct space *space,
                           struct request *request)
{
	PhiaIndex *index = (PhiaIndex *)
		index_find_unique(space, request->index_id);
	const char *key = request->key;
	uint32_t part_count = mp_decode_array(&key);
	primary_key_validate(index->key_def, key, part_count);

	struct phia_tuple *phia_key = phia_tuple_from_key_data(index->db,
		key, part_count, PHIA_EQ);
	if (phia_key == NULL)
		diag_raise();
	auto tuple_guard = make_scoped_guard([=]{
		phia_tuple_unref(index->db, phia_key);
	});

	/* remove */
	struct phia_tx *tx = (struct phia_tx *)(in_txn()->engine_tx);
	int rc = phia_delete(tx, index->db, phia_key);
	if (rc == -1)
		phia_raise();
	return NULL;
}

struct tuple *
PhiaSpace::executeUpdate(struct txn*, struct space *space,
                           struct request *request)
{
	/* Try to find the tuple by unique key */
	PhiaIndex *index = (PhiaIndex *)
		index_find_unique(space, request->index_id);
	const char *key = request->key;
	uint32_t part_count = mp_decode_array(&key);
	primary_key_validate(index->key_def, key, part_count);
	struct tuple *old_tuple = index->findByKey(key, part_count);

	if (old_tuple == NULL)
		return NULL;

	/* Phia always yields a zero-ref tuple, GC it here. */
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

	struct phia_tuple *tuple = phia_tuple_from_data(index->db,
		new_tuple->data, new_tuple->data + new_tuple->bsize);
	if (tuple == NULL)
		diag_raise();
	auto tuple_guard = make_scoped_guard([=]{
		phia_tuple_unref(index->db, tuple);
	});

	/* replace */
	struct phia_tx *tx = (struct phia_tx *)(in_txn()->engine_tx);
	int rc = phia_replace(tx, index->db, tuple);
	if (rc == -1)
		phia_raise();
	return NULL;
}

void
PhiaSpace::executeUpsert(struct txn*, struct space *space,
                           struct request *request)
{
	PhiaIndex *index = (PhiaIndex *)index_find(space, request->index_id);

	/* Check field count in tuple */
	space_validate_tuple_raw(space, request->tuple);

	/* Check tuple fields */
	tuple_validate_raw(space->format, request->tuple);

	struct phia_tx *tx = (struct phia_tx *)(in_txn()->engine_tx);
	int rc = phia_upsert(tx, index->db, request->tuple, request->tuple_end,
			     request->ops, request->ops_end,
			     request->index_base);
	if (rc == -1)
		phia_raise();
}
