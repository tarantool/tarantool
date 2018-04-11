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
#include "tuple_update.h"
#include "xrow.h"
#include "memtx_hash.h"
#include "memtx_tree.h"
#include "memtx_rtree.h"
#include "memtx_bitset.h"
#include "memtx_tuple.h"
#include "column_mask.h"
#include "sequence.h"

static void
memtx_space_destroy(struct space *space)
{
	free(space);
}

static size_t
memtx_space_bsize(struct space *space)
{
	struct memtx_space *memtx_space = (struct memtx_space *)space;
	return memtx_space->bsize;
}

/* {{{ DML */

void
memtx_space_update_bsize(struct space *space,
			 const struct tuple *old_tuple,
			 const struct tuple *new_tuple)
{
	struct memtx_space *memtx_space = (struct memtx_space *)space;
	ssize_t old_bsize = old_tuple ? box_tuple_bsize(old_tuple) : 0;
	ssize_t new_bsize = new_tuple ? box_tuple_bsize(new_tuple) : 0;
	assert((ssize_t)memtx_space->bsize + new_bsize - old_bsize >= 0);
	memtx_space->bsize += new_bsize - old_bsize;
}

/**
 * A version of space_replace for a space which has
 * no indexes (is not yet fully built).
 */
int
memtx_space_replace_no_keys(struct space *space, struct tuple *old_tuple,
			    struct tuple *new_tuple,
			    enum dup_replace_mode mode, struct tuple **result)
{
	(void)old_tuple;
	(void)new_tuple;
	(void)mode;
	(void)result;
	struct index *index = index_find(space, 0);
	assert(index == NULL); /* not reached. */
	(void) index;
	return -1;
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
int
memtx_space_replace_build_next(struct space *space, struct tuple *old_tuple,
			       struct tuple *new_tuple,
			       enum dup_replace_mode mode,
			       struct tuple **result)
{
	assert(old_tuple == NULL && mode == DUP_INSERT);
	(void)mode;
	*result = NULL;
	if (old_tuple) {
		/*
		 * Called from txn_rollback() In practice
		 * is impossible: all possible checks for tuple
		 * validity are done before the space is changed,
		 * and WAL is off, so this part can't fail.
		 */
		panic("Failed to commit transaction when loading "
		      "from snapshot");
	}
	if (index_build_next(space->index[0], new_tuple) != 0)
		return -1;
	memtx_space_update_bsize(space, NULL, new_tuple);
	return 0;
}

/**
 * A short-cut version of replace() used when loading
 * data from XLOG files.
 */
int
memtx_space_replace_primary_key(struct space *space, struct tuple *old_tuple,
				struct tuple *new_tuple,
				enum dup_replace_mode mode,
				struct tuple **result)
{
	if (index_replace(space->index[0], old_tuple,
			  new_tuple, mode, &old_tuple) != 0)
		return -1;
	memtx_space_update_bsize(space, old_tuple, new_tuple);
	*result = old_tuple;
	return 0;
}

/**
 * @brief A single method to handle REPLACE, DELETE and UPDATE.
 *
 * @param space space
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
int
memtx_space_replace_all_keys(struct space *space, struct tuple *old_tuple,
			     struct tuple *new_tuple,
			     enum dup_replace_mode mode,
			     struct tuple **result)
{
	/*
	 * Ensure we have enough slack memory to guarantee
	 * successful statement-level rollback.
	 */
	if (memtx_index_extent_reserve(new_tuple ?
				       RESERVE_EXTENTS_BEFORE_REPLACE :
				       RESERVE_EXTENTS_BEFORE_DELETE) != 0)
		return -1;

	uint32_t i = 0;

	/* Update the primary key */
	struct index *pk = index_find(space, 0);
	if (pk == NULL)
		return -1;
	assert(pk->def->opts.is_unique);
	/*
	 * If old_tuple is not NULL, the index has to
	 * find and delete it, or return an error.
	 */
	if (index_replace(pk, old_tuple, new_tuple, mode, &old_tuple) != 0)
		return -1;
	assert(old_tuple || new_tuple);

