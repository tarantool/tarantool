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
#include "vinyl_space.h"
#include "vinyl_engine.h"
#include "vinyl_index.h"
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
{}

/* Raise an exception if the index contains a duplicate. */
static inline void
check_dup_key(VinylIndex *idx, const char *key, struct vy_tx *tx)
{
	struct tuple *found;
	mp_decode_array(&key); /* Skip array header. */
	if (vy_get(tx, idx->db, key, idx->key_def->part_count, &found))
		diag_raise();

	if (found) {
		struct space *space = space_by_id(idx->key_def->space_id);
		tnt_raise(ClientError, ER_TUPLE_FOUND,
			  index_name(idx), space_name(space));
	}
}

/* Insert a tuple only into the primary index. */
static void
vinyl_insert_primary(VinylPrimaryIndex *index, const char *tuple,
		     const char *tuple_end, struct vy_tx *tx)
{
	const char *key;
	uint32_t key_len;
	struct key_def *def = index->key_def;
	key = tuple_extract_key_raw(tuple, tuple_end, def, &key_len);
	/*
	 * A primary index always is unique and the new tuple must not
	 * conflict with existing tuples.
	 */
	check_dup_key(index, key, tx);

	if (vy_replace(tx, index->db, tuple, tuple_end))
		diag_raise();
}

/**
 * Insert a tuple into one secondary index.
 */
static void
vinyl_insert_secondary(VinylSecondaryIndex *index, const char *tuple,
		       const char *tuple_end, struct vy_tx *tx)
{
	const char *key, *key_end;
	uint32_t key_len;
	struct key_def *def = index->key_def;
	key = tuple_extract_key_raw(tuple, tuple_end,
				    index->key_def_tuple_to_key, &key_len);
	key_end = key + key_len;
	/*
	 * If the index is unique then the new tuple must not
	 * conflict with existing tuples. If the index is not
	 * unique a conflict is impossible.
	 */
	if (def->opts.is_unique)
		check_dup_key(index, key, tx);

	if (vy_replace(tx, index->db, key, key_end))
		diag_raise();
}

static void
vinyl_replace_all(struct space *space, struct request *request,
		  struct vy_tx *tx, struct txn_stmt *stmt)
{
	VinylEngine *engine = (VinylEngine *)space->handler->engine;
	(void) engine;
	struct tuple *old_tuple;
	VinylPrimaryIndex *pk = (VinylPrimaryIndex *) index_find(space, 0);
	const char *key;
	key = tuple_extract_key_raw(request->tuple, request->tuple_end,
				    pk->key_def, NULL);
	uint32_t part_count = mp_decode_array(&key);

	/* Get full tuple from the primary index. */
	if (vy_get(tx, pk->db, key, part_count, &old_tuple))
		diag_raise();
	/*
	 * Replace in the primary index without explicit deletion of
	 * the old tuple.
	 */
	if (vy_replace(tx, pk->db, request->tuple, request->tuple_end))
		diag_raise();

	/* Update secondary keys, avoid duplicates. */
	for (uint32_t iid = 1; iid < space->index_count; ++iid) {
		VinylSecondaryIndex *index;
		index = (VinylSecondaryIndex *) space->index[iid];
		/**
		 * Delete goes first, so if old and new keys
		 * fully match, there is no look up beyond the
		 * transaction index.
		 */
		if (old_tuple) {
			key = tuple_extract_key(old_tuple,
						index->key_def_tuple_to_key,
						NULL);
			part_count = mp_decode_array(&key);
			if (vy_delete(tx, index->db, key, part_count))
				diag_raise();
		}
		vinyl_insert_secondary(index, request->tuple,
				       request->tuple_end, tx);
	}
	/** The old tuple is used if there is an on_replace trigger. */
	if (stmt) {
		if (old_tuple)
			tuple_ref(old_tuple);
		stmt->old_tuple = old_tuple;
	}
}

