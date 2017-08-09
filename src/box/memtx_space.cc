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
#include "tuple_compare.h"
#include "xrow.h"
#include "memtx_hash.h"
#include "memtx_tree.h"
#include "memtx_rtree.h"
#include "memtx_bitset.h"
#include "port.h"
#include "memtx_tuple.h"
#include "column_mask.h"

/* {{{ DML */

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

enum {
	/**
	 * This number is calculated based on the
	 * max (realistic) number of insertions
	 * a deletion from a B-tree or an R-tree
	 * can lead to, and, as a result, the max
	 * number of new block allocations.
	 */
	RESERVE_EXTENTS_BEFORE_DELETE = 8,
	RESERVE_EXTENTS_BEFORE_REPLACE = 16
};

/**
 * A short-cut version of replace() used during bulk load
 * from snapshot.
 */
void
memtx_replace_build_next(struct txn_stmt *stmt, struct space *space,
			 enum dup_replace_mode mode)
{
	assert(stmt->old_tuple == NULL && mode == DUP_INSERT);
	(void) mode;
	if (stmt->old_tuple) {
		/*
		 * Called from txn_rollback() In practice
		 * is impossible: all possible checks for tuple
		 * validity are done before the space is changed,
		 * and WAL is off, so this part can't fail.
		 */
		panic("Failed to commit transaction when loading "
		      "from snapshot");
	}
	((MemtxIndex *) space->index[0])->buildNext(stmt->new_tuple);
	stmt->engine_savepoint = stmt;
	stmt->bsize_change = space_bsize_update(space, NULL, stmt->new_tuple);
}

/**
 * A short-cut version of replace() used when loading
 * data from XLOG files.
 */
void
memtx_replace_primary_key(struct txn_stmt *stmt, struct space *space,
			  enum dup_replace_mode mode)
{
	stmt->old_tuple = space->index[0]->replace(stmt->old_tuple,
						   stmt->new_tuple, mode);
	stmt->engine_savepoint = stmt;
	stmt->bsize_change = space_bsize_update(space, stmt->old_tuple, stmt->new_tuple);
}

/**
 * @brief A single method to handle REPLACE, DELETE and UPDATE.
 *
 * @param sp space
 * @param old_tuple the tuple that should be removed (can be NULL)
 * @param new_tuple the tuple that should be inserted (can be NULL)
 * @param mode      dup_replace_mode, used only if new_tuple is not
 *                  NULL and old_tuple is NULL, and only for the
 *                  primary key.
 *
 * For DELETE, new_tuple must be NULL. old_tuple must be
 * previously found in the primary key.
 *
 * For REPLACE, old_tuple must be NULL. The additional
 * argument dup_replace_mode further defines how REPLACE
 * should proceed.
 *
 * For UPDATE, both old_tuple and new_tuple must be given,
 * where old_tuple must be previously found in the primary key.
 *
 * Let's consider these three cases in detail:
 *
 * 1. DELETE, old_tuple is not NULL, new_tuple is NULL
 *    The effect is that old_tuple is removed from all
 *    indexes. dup_replace_mode is ignored.
 *
 * 2. REPLACE, old_tuple is NULL, new_tuple is not NULL,
 *    has one simple sub-case and two with further
 *    ramifications:
 *
 *	A. dup_replace_mode is DUP_INSERT. Attempts to insert the
 *	new tuple into all indexes. If *any* of the unique indexes
 *	has a duplicate key, deletion is aborted, all of its
 *	effects are removed, and an error is thrown.
 *
 *	B. dup_replace_mode is DUP_REPLACE. It means an existing
 *	tuple has to be replaced with the new one. To do it, tries
 *	to find a tuple with a duplicate key in the primary index.
 *	If the tuple is not found, throws an error. Otherwise,
 *	replaces the old tuple with a new one in the primary key.
 *	Continues on to secondary keys, but if there is any
 *	secondary key, which has a duplicate tuple, but one which
 *	is different from the duplicate found in the primary key,
 *	aborts, puts everything back, throws an exception.
 *
 *	For example, if there is a space with 3 unique keys and
 *	two tuples { 1, 2, 3 } and { 3, 1, 2 }:
 *
 *	This REPLACE/DUP_REPLACE is OK: { 1, 5, 5 }
 *	This REPLACE/DUP_REPLACE is not OK: { 2, 2, 2 } (there
 *	is no tuple with key '2' in the primary key)
 *	This REPLACE/DUP_REPLACE is not OK: { 1, 1, 1 } (there
 *	is a conflicting tuple in the secondary unique key).
 *
 *	C. dup_replace_mode is DUP_REPLACE_OR_INSERT. If
 *	there is a duplicate tuple in the primary key, behaves the
 *	same way as DUP_REPLACE, otherwise behaves the same way as
 *	DUP_INSERT.
 *
 * 3. UPDATE has to delete the old tuple and insert a new one.
 *    dup_replace_mode is ignored.
 *    Note that old_tuple primary key doesn't have to match
 *    new_tuple primary key, thus a duplicate can be found.
 *    For this reason, and since there can be duplicates in
 *    other indexes, UPDATE is the same as DELETE +
 *    REPLACE/DUP_INSERT.
 *
 * @return old_tuple. DELETE, UPDATE and REPLACE/DUP_REPLACE
 * always produce an old tuple. REPLACE/DUP_INSERT always returns
 * NULL. REPLACE/DUP_REPLACE_OR_INSERT may or may not find
 * a duplicate.
 *
 * The method is all-or-nothing in all cases. Changes are either
 * applied to all indexes, or nothing applied at all.
 *
 * Note, that even in case of REPLACE, dup_replace_mode only
 * affects the primary key, for secondary keys it's always
 * DUP_INSERT.
 *
 * The call never removes more than one tuple: if
 * old_tuple is given, dup_replace_mode is ignored.
 * Otherwise, it's taken into account only for the
 * primary key.
 */