	/* Update secondary keys. */
	for (i++; i < space->index_count; i++) {
		struct tuple *unused;
		struct index *index = space->index[i];
		if (index_replace(index, old_tuple, new_tuple,
				  DUP_INSERT, &unused) != 0)
			goto rollback;
	}

	memtx_space_update_bsize(space, old_tuple, new_tuple);
	*result = old_tuple;
	return 0;

rollback:
	for (; i > 0; i--) {
		struct tuple *unused;
		struct index *index = space->index[i - 1];
		/* Rollback must not fail. */
		if (index_replace(index, new_tuple, old_tuple,
				  DUP_INSERT, &unused) != 0) {
			diag_log();
			unreachable();
			panic("failed to rollback change");
		}
	}
	return -1;
}

static inline enum dup_replace_mode
dup_replace_mode(uint32_t op)
{
	return op == IPROTO_INSERT ? DUP_INSERT : DUP_REPLACE_OR_INSERT;
}

static int
memtx_space_apply_initial_join_row(struct space *space, struct request *request)
{
	struct memtx_space *memtx_space = (struct memtx_space *)space;
	if (request->type != IPROTO_INSERT) {
		diag_set(ClientError, ER_UNKNOWN_REQUEST_TYPE, request->type);
		return -1;
	}
	request->header->replica_id = 0;
	struct txn *txn = txn_begin_stmt(space);
	if (txn == NULL)
		return -1;
	struct txn_stmt *stmt = txn_current_stmt(txn);
	stmt->new_tuple = memtx_tuple_new(space->format, request->tuple,
					  request->tuple_end);
	if (stmt->new_tuple == NULL)
		goto rollback;
	tuple_ref(stmt->new_tuple);
	if (memtx_space->replace(space, NULL, stmt->new_tuple,
				 DUP_INSERT, &stmt->old_tuple) != 0)
		goto rollback;
	return txn_commit_stmt(txn, request);

rollback:
	say_error("rollback: %s", diag_last_error(diag_get())->errmsg);
	txn_rollback_stmt();
	return -1;
}

static int
memtx_space_execute_replace(struct space *space, struct txn *txn,
			    struct request *request, struct tuple **result)
{
	struct memtx_space *memtx_space = (struct memtx_space *)space;
	struct txn_stmt *stmt = txn_current_stmt(txn);
	enum dup_replace_mode mode = dup_replace_mode(request->type);
	stmt->new_tuple = memtx_tuple_new(space->format, request->tuple,
					  request->tuple_end);
	if (stmt->new_tuple == NULL)
		return -1;
	tuple_ref(stmt->new_tuple);
	struct tuple *old_tuple;
	if (memtx_space->replace(space, stmt->old_tuple, stmt->new_tuple,
				 mode, &old_tuple) != 0)
		return -1;
	stmt->old_tuple = old_tuple;
	stmt->engine_savepoint = stmt;
	/** The new tuple is referenced by the primary key. */
	*result = stmt->new_tuple;
	return 0;
}

static int
memtx_space_execute_delete(struct space *space, struct txn *txn,
			   struct request *request, struct tuple **result)
{
	struct memtx_space *memtx_space = (struct memtx_space *)space;
	struct txn_stmt *stmt = txn_current_stmt(txn);
	/* Try to find the tuple by unique key. */
	struct index *pk = index_find_unique(space, request->index_id);
	if (pk == NULL)
		return -1;
	const char *key = request->key;
	uint32_t part_count = mp_decode_array(&key);
	if (exact_key_validate(pk->def->key_def, key, part_count) != 0)
		return -1;
	if (index_get(pk, key, part_count, &stmt->old_tuple) != 0)
		return -1;
	struct tuple *old_tuple = NULL;
	if (stmt->old_tuple != NULL &&
	    memtx_space->replace(space, stmt->old_tuple, stmt->new_tuple,
				 DUP_REPLACE_OR_INSERT, &old_tuple) != 0)
		return -1;
	stmt->old_tuple = old_tuple;
	stmt->engine_savepoint = stmt;
	*result = stmt->old_tuple;
	return 0;
}