void
VinylSpace::applySnapshotRow(struct space *space, struct request *request)
{
	assert(request->type == IPROTO_INSERT);
	assert(request->header != NULL);
	struct vy_env *env = ((VinylEngine *)space->handler->engine)->env;

	/* Check the tuple fields. */
	if (tuple_validate_raw(space->format, request->tuple))
		diag_raise();

	struct vy_tx *tx = vy_begin(env);
	if (tx == NULL)
		diag_raise();

	int64_t signature = request->header->lsn;

	vinyl_replace_all(space, request, tx, NULL);

	if (vy_prepare(env, tx)) {
		vy_rollback(env, tx);
		diag_raise();
	}
	if (vy_commit(env, tx, signature))
		panic("failed to commit vinyl transaction");
}

/**
 * Delete a tuple from all indexes, primary and secondary.
 */
static void
vinyl_delete_all(struct space *space, struct tuple *tuple,
		 struct request *request, struct vy_tx *tx)
{
	uint32_t part_count;
	const char *key;
	VinylPrimaryIndex *pk = (VinylPrimaryIndex *) index_find(space, 0);
	if (request->index_id == 0) {
		key = request->key;
	} else {
		key = tuple_extract_key(tuple, pk->key_def, NULL);
	}
	part_count = mp_decode_array(&key);
	if (vy_delete(tx, pk->db, key, part_count))
		diag_raise();

	VinylSecondaryIndex *index;
	for (uint32_t iid = 1; iid < space->index_count; ++iid) {
		index = (VinylSecondaryIndex *) space->index[iid];
		key = tuple_extract_key(tuple,
					index->key_def_tuple_to_key,
					NULL);
		part_count = mp_decode_array(&key);
		if (vy_delete(tx, index->db, key, part_count))
			diag_raise();
	}
}

/**
 * Insert a tuple into a space.
 */
static void
vinyl_insert_all(struct space *space, struct request *request,
		 struct vy_tx *tx)
{
	/* First insert into the primary index. */
	VinylPrimaryIndex *pk = (VinylPrimaryIndex *) index_find(space, 0);
	vinyl_insert_primary(pk, request->tuple, request->tuple_end, tx);

	for (uint32_t iid = 1; iid < space->index_count; ++iid) {
		VinylSecondaryIndex *index;
		index = (VinylSecondaryIndex *) space->index[iid];
		vinyl_insert_secondary(index, request->tuple,
				       request->tuple_end, tx);
	}
}

static void
vinyl_insert_without_lookup(struct space *space, struct request *request,
			    struct vy_tx *tx)
{
	VinylPrimaryIndex *pk = (VinylPrimaryIndex *) index_find(space, 0);
	if (vy_replace(tx, pk->db, request->tuple, request->tuple_end))
		diag_raise();
	for (uint32_t iid = 1; iid < space->index_count; ++iid) {
		VinylSecondaryIndex *index;
		index = (VinylSecondaryIndex *) space->index[iid];
		vinyl_insert_secondary(index, request->tuple,
				       request->tuple_end, tx);
	}
}

static void
vinyl_replace_one(struct space *space, struct request *request,
		  struct vy_tx *tx, struct txn_stmt *stmt)
{
	VinylEngine *engine = (VinylEngine *)space->handler->engine;
	(void) engine;
	assert(space->index_count == 1);
	VinylIndex *pk = (VinylIndex *) index_find(space, 0);
	/**
	 * If the space has triggers, then we need to fetch the old tuple to
	 * pass it to the trigger. Use vy_get to fetch it.
	 */
	if (!rlist_empty(&space->on_replace)) {
		const char *key;
		key = tuple_extract_key_raw(request->tuple, request->tuple_end,
					    pk->key_def, NULL);
		uint32_t part_count = mp_decode_array(&key);
		if (vy_get(tx, pk->db, key, part_count, &stmt->old_tuple))
			diag_raise();
		if (stmt->old_tuple)
			tuple_ref(stmt->old_tuple);
	}
	if (vy_replace(tx, pk->db, request->tuple, request->tuple_end))
		diag_raise();
}