void
memtx_replace_all_keys(struct txn_stmt *stmt, struct space *space,
		       enum dup_replace_mode mode)
{
	struct tuple *old_tuple = stmt->old_tuple;
	struct tuple *new_tuple = stmt->new_tuple;
	/*
	 * Ensure we have enough slack memory to guarantee
	 * successful statement-level rollback.
	 */
	memtx_index_extent_reserve(new_tuple ?
				   RESERVE_EXTENTS_BEFORE_REPLACE :
				   RESERVE_EXTENTS_BEFORE_DELETE);
	uint32_t i = 0;
	try {
		/* Update the primary key */
		Index *pk = index_find_xc(space, 0);
		assert(pk->index_def->opts.is_unique);
		/*
		 * If old_tuple is not NULL, the index
		 * has to find and delete it, or raise an
		 * error.
		 */
		old_tuple = pk->replace(old_tuple, new_tuple, mode);

		assert(old_tuple || new_tuple);
		/* Update secondary keys. */
		for (i++; i < space->index_count; i++) {
			Index *index = space->index[i];
			index->replace(old_tuple, new_tuple, DUP_INSERT);
		}
	} catch (Exception *e) {
		/* Rollback all changes */
		for (; i > 0; i--) {
			Index *index = space->index[i-1];
			index->replace(new_tuple, old_tuple, DUP_INSERT);
		}
		throw;
	}
	stmt->old_tuple = old_tuple;
	stmt->engine_savepoint = stmt;
	stmt->bsize_change = space_bsize_update(space, old_tuple, new_tuple);
}


MemtxSpace::MemtxSpace(Engine *e, struct tuple_format *format)
	: Handler(e),
	m_format(format)
{
	tuple_format_ref(m_format);
	replace = memtx_replace_no_keys;
}

MemtxSpace::~MemtxSpace()
{
	tuple_format_unref(m_format);
}

static inline enum dup_replace_mode
dup_replace_mode(uint32_t op)
{
	return op == IPROTO_INSERT ? DUP_INSERT : DUP_REPLACE_OR_INSERT;
}

