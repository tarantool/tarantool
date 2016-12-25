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
#include "memtx_space.h"
#include "space.h"
#include "iproto_constants.h"
#include "txn.h"
#include "tuple.h"
#include "xrow.h"
#include "memtx_hash.h"
#include "memtx_tree.h"
#include "memtx_rtree.h"
#include "memtx_bitset.h"
#include "port.h"

/**
 * A version of space_replace for a space which has
 * no indexes (is not yet fully built).
 */
struct tuple *
memtx_replace_no_keys(struct space *space,
		      struct tuple * /* old_tuple */,
		      struct tuple * /* new_tuple */,
		      enum dup_replace_mode /* mode */)
{
	Index *index = index_find_xc(space, 0);
	assert(index == NULL); /* not reached. */
	(void) index;
	return NULL;
}

MemtxSpace::MemtxSpace(Engine *e)
	: Handler(e)
{
	replace = memtx_replace_no_keys;
}

static inline enum dup_replace_mode
dup_replace_mode(uint32_t op)
{
	return op == IPROTO_INSERT ? DUP_INSERT : DUP_REPLACE_OR_INSERT;
}

/**
 * Do the plumbing necessary for correct statement-level
 * and transaction rollback.
 */
static inline void
memtx_txn_add_undo(struct txn *txn, struct tuple *old_tuple,
		   struct tuple *new_tuple)
{
	/*
	 * Remember the old tuple only if we replaced it
	 * successfully, to not remove a tuple inserted by
	 * another transaction in rollback().
	 */
	struct txn_stmt *stmt = txn_current_stmt(txn);
	assert(stmt->space);
	stmt->old_tuple = old_tuple;
	stmt->new_tuple = new_tuple;
}

void
MemtxSpace::applySnapshotRow(struct space *space, struct request *request)
{
	assert(request->type == IPROTO_INSERT);
	struct tuple *new_tuple = tuple_new_xc(space->format, request->tuple,
					       request->tuple_end);
	/* GC the new tuple if there is an exception below. */
	TupleRef ref(new_tuple);
	if (!rlist_empty(&space->on_replace)) {
		/*
		 * Emulate transactions for system spaces with triggers
		 */
		assert(in_txn() == NULL);
		request->header->server_id = 0;
		struct txn *txn = txn_begin_stmt(space);
		try {
			struct tuple *old_tuple = this->replace(space, NULL,
				new_tuple, DUP_INSERT);
			memtx_txn_add_undo(txn, old_tuple, new_tuple);
			txn_commit_stmt(txn, request);
		} catch (Exception *e) {
			say_error("rollback: %s", e->errmsg);
			txn_rollback_stmt();
			throw;
		}
	} else {
		this->replace(space, NULL, new_tuple, DUP_INSERT);
	}
	/** The new tuple is referenced by the primary key. */
}

struct tuple *
MemtxSpace::executeReplace(struct txn *txn, struct space *space,
			   struct request *request)
{
	struct tuple *new_tuple = tuple_new_xc(space->format, request->tuple,
					       request->tuple_end);
	/* GC the new tuple if there is an exception below. */
	TupleRef ref(new_tuple);
	enum dup_replace_mode mode = dup_replace_mode(request->type);
	struct tuple *old_tuple = this->replace(space, NULL, new_tuple, mode);
	memtx_txn_add_undo(txn, old_tuple, new_tuple);
	/** The new tuple is referenced by the primary key. */
	return new_tuple;
}

struct tuple *
MemtxSpace::executeDelete(struct txn *txn, struct space *space,
			  struct request *request)
{
	/* Try to find the tuple by unique key. */
	Index *pk = index_find_unique(space, request->index_id);
	const char *key = request->key;
	uint32_t part_count = mp_decode_array(&key);
	if (primary_key_validate(pk->key_def, key, part_count))
		diag_raise();
	struct tuple *old_tuple = pk->findByKey(key, part_count);
	if (old_tuple == NULL)
		return NULL;

	this->replace(space, old_tuple, NULL, DUP_REPLACE_OR_INSERT);
	memtx_txn_add_undo(txn, old_tuple, NULL);
	return old_tuple;
}

struct tuple *
MemtxSpace::executeUpdate(struct txn *txn, struct space *space,
			  struct request *request)
{
	/* Try to find the tuple by unique key. */
	Index *pk = index_find_unique(space, request->index_id);
	const char *key = request->key;
	uint32_t part_count = mp_decode_array(&key);
	if (primary_key_validate(pk->key_def, key, part_count))
		diag_raise();
	struct tuple *old_tuple = pk->findByKey(key, part_count);

	if (old_tuple == NULL)
		return NULL;

	/* Update the tuple; legacy, request ops are in request->tuple */
	struct tuple *new_tuple = tuple_update(space->format,
					       region_aligned_alloc_xc_cb,
					       &fiber()->gc,
					       old_tuple, request->tuple,
					       request->tuple_end,
					       request->index_base, NULL);
	TupleRef ref(new_tuple);
	this->replace(space, old_tuple, new_tuple, DUP_REPLACE);
	memtx_txn_add_undo(txn, old_tuple, new_tuple);
	return new_tuple;
}