static int
memtx_space_execute_update(struct space *space, struct txn *txn,
			   struct request *request, struct tuple **result)
{
	struct memtx_space *memtx_space = (struct memtx_space *)space;
	struct txn_stmt *stmt = txn_current_stmt(txn);
	/* Try to find the tuple by unique key. */
	struct index *pk = index_find_unique(space, request->index_id);
	if (pk == NULL)
		return -1;
	const char *key = request->key;
	uint32_t part_count = mp_decode_array(&key);
	if (exact_key_validate(pk->def->key_def, key, part_count) != 0)
		return -1;
	if (index_get(pk, key, part_count, &stmt->old_tuple) != 0)
		return -1;

	if (stmt->old_tuple == NULL) {
		*result = NULL;
		return 0;
	}

	/* Update the tuple; legacy, request ops are in request->tuple */
	uint32_t new_size = 0, bsize;
	const char *old_data = tuple_data_range(stmt->old_tuple, &bsize);
	const char *new_data =
		tuple_update_execute(region_aligned_alloc_cb, &fiber()->gc,
				     request->tuple, request->tuple_end,
				     old_data, old_data + bsize,
				     &new_size, request->index_base, NULL);
	if (new_data == NULL)
		return -1;

	stmt->new_tuple = memtx_tuple_new(space->format, new_data,
					  new_data + new_size);
	if (stmt->new_tuple == NULL)
		return -1;
	tuple_ref(stmt->new_tuple);
	struct tuple *old_tuple = NULL;
	if (stmt->old_tuple != NULL &&
	    memtx_space->replace(space, stmt->old_tuple, stmt->new_tuple,
				 DUP_REPLACE, &old_tuple) != 0)
		return -1;
	stmt->old_tuple = old_tuple;
	stmt->engine_savepoint = stmt;
	*result = stmt->new_tuple;
	return 0;
}

static int
memtx_space_execute_upsert(struct space *space, struct txn *txn,
			   struct request *request)
{
	struct memtx_space *memtx_space = (struct memtx_space *)space;
	struct txn_stmt *stmt = txn_current_stmt(txn);
	/*
	 * Check all tuple fields: we should produce an error on
	 * malformed tuple even if upsert turns into an update.
	 */
	if (tuple_validate_raw(space->format, request->tuple))
		return -1;

	struct index *index = index_find_unique(space, 0);
	if (index == NULL)
		return -1;

	uint32_t part_count = index->def->key_def->part_count;
	/* Extract the primary key from tuple. */
	const char *key = tuple_extract_key_raw(request->tuple,
						request->tuple_end,
						index->def->key_def, NULL);
	if (key == NULL)
		return -1;
	/* Cut array header */
	mp_decode_array(&key);

	/* Try to find the tuple by primary key. */
	if (index_get(index, key, part_count, &stmt->old_tuple) != 0)
		return -1;

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
		if (tuple_update_check_ops(region_aligned_alloc_cb, &fiber()->gc,
				       request->ops, request->ops_end,
				       request->index_base)) {
			return -1;
		}
		stmt->new_tuple = memtx_tuple_new(space->format,
						  request->tuple,
						  request->tuple_end);
		if (stmt->new_tuple == NULL)
			return -1;
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
			return -1;

		stmt->new_tuple = memtx_tuple_new(space->format, new_data,
						  new_data + new_size);
		if (stmt->new_tuple == NULL)
			return -1;
		tuple_ref(stmt->new_tuple);

