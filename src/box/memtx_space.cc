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
void
memtx_replace_no_keys(struct txn_stmt * /* stmt */, struct space *space,
		      enum dup_replace_mode /* mode */)
{
	Index *index = index_find_xc(space, 0);
	assert(index == NULL); /* not reached. */
	(void) index;
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

void
MemtxSpace::applyInitialJoinRow(struct space *space, struct request *request)
{
	assert(request->type == IPROTO_INSERT);
	request->header->server_id = 0;
	struct txn *txn = txn_begin_stmt(space);
	try {
		struct txn_stmt *stmt = txn_current_stmt(txn);
		prepareReplace(stmt, space, request);
		this->replace(stmt, space, DUP_INSERT);
		txn_commit_stmt(txn, request);
	} catch (Exception *e) {
		say_error("rollback: %s", e->errmsg);
		txn_rollback_stmt();
		throw;
	}
	/** The new tuple is referenced by the primary key. */
}

void
MemtxSpace::prepareReplace(struct txn_stmt *stmt, struct space *space,
			   struct request *request)
{
	stmt->new_tuple = tuple_new_xc(space->format, request->tuple,
				       request->tuple_end);
	tuple_ref(stmt->new_tuple);
}

void
MemtxSpace::prepareDelete(struct txn_stmt *stmt, struct space *space,
			  struct request *request)
{
	/* Try to find the tuple by unique key. */
	Index *pk = index_find_unique(space, request->index_id);
	const char *key = request->key;
	uint32_t part_count = mp_decode_array(&key);
	if (primary_key_validate(pk->key_def, key, part_count))
		diag_raise();
	stmt->old_tuple = pk->findByKey(key, part_count);
}

void
MemtxSpace::prepareUpdate(struct txn_stmt *stmt, struct space *space,
			  struct request *request)
{
	/* Try to find the tuple by unique key. */
	Index *pk = index_find_unique(space, request->index_id);
	const char *key = request->key;
	uint32_t part_count = mp_decode_array(&key);
	if (primary_key_validate(pk->key_def, key, part_count))
		diag_raise();
	stmt->old_tuple = pk->findByKey(key, part_count);

	if (stmt->old_tuple == NULL)
		return;

	/* Update the tuple; legacy, request ops are in request->tuple */
	stmt->new_tuple = tuple_update(space->format,
				       region_aligned_alloc_xc_cb,
				       &fiber()->gc,
				       stmt->old_tuple, request->tuple,
				       request->tuple_end,
				       request->index_base, NULL);
	tuple_ref(stmt->new_tuple);
}

void
MemtxSpace::prepareUpsert(struct txn_stmt *stmt, struct space *space,
			  struct request *request)
{
	/*
	 * Check all tuple fields: we should produce an error on
	 * malformed tuple even if upsert turns into an update.
	 */
	if (tuple_validate_raw(space->format, request->tuple))
		diag_raise();

	Index *index = index_find_unique(space, request->index_id);

	struct key_def *key_def = index->key_def;
	uint32_t part_count = index->key_def->part_count;
	/* Extract the primary key from tuple. */
	const char *key = tuple_extract_key_raw(request->tuple,
						request->tuple_end,
						key_def, NULL);
	if (key == NULL)
		diag_raise();
	/* Cut array header */
	mp_decode_array(&key);

	/* Try to find the tuple by primary key. */
	stmt->old_tuple = index->findByKey(key, part_count);

	if (stmt->old_tuple == NULL) {
		/**
		 * Old tuple was not found. A write optimized
		 * engine may only know this after commit, so
		 * some errors which happen on this branch would
		 * only make it to the server log in it.
		 * To provide identical semantics, we should not throw
		 * anything. However, considering the kind of
		 * error which may occur, throwing it won't break
		 * cross-engine compatibility:
		 * - update ops are checked before commit
		 * - OOM may happen at any time
		 * - duplicate key has to be checked by
		 *   write-optimized engine before commit, so if
		 *   we get it here, it's also OK to throw it
		 * @sa https://github.com/tarantool/tarantool/issues/1156
		 */
		if (tuple_update_check_ops(region_aligned_alloc_xc_cb, &fiber()->gc,
				       request->ops, request->ops_end,
				       request->index_base)) {
			diag_raise();
		}
		stmt->new_tuple = tuple_new_xc(space->format,
						       request->tuple,
						       request->tuple_end);
		tuple_ref(stmt->new_tuple);
	} else {
		/*
		 * Update the tuple.
		 * tuple_upsert() throws on totally wrong tuple ops,
		 * but ignores ops that not suitable for the tuple
		 */
		stmt->new_tuple = tuple_upsert(space->format,
					       region_aligned_alloc_xc_cb,
					       &fiber()->gc, stmt->old_tuple,
					       request->ops, request->ops_end,
					       request->index_base);

		tuple_ref(stmt->new_tuple);
		Index *pk = space->index[0];
		if (tuple_compare(stmt->old_tuple, stmt->new_tuple, pk->key_def)) {
			/* Primary key is changed: log error and do nothing. */
			diag_set(ClientError, ER_CANT_UPDATE_PRIMARY_KEY,
				 pk->key_def->name, space_name(space));
			error_log(diag_last_error(diag_get()));
			tuple_unref(stmt->new_tuple);
			stmt->old_tuple = NULL;
			stmt->new_tuple = NULL;
		}
	}
}

struct tuple *
MemtxSpace::executeReplace(struct txn *txn, struct space *space,
			   struct request *request)
{
	struct txn_stmt *stmt = txn_current_stmt(txn);
	enum dup_replace_mode mode = dup_replace_mode(request->type);
	prepareReplace(stmt, space, request);
	this->replace(stmt, space, mode);
	/** The new tuple is referenced by the primary key. */
	return stmt->new_tuple;
}

struct tuple *
MemtxSpace::executeDelete(struct txn *txn, struct space *space,
			  struct request *request)
{
	struct txn_stmt *stmt = txn_current_stmt(txn);
	prepareDelete(stmt, space, request);
	if (stmt->old_tuple)
		this->replace(stmt, space, DUP_REPLACE_OR_INSERT);
	return stmt->old_tuple;
}

struct tuple *
MemtxSpace::executeUpdate(struct txn *txn, struct space *space,
			  struct request *request)
{
	struct txn_stmt *stmt = txn_current_stmt(txn);
	prepareUpdate(stmt, space, request);
	if (stmt->old_tuple)
		this->replace(stmt, space, DUP_REPLACE);
	return stmt->new_tuple;
}

void
MemtxSpace::executeUpsert(struct txn *txn, struct space *space,
			  struct request *request)
{
	struct txn_stmt *stmt = txn_current_stmt(txn);
	prepareUpsert(stmt, space, request);
	/*
	 * It's OK to use DUP_REPLACE_OR_INSERT: we don't risk
	 * inserting a new tuple if the old one exists, since
	 * prepareUpsert() checked this case explicitly and
	 * skipped the upsert.
	 */
	if (stmt->new_tuple)
		this->replace(stmt, space, DUP_REPLACE_OR_INSERT);
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