/*
 * Four cases:
 *  - insert in one index
 *  - insert in multiple indexes
 *  - replace in one index
 *  - replace in multiple indexes.
 */
struct tuple *
VinylSpace::executeReplace(struct txn *txn, struct space *space,
			   struct request *request)
{
	assert(request->index_id == 0);

	/* Check the tuple fields. */
	if (tuple_validate_raw(space->format, request->tuple))
		diag_raise();
	struct vy_tx *tx = (struct vy_tx *)txn->engine_tx;
	VinylEngine *engine = (VinylEngine *)space->handler->engine;
	struct txn_stmt *stmt = txn_current_stmt(txn);

	if (request->type == IPROTO_INSERT && engine->recovery_complete) {
		vinyl_insert_all(space, request, tx);
	} else {
		if (space->index_count == 1) {
			/* Replace in a space with a single index. */
			vinyl_replace_one(space, request, tx, stmt);
		} else {
			/* Replace in a space with secondary indexes. */
			vinyl_replace_all(space, request, tx, stmt);
		}
	}

	struct tuple *new_tuple = tuple_new(space->format, request->tuple,
					    request->tuple_end);
	if (new_tuple == NULL)
		diag_raise();
	/* GC the new tuple if there is an exception below. */
	TupleRef ref(new_tuple);
	tuple_ref(new_tuple);
	stmt->new_tuple = new_tuple;
	return tuple_bless(new_tuple);
}

struct tuple *
VinylSpace::executeDelete(struct txn *txn, struct space *space,
                          struct request *request)
{
	VinylIndex *index;
	index = (VinylIndex *)index_find_unique(space, request->index_id);

	const char *key = request->key;
	uint32_t part_count = mp_decode_array(&key);
	if (primary_key_validate(index->key_def, key, part_count))
		diag_raise();
	struct txn_stmt *stmt = txn_current_stmt(txn);

	struct tuple *old_tuple = NULL;
	struct vy_tx *tx = (struct vy_tx *)txn->engine_tx;
	/**
	 * If there is more than one index, then get the old tuple and use it
	 * to extract key parts for all secondary indexes. The old tuple is
	 * also used if the space has triggers, in which case we need to pass
	 * it into the trigger.
	 */
	if (space->index_count > 1 || !rlist_empty(&space->on_replace)) {
		old_tuple = index->findByKey(key, part_count);
	}
	if (space->index_count > 1) {
		/**
		 * Find a full tuple to fetch keys of secondary indexes.
		 */
		if (old_tuple)
			vinyl_delete_all(space, old_tuple, request, tx);
	} else {
		if (vy_delete(tx, index->db, key, part_count))
			diag_raise();
	}
	if (old_tuple)
		tuple_ref(old_tuple);
	stmt->old_tuple = old_tuple;
	return NULL;
}

/**
 * Don't modify indexes whose fields were not changed by update.
 *
 * If there is at least one bit in the columns mask (@sa update_read_ops in
 * tuple_update.cc) set that corresponds to one of the columns from
 * key_def->parts, then the update operation changes at least one
 * indexed field and the optimization is inapplicable.
 *
 * Otherwise, we can skip the update.
 */
static bool
can_optimize_update(const VinylSecondaryIndex *idx, uint64_t column_mask)
{
	return (column_mask & idx->column_mask) == 0;
}

struct tuple *
VinylSpace::executeUpdate(struct txn *txn, struct space *space,
                          struct request *request)
{
	uint32_t index_id = request->index_id;
	struct tuple *old_tuple = NULL;
	VinylIndex *index = (VinylIndex *)index_find_unique(space, index_id);
	struct vy_tx *tx = (struct vy_tx *)txn->engine_tx;
	const char *key = request->key;
	uint32_t part_count = mp_decode_array(&key);
	if (primary_key_validate(index->key_def, key, part_count))
		diag_raise();
	struct txn_stmt *stmt = txn_current_stmt(txn);