		struct index *pk = space->index[0];
		if (!key_update_can_be_skipped(pk->def->key_def->column_mask,
					       column_mask) &&
		    tuple_compare(stmt->old_tuple, stmt->new_tuple,
				  pk->def->key_def) != 0) {
			/* Primary key is changed: log error and do nothing. */
			diag_set(ClientError, ER_CANT_UPDATE_PRIMARY_KEY,
				 pk->def->name, space_name(space));
			diag_log();
			tuple_unref(stmt->new_tuple);
			stmt->old_tuple = NULL;
			stmt->new_tuple = NULL;
		}
	}
	/*
	 * It's OK to use DUP_REPLACE_OR_INSERT: we don't risk
	 * inserting a new tuple if the old one exists, since
	 * we checked this case explicitly and skipped the upsert
	 * above.
	 */
	struct tuple *old_tuple = NULL;
	if (stmt->new_tuple != NULL &&
	    memtx_space->replace(space, stmt->old_tuple, stmt->new_tuple,
				 DUP_REPLACE_OR_INSERT, &old_tuple) != 0)
		return -1;
	stmt->old_tuple = old_tuple;
	stmt->engine_savepoint = stmt;
	/* Return nothing: UPSERT does not return data. */
	return 0;
}

/**
 * This function simply creates new memtx tuple, refs it and calls space's
 * replace function. In constrast to original memtx_space_execute_replace(), it
 * doesn't handle any transaction routine.
 * Ephemeral spaces shouldn't be involved in transaction routine, since
 * they are used only for internal purposes. Moreover, ephemeral spaces
 * can be created and destroyed within one transaction and rollback of already
 * destroyed space may lead to undefined behaviour. For this reason it
 * doesn't take txn as an argument.
 */
static int
memtx_space_ephemeral_replace(struct space *space, const char *tuple,
				      const char *tuple_end)
{
	struct memtx_space *memtx_space = (struct memtx_space *)space;
	struct tuple *new_tuple = memtx_tuple_new(space->format, tuple,
						  tuple_end);
	if (new_tuple == NULL)
		return -1;
	tuple_ref(new_tuple);
	struct tuple *old_tuple = NULL;
	if (memtx_space->replace(space, old_tuple, new_tuple,
				 DUP_REPLACE_OR_INSERT, &old_tuple) != 0) {
		tuple_unref(new_tuple);
		return -1;
	}
	if (old_tuple != NULL)
		tuple_unref(old_tuple);
	return 0;
}

/**
 * Delete tuple with given key from primary index. Tuple checking is omitted
 * due to the ability of ephemeral spaces to hold nulls in primary key.
 * Generally speaking, it is not correct behaviour owing to ambiguity when
 * fetching/deleting tuple from space with several tuples containing
 * nulls in PK. On the other hand, ephemeral spaces are used only for internal
 * needs, so if it is guaranteed that no such situation occur
 * (when several tuples with nulls in PK exist), it is OK to allow
 * insertion nulls in PK.
 *
 * Similarly to ephemeral replace function,
 * it isn't involved in any transaction routine.
 */
static int
memtx_space_ephemeral_delete(struct space *space, const char *key)
{
	struct memtx_space *memtx_space = (struct memtx_space *)space;
	struct index *primary_index = space_index(space, 0 /* primary index*/);
	if (primary_index == NULL)
		return -1;
	uint32_t part_count = mp_decode_array(&key);
	struct tuple *old_tuple;
	if (index_get(primary_index, key, part_count, &old_tuple) != 0)
		return -1;
	if (old_tuple != NULL &&
	    memtx_space->replace(space, old_tuple, NULL,
				 DUP_REPLACE, &old_tuple) != 0)
		return -1;
	tuple_unref(old_tuple);
	return 0;
}

/* }}} DML */

/* {{{ DDL */