void
MemtxSpace::applyInitialJoinRow(struct space *space, struct request *request)
{
	if (request->type != IPROTO_INSERT) {
		tnt_raise(ClientError, ER_UNKNOWN_REQUEST_TYPE,
				(uint32_t) request->type);
	}
	request->header->replica_id = 0;
	struct txn *txn = txn_begin_stmt(space);
	try {
		struct txn_stmt *stmt = txn_current_stmt(txn);
		prepareReplace(stmt, request);
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
MemtxSpace::prepareReplace(struct txn_stmt *stmt, struct request *request)
{
	stmt->new_tuple = memtx_tuple_new_xc(m_format, request->tuple,
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
	if (primary_key_validate(pk->index_def->key_def, key, part_count) != 0)
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
	if (primary_key_validate(pk->index_def->key_def, key, part_count) != 0)
		diag_raise();
	stmt->old_tuple = pk->findByKey(key, part_count);

	if (stmt->old_tuple == NULL)
		return;

	/* Update the tuple; legacy, request ops are in request->tuple */
	uint32_t new_size = 0, bsize;
	const char *old_data = tuple_data_range(stmt->old_tuple, &bsize);
	const char *new_data =
		tuple_update_execute(region_aligned_alloc_cb, &fiber()->gc,
				     request->tuple, request->tuple_end,
				     old_data, old_data + bsize,
				     &new_size, request->index_base, NULL);
	if (new_data == NULL)
		diag_raise();

	stmt->new_tuple = memtx_tuple_new_xc(m_format, new_data,
					     new_data + new_size);
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
	if (tuple_validate_raw(m_format, request->tuple))
		diag_raise();

	Index *index = index_find_unique(space, 0);

	struct index_def *index_def = index->index_def;
	uint32_t part_count = index->index_def->key_def->part_count;
	/* Extract the primary key from tuple. */
	const char *key = tuple_extract_key_raw(request->tuple,
						request->tuple_end,
						index_def->key_def, NULL);
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
		 * only make it to the error log in it.
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
		stmt->new_tuple = memtx_tuple_new_xc(m_format,
						     request->tuple,
						     request->tuple_end);
		tuple_ref(stmt->new_tuple);
	} else {
		uint32_t new_size = 0, bsize;
		const char *old_data = tuple_data_range(stmt->old_tuple,
							&bsize);
		/*
		 * Update the tuple.
		 * tuple_upsert_execute() fails on totally wrong
		 * tuple ops, but ignores ops that not suitable
		 * for the tuple.
		 */
		uint64_t column_mask = COLUMN_MASK_FULL;
		const char *new_data =
			tuple_upsert_execute(region_aligned_alloc_cb,
					     &fiber()->gc, request->ops,
					     request->ops_end, old_data,
					     old_data + bsize, &new_size,
					     request->index_base, false,
					     &column_mask);
		if (new_data == NULL)
			diag_raise();

		stmt->new_tuple = memtx_tuple_new_xc(m_format, new_data,
						     new_data + new_size);
		tuple_ref(stmt->new_tuple);

		Index *pk = space->index[0];
		if (!key_update_can_be_skipped(pk->index_def->key_def->column_mask,
					       column_mask) &&
		    tuple_compare(stmt->old_tuple, stmt->new_tuple,
				  pk->index_def->key_def) != 0) {
			/* Primary key is changed: log error and do nothing. */
			diag_set(ClientError, ER_CANT_UPDATE_PRIMARY_KEY,
				 pk->index_def->name, space_name(space));
			diag_log();
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
	prepareReplace(stmt, request);
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
	if (key_validate(index->index_def, type, key, part_count))
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
		port_add_tuple_xc(port, tuple);
	}
}

/* }}} DML */

/* {{{ DDL */

void
MemtxSpace::checkIndexDef(struct space *space, struct index_def *index_def)
{
	switch (index_def->type) {
	case HASH:
		if (! index_def->opts.is_unique) {
			tnt_raise(ClientError, ER_MODIFY_INDEX,
				  index_def->name,
				  space_name(space),
				  "HASH index must be unique");
		}
		break;
	case TREE:
		/* TREE index has no limitations. */
		break;
	case RTREE:
		if (index_def->key_def->part_count != 1) {
			tnt_raise(ClientError, ER_MODIFY_INDEX,
				  index_def->name,
				  space_name(space),
				  "RTREE index key can not be multipart");
		}
		if (index_def->opts.is_unique) {
			tnt_raise(ClientError, ER_MODIFY_INDEX,
				  index_def->name,
				  space_name(space),
				  "RTREE index can not be unique");
		}
		if (index_def->key_def->parts[0].type != FIELD_TYPE_ARRAY) {
			tnt_raise(ClientError, ER_MODIFY_INDEX,
				  index_def->name,
				  space_name(space),
				  "RTREE index field type must be ARRAY");
		}
		/* no furter checks of parts needed */
		return;
	case BITSET:
		if (index_def->key_def->part_count != 1) {
			tnt_raise(ClientError, ER_MODIFY_INDEX,
				  index_def->name,
				  space_name(space),
				  "BITSET index key can not be multipart");
		}
		if (index_def->opts.is_unique) {
			tnt_raise(ClientError, ER_MODIFY_INDEX,
				  index_def->name,
				  space_name(space),
				  "BITSET can not be unique");
		}
		if (index_def->key_def->parts[0].type != FIELD_TYPE_UNSIGNED &&
		    index_def->key_def->parts[0].type != FIELD_TYPE_STRING) {
			tnt_raise(ClientError, ER_MODIFY_INDEX,
				  index_def->name,
				  space_name(space),
				  "BITSET index field type must be NUM or STR");
		}
		/* no furter checks of parts needed */
		return;
	default:
		tnt_raise(ClientError, ER_INDEX_TYPE,
			  index_def->name,
			  space_name(space));
		break;
	}
	/* Only HASH and TREE indexes checks parts there */
	/* Just check that there are no ARRAY parts */
	for (uint32_t i = 0; i < index_def->key_def->part_count; i++) {
		if (index_def->key_def->parts[i].type == FIELD_TYPE_ARRAY) {
			tnt_raise(ClientError, ER_MODIFY_INDEX,
				  index_def->name,
				  space_name(space),
				  "ARRAY field type is not supported");
		}
	}
}

Index *
MemtxSpace::createIndex(struct space *space, struct index_def *index_def_arg)
{
	(void) space;
	switch (index_def_arg->type) {
	case HASH:
		return new MemtxHash(index_def_arg);
	case TREE:
		return new MemtxTree(index_def_arg);
	case RTREE:
		return new MemtxRTree(index_def_arg);
	case BITSET:
		return new MemtxBitset(index_def_arg);
	default:
		unreachable();
		return NULL;
	}
}

/**
 * Replicate engine state in a newly created space.
 * This function is invoked when executing a replace into _index
 * space originating either from a snapshot or from the binary
 * log. It brings the newly created space up to date with the
 * engine recovery state: if the event comes from the snapshot,
 * then the primary key is not built, otherwise it's created
 * right away.
 */
static void
memtx_add_primary_key(struct space *space, enum memtx_recovery_state state)
{
	struct MemtxSpace *handler = (struct MemtxSpace *) space->handler;
	switch (state) {
	case MEMTX_INITIALIZED:
		panic("can't create a new space before snapshot recovery");
		break;
	case MEMTX_INITIAL_RECOVERY:
		((MemtxIndex *) space->index[0])->beginBuild();
		handler->replace = memtx_replace_build_next;
		break;
	case MEMTX_FINAL_RECOVERY:
		((MemtxIndex *) space->index[0])->beginBuild();
		((MemtxIndex *) space->index[0])->endBuild();
		handler->replace = memtx_replace_primary_key;
		break;
	case MEMTX_OK:
		((MemtxIndex *) space->index[0])->beginBuild();
		((MemtxIndex *) space->index[0])->endBuild();
		handler->replace = memtx_replace_all_keys;
		break;
	}
}

void
MemtxSpace::addPrimaryKey(struct space *space)
{
	memtx_add_primary_key(space, ((MemtxEngine *) engine)->m_state);
}

void
MemtxSpace::dropPrimaryKey(struct space *space)
{
	(void )space;
	assert(this == space->handler);
	replace = memtx_replace_no_keys;
}

void
MemtxSpace::initSystemSpace(struct space *space)
{
	memtx_add_primary_key(space, MEMTX_OK);
}

void
MemtxSpace::buildSecondaryKey(struct space *old_space,
			      struct space *new_space, Index *new_index)
{
	struct index_def *new_index_def = new_index->index_def;
	/**
	 * If it's a secondary key, and we're not building them
	 * yet (i.e. it's snapshot recovery for memtx), do nothing.
	 */
	if (new_index_def->iid != 0) {
		struct MemtxSpace *handler;
		handler = (struct MemtxSpace *) new_space->handler;
		if (!(handler->replace == memtx_replace_all_keys))
			return;
	}
	Index *pk = index_find_xc(old_space, 0);

	/* Now deal with any kind of add index during normal operation. */
	struct iterator *it = pk->allocIterator();
	IteratorGuard guard(it);
	pk->initIterator(it, ITER_ALL, NULL, 0);

	/*
	 * The index has to be built tuple by tuple, since
	 * there is no guarantee that all tuples satisfy
	 * new index' constraints. If any tuple can not be
	 * added to the index (insufficient number of fields,
	 * etc., the build is aborted.
	 */
	/* Build the new index. */
	struct tuple *tuple;
	while ((tuple = it->next(it))) {
		/*
		 * Check that the tuple is OK according to the
		 * new format.
		 */
		if (tuple_validate(m_format, tuple))
			diag_raise();
		/*
		 * @todo: better message if there is a duplicate.
		 */
		struct tuple *old_tuple =
			new_index->replace(NULL, tuple, DUP_INSERT);
		assert(old_tuple == NULL); /* Guaranteed by DUP_INSERT. */
		(void) old_tuple;
	}
}

void
MemtxSpace::prepareTruncateSpace(struct space *old_space,
				 struct space *new_space)
{
	(void)new_space;
	MemtxSpace *handler = (MemtxSpace *) old_space->handler;
	replace = handler->replace;
}

void
MemtxSpace::commitTruncateSpace(struct space *old_space,
				struct space *new_space)
{
	(void)new_space;
	MemtxIndex *index = (MemtxIndex *) space_index(old_space, 0);
	if (index != NULL)
		index->truncate();
}
void
MemtxSpace::prepareAlterSpace(struct space *old_space, struct space *new_space)
{
	(void)new_space;
	MemtxSpace *handler = (MemtxSpace *) old_space->handler;
	replace = handler->replace;
}

/* }}} DDL */