	/* Find full tuple in the index. */
	old_tuple = index->findByKey(key, part_count);
	if (old_tuple == NULL)
		return NULL;

	TupleRef old_ref(old_tuple);
	struct tuple *new_tuple;
	uint64_t column_mask = 0;
	new_tuple = tuple_update(space->format, region_aligned_alloc_xc_cb,
				 &fiber()->gc, old_tuple, request->tuple,
				 request->tuple_end, request->index_base,
				 &column_mask);
	TupleRef new_ref(new_tuple);
	space_check_update(space, old_tuple, new_tuple);

	/**
	 * In the primary index tuple can be replaced
	 * without deleting old tuple.
	 */
	index = (VinylIndex *)space->index[0];
	if (vy_replace(tx, index->db, new_tuple->data,
		       new_tuple->data + new_tuple->bsize))
		diag_raise();

	/* Update secondary keys, avoid duplicates. */
	VinylSecondaryIndex *sec_idx;
	for (uint32_t iid = 1; iid < space->index_count; ++iid) {
		sec_idx = (VinylSecondaryIndex *) space->index[iid];
		key = tuple_extract_key(old_tuple, sec_idx->key_def_tuple_to_key,
					NULL);
		if (can_optimize_update(sec_idx, column_mask)) {
			continue;
		}
		part_count = mp_decode_array(&key);
		/**
		 * Delete goes first, so if old and new keys
		 * fully match, there is no look up beyond the
		 * transaction index.
		 */
		if (vy_delete(tx, sec_idx->db, key, part_count))
			diag_raise();
		vinyl_insert_secondary(sec_idx, new_tuple->data,
				       new_tuple->data + new_tuple->bsize, tx);
	}
	if (old_tuple)
		tuple_ref(old_tuple);
	tuple_ref(new_tuple);
	stmt->old_tuple = old_tuple;
	stmt->new_tuple = new_tuple;
	return tuple_bless(new_tuple);
}

void
VinylSpace::executeUpsert(struct txn *txn, struct space *space,
                           struct request *request)
{
	assert(request->index_id == 0);
	VinylIndex *pk;
	pk = (VinylIndex *)index_find_unique(space, 0);

	/* Check tuple fields. */
	if (tuple_validate_raw(space->format, request->tuple))
		diag_raise();