static int
memtx_space_check_index_def(struct space *space, struct index_def *index_def)
{
	if (index_def->key_def->is_nullable) {
		if (index_def->iid == 0) {
			diag_set(ClientError, ER_NULLABLE_PRIMARY,
				 space_name(space));
			return -1;
		}
		if (index_def->type != TREE) {
			diag_set(ClientError, ER_UNSUPPORTED,
				 index_type_strs[index_def->type],
				 "nullable parts");
			return -1;
		}
	}
	switch (index_def->type) {
	case HASH:
		if (! index_def->opts.is_unique) {
			diag_set(ClientError, ER_MODIFY_INDEX,
				 index_def->name, space_name(space),
				 "HASH index must be unique");
			return -1;
		}
		break;
	case TREE:
		/* TREE index has no limitations. */
		break;
	case RTREE:
		if (index_def->key_def->part_count != 1) {
			diag_set(ClientError, ER_MODIFY_INDEX,
				 index_def->name, space_name(space),
				 "RTREE index key can not be multipart");
			return -1;
		}
		if (index_def->opts.is_unique) {
			diag_set(ClientError, ER_MODIFY_INDEX,
				 index_def->name, space_name(space),
				 "RTREE index can not be unique");
			return -1;
		}
		if (index_def->key_def->parts[0].type != FIELD_TYPE_ARRAY) {
			diag_set(ClientError, ER_MODIFY_INDEX,
				 index_def->name, space_name(space),
				 "RTREE index field type must be ARRAY");
			return -1;
		}
		/* no furter checks of parts needed */
		return 0;
	case BITSET:
		if (index_def->key_def->part_count != 1) {
			diag_set(ClientError, ER_MODIFY_INDEX,
				 index_def->name, space_name(space),
				 "BITSET index key can not be multipart");
			return -1;
		}
		if (index_def->opts.is_unique) {
			diag_set(ClientError, ER_MODIFY_INDEX,
				 index_def->name, space_name(space),
				 "BITSET can not be unique");
			return -1;
		}
		if (index_def->key_def->parts[0].type != FIELD_TYPE_UNSIGNED &&
		    index_def->key_def->parts[0].type != FIELD_TYPE_STRING) {
			diag_set(ClientError, ER_MODIFY_INDEX,
				 index_def->name, space_name(space),
				 "BITSET index field type must be NUM or STR");
			return -1;
		}
		/* no furter checks of parts needed */
		return 0;
	default:
		diag_set(ClientError, ER_INDEX_TYPE,
			 index_def->name, space_name(space));
		return -1;
	}
	/* Only HASH and TREE indexes checks parts there */
	/* Check that there are no ANY, ARRAY, MAP parts */
	for (uint32_t i = 0; i < index_def->key_def->part_count; i++) {
		struct key_part *part = &index_def->key_def->parts[i];
		if (part->type <= FIELD_TYPE_ANY ||
		    part->type >= FIELD_TYPE_ARRAY) {
			diag_set(ClientError, ER_MODIFY_INDEX,
				 index_def->name, space_name(space),
				 tt_sprintf("field type '%s' is not supported",
					    field_type_strs[part->type]));
			return -1;
		}
	}
	return 0;
}

static struct snapshot_iterator *
sequence_data_index_create_snapshot_iterator(struct index *index)
{
	(void)index;
	return sequence_data_iterator_create();
}

static struct index *
sequence_data_index_new(struct memtx_engine *memtx, struct index_def *def)
{
	struct memtx_hash_index *index = memtx_hash_index_new(memtx, def);
	if (index == NULL)
		return NULL;

	static struct index_vtab vtab;
	static bool vtab_initialized;
	if (!vtab_initialized) {
		vtab = *index->base.vtab;
		vtab.create_snapshot_iterator =
			sequence_data_index_create_snapshot_iterator;
		vtab_initialized = true;
	}

	index->base.vtab = &vtab;
	return &index->base;
}