void
MemtxSpace::executeUpsert(struct txn *txn, struct space *space,
			  struct request *request)
{
	Index *pk = index_find_unique(space, request->index_id);

	/* Check tuple fields */
	if (tuple_validate_raw(space->format, request->tuple))
		diag_raise();

	struct key_def *key_def = pk->key_def;
	uint32_t part_count = pk->key_def->part_count;
	/*
	 * Extract the primary key from tuple.
	 * Allocate enough memory to store the key.
	 */
	const char *key = tuple_extract_key_raw(request->tuple,
						request->tuple_end,
						key_def, NULL);
	if (key == NULL)
		diag_raise();
	/* Cut array header */
	mp_decode_array(&key);

	/* Try to find the tuple by primary key. */
	struct tuple *old_tuple = pk->findByKey(key, part_count);

	if (old_tuple == NULL) {
		/**
		 * Old tuple was not found. In a "true"
		 * non-reading-write engine, this is known only
		 * after commit. Thus any error that can happen
		 * at this point is ignored. Emulate this by
		 * suppressing the error. It's logged and ignored.
		 *
		 * Taking into account that:
		 * 1) Default tuple fields are already fully checked
		 *    at the beginning of the function
		 * 2) Space with unique secondary indexes does not support
		 *    upsert and we can't get duplicate error
		 *
		 * Thus we could get only OOM error, but according to
		 *   https://github.com/tarantool/tarantool/issues/1156
		 *   we should not suppress it
		 *
		 * So we have nothing to catch and suppress!
		 */
		if (tuple_update_check_ops(region_aligned_alloc_xc_cb, &fiber()->gc,
				       request->ops, request->ops_end,
				       request->index_base)) {
			diag_raise();
		}
		struct tuple *new_tuple = tuple_new_xc(space->format,
						       request->tuple,
						       request->tuple_end);
		TupleRef ref(new_tuple); /* useless, for unified approach */
		old_tuple = replace(space, NULL, new_tuple, DUP_INSERT);
		memtx_txn_add_undo(txn, old_tuple, new_tuple);
	} else {
		/**
		 * Update the tuple.
		 * tuple_upsert throws on totally wrong tuple ops,
		 * but ignores ops that not suitable for the tuple
		 */
		struct tuple *new_tuple;
		new_tuple = tuple_upsert(space->format,
					 region_aligned_alloc_xc_cb,
					 &fiber()->gc, old_tuple,
					 request->ops, request->ops_end,
					 request->index_base);
		TupleRef ref(new_tuple);

		/**
		 * Ignore and log all client exceptions,
		 * note that OutOfMemory is not catched.
		 */
		try {
			replace(space, old_tuple, new_tuple, DUP_REPLACE);
			memtx_txn_add_undo(txn, old_tuple, new_tuple);
		} catch (ClientError *e) {
			say_error("UPSERT failed:");
			e->log();
		}
	}
	/* Return nothing: UPSERT does not return data. */
}

Index *
MemtxSpace::createIndex(struct space *space, struct key_def *key_def_arg)
{
	(void) space;
	switch (key_def_arg->type) {
	case HASH:
		return new MemtxHash(key_def_arg);
	case TREE:
		return new MemtxTree(key_def_arg);
	case RTREE:
		return new MemtxRTree(key_def_arg);
	case BITSET:
		return new MemtxBitset(key_def_arg);
	default:
		unreachable();
		return NULL;
	}
}

void
MemtxSpace::dropIndex(Index *index)
{
	if (index->key_def->iid != 0)
		return; /* nothing to do for secondary keys */
	/*
	 * Delete all tuples in the old space if dropping the
	 * primary key.
	 */
	struct iterator *it = ((MemtxIndex*) index)->position();
	index->initIterator(it, ITER_ALL, NULL, 0);
	struct tuple *tuple;
	while ((tuple = it->next(it)))
		tuple_unref(tuple);
}

void
MemtxSpace::prepareAlterSpace(struct space *old_space, struct space *new_space)
{
	(void)new_space;
	MemtxSpace *handler = (MemtxSpace *) old_space->handler;
	replace = handler->replace;
}

void
MemtxSpace::executeSelect(struct txn *, struct space *space,
			  uint32_t index_id, uint32_t iterator,
			  uint32_t offset, uint32_t limit,
			  const char *key, const char * /* key_end */,
			  struct port *port)
{
	MemtxIndex *index = (MemtxIndex *) index_find_xc(space, index_id);

	ERROR_INJECT_EXCEPTION(ERRINJ_TESTING);

	uint32_t found = 0;
	if (iterator >= iterator_type_MAX)
		tnt_raise(IllegalParams, "Invalid iterator type");
	enum iterator_type type = (enum iterator_type) iterator;

	uint32_t part_count = key ? mp_decode_array(&key) : 0;
	if (key_validate(index->key_def, type, key, part_count))
		diag_raise();

	struct iterator *it = index->position();
	index->initIterator(it, type, key, part_count);

	struct tuple *tuple;
	while ((tuple = it->next(it)) != NULL) {
		if (offset > 0) {
			offset--;
			continue;
		}
		if (limit == found++)
			break;
		port_add_tuple(port, tuple);
	}
}