	struct vy_tx *tx = (struct vy_tx *)txn->engine_tx;
	/* Check update operations. */
	if (tuple_update_check_ops(region_aligned_alloc_xc_cb, &fiber()->gc,
			       request->ops, request->ops_end,
			       request->index_base)) {
		diag_raise();
	}
	const char *key;
	uint32_t part_count;
	struct txn_stmt *stmt = txn_current_stmt(txn);
	struct tuple *old_tuple = NULL;
	struct tuple *new_tuple = NULL;
	/**
	 * Need to look up the old tuple in two cases:
	 *   - if there is at least one on_replace trigger, then
	 *     old_tuple has to be passed into the trigger
	 *
	 *   - if the space has secondary indexes, then the old tuple
	 *     is necessary to apply update operations to it
	 *     and fetch the secondary key
	 */
	if (space->index_count > 1 || !rlist_empty(&space->on_replace)) {
		/* Find the old tuple using the primary key. */
		key = tuple_extract_key_raw(request->tuple, request->tuple_end,
					    pk->key_def, NULL);
		part_count = mp_decode_array(&key);
		old_tuple = pk->findByKey(key, part_count);
		if (old_tuple == NULL) {
			/**
			 * If the old tuple was not found then upsert
			 * becomes an insert.
			 */
			vinyl_insert_without_lookup(space, request, tx);
			new_tuple = tuple_new(space->format, request->tuple,
					      request->tuple_end);
			if (new_tuple == NULL)
				diag_raise();
			tuple_ref(new_tuple);
			stmt->new_tuple = new_tuple;
			return;
		}
	}
	/**
	 * This is a case of a simple and fast vy_upsert in the primary
	 * index: this index is covering and we don't need to
	 * fetch the old tuple to find out the key.
	 */
	if (vy_upsert(tx, pk->db, request->tuple,
		      request->tuple_end, request->ops,
		      request->ops_end, request->index_base) < 0) {
		diag_raise();
	}
	/**
	 * We have secondary keys or triggers, and there is an old
	 * tuple found in this space. Construct a new tuple to
	 * use in triggers and/or secondary keys.
	 */
	if (space->index_count > 1 || !rlist_empty(&space->on_replace)) {
		new_tuple = tuple_upsert(space->format,
					 region_aligned_alloc_xc_cb,
					 &fiber()->gc, old_tuple,
					 request->ops, request->ops_end,
					 request->index_base);
		TupleRef new_ref(new_tuple);
		/**
		 * Ignore errors since upsert doesn't raise not
		 * critical errors.
		 */
		try {
			space_check_update(space, old_tuple, new_tuple);
		} catch (ClientError *e) {
			e->log();
			return;
		}
		VinylSecondaryIndex *sec_idx;
		for (uint32_t i = 1; i < space->index_count; ++i) {
			/* Update secondary keys with the new tuple. */
			sec_idx = (VinylSecondaryIndex *) space->index[i];
			key = tuple_extract_key(old_tuple,
						sec_idx->key_def_tuple_to_key,
						NULL);
			part_count = mp_decode_array(&key);
			/*
			 * Delete the old tuple from the secondary key and
			 * insert the new tuple.
			 */
			if (vy_delete(tx, sec_idx->db, key, part_count))
				diag_raise();
			vinyl_insert_secondary(sec_idx, new_tuple->data,
					       new_tuple->data +
					       new_tuple->bsize, tx);
		}
		tuple_ref(new_tuple);
		stmt->new_tuple = new_tuple;
	}
	if (old_tuple)
		tuple_ref(old_tuple);
	stmt->old_tuple = old_tuple;
}

Index *
VinylSpace::createIndex(struct space *space, struct key_def *key_def)
{
	VinylEngine *engine = (VinylEngine *) this->engine;
	if (key_def->type != TREE) {
		unreachable();
		return NULL;
	}
	if (key_def->iid == 0)
		return new VinylPrimaryIndex(engine->env, key_def);
	/**
	 * Get pointer to the primary key from space->index_map, because
	 * the array space->index can be empty.
	 */
	VinylPrimaryIndex *pk = (VinylPrimaryIndex *) space_index(space, 0);
	return new VinylSecondaryIndex(engine->env, pk, key_def);
}

void
VinylSpace::dropIndex(Index *index)
{
	VinylIndex *i = (VinylIndex *)index;
	/* schedule asynchronous drop */
	int rc = vy_index_drop(i->db);
	if (rc == -1)
		diag_raise();
	i->db  = NULL;
	i->env = NULL;
}

void
VinylSpace::prepareAlterSpace(struct space *old_space, struct space *new_space)
{
	if (old_space->index_count &&
	    old_space->index_count <= new_space->index_count) {
		VinylEngine *engine = (VinylEngine *)old_space->handler->engine;
		Index *primary_index = index_find(old_space, 0);
		if (engine->recovery_complete && primary_index->min(NULL, 0)) {
			/**
			 * If space is not empty then forbid new indexes creating
			 */
			tnt_raise(ClientError, ER_UNSUPPORTED, "Vinyl",
				  "altering not empty space");
		}
	}
}

void
VinylSpace::commitAlterSpace(struct space *old_space, struct space *new_space)
{
	if (new_space == NULL || new_space->index_count == 0) {
		/* This is drop space. */
		return;
	}
	(void)old_space;
	VinylPrimaryIndex *primary = (VinylPrimaryIndex *)index_find(new_space, 0);
	for (uint32_t i = 1; i < new_space->index_count; ++i) {
		((VinylSecondaryIndex *)new_space->index[i])->primary_index = primary;
	}
}