static struct index *
memtx_space_create_index(struct space *space, struct index_def *index_def)
{
	struct memtx_engine *memtx = (struct memtx_engine *)space->engine;

	if (space->def->id == BOX_SEQUENCE_DATA_ID) {
		/*
		 * The content of _sequence_data is not updated
		 * when a sequence is used for auto increment in
		 * a space. To make sure all sequence values are
		 * written to snapshot, use a special snapshot
		 * iterator that walks over the sequence cache.
		 */
		return sequence_data_index_new(memtx, index_def);
	}

	switch (index_def->type) {
	case HASH:
		return (struct index *)memtx_hash_index_new(memtx, index_def);
	case TREE:
		return (struct index *)memtx_tree_index_new(memtx, index_def);
	case RTREE:
		return (struct index *)memtx_rtree_index_new(memtx, index_def);
	case BITSET:
		return (struct index *)memtx_bitset_index_new(memtx, index_def);
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
static int
memtx_space_add_primary_key(struct space *space)
{
	struct memtx_space *memtx_space = (struct memtx_space *)space;
	struct memtx_engine *memtx = (struct memtx_engine *)space->engine;
	switch (memtx->state) {
	case MEMTX_INITIALIZED:
		panic("can't create a new space before snapshot recovery");
		break;
	case MEMTX_INITIAL_RECOVERY:
		index_begin_build(space->index[0]);
		memtx_space->replace = memtx_space_replace_build_next;
		break;
	case MEMTX_FINAL_RECOVERY:
		memtx_space->replace = memtx_space_replace_primary_key;
		break;
	case MEMTX_OK:
		memtx_space->replace = memtx_space_replace_all_keys;
		break;
	}
	return 0;
}

static int
memtx_space_check_format(struct space *space, struct tuple_format *format)
{
	if (space->index_count == 0)
		return 0;
	struct index *pk = space->index[0];
	if (index_size(pk) == 0)
		return 0;

	struct iterator *it = index_create_iterator(pk, ITER_ALL, NULL, 0);
	if (it == NULL)
		return -1;

	int rc;
	struct tuple *tuple;
	while ((rc = iterator_next(it, &tuple)) == 0 && tuple != NULL) {
		/*
		 * Check that the tuple is OK according to the
		 * new format.
		 */
		rc = tuple_validate(format, tuple);
		if (rc != 0)
			break;
	}
	iterator_delete(it);
	return rc;
}

static void
memtx_space_drop_primary_key(struct space *space)
{
	struct memtx_space *memtx_space = (struct memtx_space *)space;
	/*
	 * Reset 'replace' callback so that:
	 * - DML returns proper errors rather than crashes the
	 *   program.
	 * - When a new primary key is finally added, the space
	 *   can be put back online properly.
	 */
	memtx_space->replace = memtx_space_replace_no_keys;
	memtx_space->bsize = 0;
}

static void
memtx_init_system_space(struct space *space)
{
	struct memtx_space *memtx_space = (struct memtx_space *)space;
	memtx_space->replace = memtx_space_replace_all_keys;
}

static void
memtx_init_ephemeral_space(struct space *space)
{
	memtx_space_add_primary_key(space);
}

static int
memtx_space_build_index(struct space *src_space, struct index *new_index,
			struct tuple_format *new_format)
{
	/**
	 * If it's a secondary key, and we're not building them
	 * yet (i.e. it's snapshot recovery for memtx), do nothing.
	 */
	if (new_index->def->iid != 0) {
		struct memtx_space *memtx_space;
		memtx_space = (struct memtx_space *)src_space;
		if (!(memtx_space->replace == memtx_space_replace_all_keys))
			return 0;
	}
	struct index *pk = index_find(src_space, 0);
	if (pk == NULL)
		return -1;

	struct errinj *inj = errinj(ERRINJ_BUILD_INDEX, ERRINJ_INT);
	if (inj != NULL && inj->iparam == (int)new_index->def->iid) {
		diag_set(ClientError, ER_INJECTION, "build index");
		return -1;
	}

	/* Now deal with any kind of add index during normal operation. */
	struct iterator *it = index_create_iterator(pk, ITER_ALL, NULL, 0);
	if (it == NULL)
		return -1;

	/*
	 * The index has to be built tuple by tuple, since
	 * there is no guarantee that all tuples satisfy
	 * new index' constraints. If any tuple can not be
	 * added to the index (insufficient number of fields,
	 * etc., the build is aborted.
	 */
	/* Build the new index. */
	int rc;
	struct tuple *tuple;
	while ((rc = iterator_next(it, &tuple)) == 0 && tuple != NULL) {
		/*
		 * Check that the tuple is OK according to the
		 * new format.
		 */
		rc = tuple_validate(new_format, tuple);
		if (rc != 0)
			break;
		/*
		 * @todo: better message if there is a duplicate.
		 */
		struct tuple *old_tuple;
		rc = index_replace(new_index, NULL, tuple,
				   DUP_INSERT, &old_tuple);
		if (rc != 0)
			break;
		assert(old_tuple == NULL); /* Guaranteed by DUP_INSERT. */
		(void) old_tuple;
		/*
		 * All tuples stored in a memtx space must be
		 * referenced by the primary index.
		 */
		if (new_index->def->iid == 0)
			tuple_ref(tuple);
	}
	iterator_delete(it);
	return rc;
}

static void
memtx_space_ephemeral_cleanup(struct space *space)
{
	memtx_index_prune(space->index[0]);
}

static int
memtx_space_prepare_alter(struct space *old_space, struct space *new_space)
{
	struct memtx_space *old_memtx_space = (struct memtx_space *)old_space;
	struct memtx_space *new_memtx_space = (struct memtx_space *)new_space;

	if (old_memtx_space->bsize != 0 &&
	    old_space->def->opts.temporary != new_space->def->opts.temporary) {
		diag_set(ClientError, ER_ALTER_SPACE, old_space->def->name,
			 "can not switch temporary flag on a non-empty space");
		return -1;
	}

	new_memtx_space->replace = old_memtx_space->replace;
	new_memtx_space->bsize = old_memtx_space->bsize;
	return 0;
}

/* }}} DDL */

static const struct space_vtab memtx_space_vtab = {
	/* .destroy = */ memtx_space_destroy,
	/* .bsize = */ memtx_space_bsize,
	/* .apply_initial_join_row = */ memtx_space_apply_initial_join_row,
	/* .execute_replace = */ memtx_space_execute_replace,
	/* .execute_delete = */ memtx_space_execute_delete,
	/* .execute_update = */ memtx_space_execute_update,
	/* .execute_upsert = */ memtx_space_execute_upsert,
	/* .ephemeral_replace = */ memtx_space_ephemeral_replace,
	/* .ephemeral_delete = */ memtx_space_ephemeral_delete,
	/* .ephemeral_cleanup = */ memtx_space_ephemeral_cleanup,
	/* .init_system_space = */ memtx_init_system_space,
	/* .init_ephemeral_space = */ memtx_init_ephemeral_space,
	/* .check_index_def = */ memtx_space_check_index_def,
	/* .create_index = */ memtx_space_create_index,
	/* .add_primary_key = */ memtx_space_add_primary_key,
	/* .drop_primary_key = */ memtx_space_drop_primary_key,
	/* .check_format  = */ memtx_space_check_format,
	/* .build_index = */ memtx_space_build_index,
	/* .swap_index = */ generic_space_swap_index,
	/* .prepare_alter = */ memtx_space_prepare_alter,
};

struct space *
memtx_space_new(struct memtx_engine *memtx,
		struct space_def *def, struct rlist *key_list)
{
	struct memtx_space *memtx_space = malloc(sizeof(*memtx_space));
	if (memtx_space == NULL) {
		diag_set(OutOfMemory, sizeof(*memtx_space),
			 "malloc", "struct memtx_space");
		return NULL;
	}

	/* Create a format from key and field definitions. */
	int key_count = 0;
	struct index_def *index_def;
	rlist_foreach_entry(index_def, key_list, link)
		key_count++;
	struct key_def **keys = region_alloc(&fiber()->gc,
					     sizeof(*keys) * key_count);
	if (keys == NULL) {
		free(memtx_space);
		return NULL;
	}
	key_count = 0;
	rlist_foreach_entry(index_def, key_list, link)
		keys[key_count++] = index_def->key_def;

	struct tuple_format *format =
		tuple_format_new(&memtx_tuple_format_vtab, keys, key_count, 0,
				 def->fields, def->field_count, def->dict);
	if (format == NULL) {
		free(memtx_space);
		return NULL;
	}
	format->exact_field_count = def->exact_field_count;
	tuple_format_ref(format);

	if (space_create((struct space *)memtx_space, (struct engine *)memtx,
			 &memtx_space_vtab, def, key_list, format) != 0) {
		tuple_format_unref(format);
		free(memtx_space);
		return NULL;
	}

	/* Format is now referenced by the space. */
	tuple_format_unref(format);

	memtx_space->bsize = 0;
	memtx_space->replace = memtx_space_replace_no_keys;
	return (struct space *)memtx_space;
}
