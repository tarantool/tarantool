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
#include "memtx_tx.h"
#include "tuple.h"
#include "xrow_update.h"
#include "xrow.h"
#include "memtx_hash.h"
#include "memtx_tree.h"
#include "memtx_rtree.h"
#include "memtx_bitset.h"
#include "memtx_engine.h"
#include "column_mask.h"
#include "sequence.h"
#include "memtx_tuple_compression.h"
#include "schema.h"
#include "result.h"
#include "small/region.h"

/*
 * Yield every 1K tuples while building a new index or checking
 * a space format. In debug mode yield more often for testing
 * purposes.
 *
 * Yields do not happen during recovery. At this point of time
 * iproto aready accepts requests, and yielding would allow them
 * to be proccessed while data is not fully recovered.
 */
#ifdef NDEBUG
enum { MEMTX_DDL_YIELD_LOOPS = 1000 };
#else
enum { MEMTX_DDL_YIELD_LOOPS = 10 };
#endif

static void
memtx_space_destroy(struct space *space)
{
	TRASH(space);
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
memtx_space_update_bsize(struct space *space, struct tuple *old_tuple,
			 struct tuple *new_tuple)
{
	assert(space->vtab->destroy == &memtx_space_destroy);
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
	tuple_ref(new_tuple);
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
	struct tuple *successor;
	if (index_replace(space->index[0], old_tuple,
			  new_tuple, mode, &old_tuple, &successor) != 0)
		return -1;
	memtx_space_update_bsize(space, old_tuple, new_tuple);
	if (new_tuple != NULL)
		tuple_ref(new_tuple);
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
	struct memtx_engine *memtx = (struct memtx_engine *)space->engine;
	/*
	 * Ensure we have enough slack memory to guarantee
	 * successful statement-level rollback.
	 */
	if (memtx_index_extent_reserve(memtx, new_tuple != NULL ?
				       RESERVE_EXTENTS_BEFORE_REPLACE :
				       RESERVE_EXTENTS_BEFORE_DELETE) != 0)
		return -1;

	uint32_t i = 0;

	/* Update the primary key */
	struct index *pk = index_find(space, 0);
	if (pk == NULL)
		return -1;
	assert(pk->def->opts.is_unique);

	/* Replace must be done in transaction, except ephemeral spaces. */
	assert(space->def->opts.is_ephemeral ||
	       (in_txn() != NULL && txn_current_stmt(in_txn()) != NULL));
	/*
	 * Don't use MVCC engine for ephemeral in any case.
	 * MVCC engine requires txn to be present as a storage for
	 * reads/writes/conflicts.
	 * Also, now there's no way to turn MVCC engine off: once MVCC engine
	 * starts to manage a space - direct access to it must be prohibited.
	 * Since modification of ephemeral spaces are allowed without txn,
	 * we must not use MVCC for those spaces even if txn is present now.
	 */
	if (memtx_tx_manager_use_mvcc_engine && !space->def->opts.is_ephemeral) {
		struct txn_stmt *stmt = txn_current_stmt(in_txn());
		return memtx_tx_history_add_stmt(stmt, old_tuple, new_tuple,
						 mode, result);
	}

	/*
	 * If old_tuple is not NULL, the index has to
	 * find and delete it, or return an error.
	 */
	struct tuple *successor;
	if (index_replace(pk, old_tuple, new_tuple, mode,
			  &old_tuple, &successor) != 0)
		return -1;
	assert(old_tuple || new_tuple);

	/* Update secondary keys. */
	for (i++; i < space->index_count; i++) {
		struct tuple *unused;
		struct index *index = space->index[i];
		if (index_replace(index, old_tuple, new_tuple,
				  DUP_INSERT, &unused, &unused) != 0)
			goto rollback;
	}

	memtx_space_update_bsize(space, old_tuple, new_tuple);
	if (new_tuple != NULL)
		tuple_ref(new_tuple);
	*result = old_tuple;
	return 0;

rollback:
	for (; i > 0; i--) {
		struct tuple *unused;
		struct index *index = space->index[i - 1];
		/* Rollback must not fail. */
		if (index_replace(index, new_tuple, old_tuple,
				  DUP_INSERT, &unused, &unused) != 0) {
			diag_log();
			unreachable();
			panic("failed to rollback change");
		}
	}
	return -1;
}

static inline enum dup_replace_mode
dup_replace_mode(uint16_t op)
{
	return op == IPROTO_INSERT ? DUP_INSERT : DUP_REPLACE_OR_INSERT;
}

/**
 * Call replace method in memtx space and fill txn statement in case of
 * success. @A new_tuple is expected to be referenced once and must be
 * unreferenced by caller in case of failure.
 */
static inline int
memtx_space_replace_tuple(struct space *space, struct txn_stmt *stmt,
			  struct tuple *old_tuple, struct tuple *new_tuple,
			  enum dup_replace_mode mode)
{
	struct memtx_space *memtx_space = (struct memtx_space *)space;
	struct tuple *result;
	struct tuple *orig_new_tuple = new_tuple;
	bool was_referenced = false;
	if (new_tuple != NULL && space->format->is_compressed) {
		new_tuple = memtx_tuple_compress(new_tuple);
		if (new_tuple == NULL)
			return -1;
		tuple_ref(new_tuple);
		was_referenced = true;
	}
	int rc = memtx_space->replace(space, old_tuple, new_tuple,
				      mode, &result);
	if (rc != 0)
		goto finish;
	txn_stmt_prepare_rollback_info(stmt, result, new_tuple);
	stmt->engine_savepoint = stmt;
	stmt->new_tuple = orig_new_tuple;
	stmt->old_tuple = result;
	if (stmt->old_tuple != NULL) {
		struct tuple *orig_old_tuple = stmt->old_tuple;
		stmt->old_tuple = memtx_tuple_decompress(stmt->old_tuple);
		if (stmt->old_tuple == NULL)
			return -1;
		tuple_ref(stmt->old_tuple);
		tuple_unref(orig_old_tuple);
	}
finish:
	/*
	 * Regardless of whether the function ended with success or
	 * failure, we should unref new_tuple if it was explicitly
	 * referenced. If function returns with error we unref tuple
	 * and immidiatly delete it, otherwise we unref tuple, but it
	 * still alive because tuple is referenced by the primary key.
	 */
	if (was_referenced)
		tuple_unref(new_tuple);
	return rc;
}

static int
memtx_space_execute_replace(struct space *space, struct txn *txn,
			    struct request *request, struct tuple **result)
{
	struct txn_stmt *stmt = txn_current_stmt(txn);
	enum dup_replace_mode mode = dup_replace_mode(request->type);
	struct tuple *new_tuple =
		space->format->vtab.tuple_new(space->format, request->tuple,
					      request->tuple_end);
	if (new_tuple == NULL)
		return -1;
	tuple_ref(new_tuple);

	if (mode == DUP_INSERT)
		stmt->does_require_old_tuple = true;

	if (memtx_space_replace_tuple(space, stmt, NULL, new_tuple,
				      mode) != 0) {
		tuple_unref(new_tuple);
		return -1;
	}
	*result = stmt->new_tuple;
	return 0;
}

static int
memtx_space_execute_delete(struct space *space, struct txn *txn,
			   struct request *request, struct tuple **result)
{
	struct txn_stmt *stmt = txn_current_stmt(txn);
	/* Try to find the tuple by unique key. */
	struct index *pk = index_find_unique(space, request->index_id);
	if (pk == NULL)
		return -1;
	const char *key = request->key;
	uint32_t part_count = mp_decode_array(&key);
	if (exact_key_validate(pk->def->key_def, key, part_count) != 0)
		return -1;
	struct tuple *old_tuple;
	if (index_get_internal(pk, key, part_count, &old_tuple) != 0)
		return -1;

	if (old_tuple == NULL) {
		*result = NULL;
		return 0;
	}

	/*
	 * We have to delete exactly old_tuple just because we return it as
	 * a result.
	 */
	stmt->does_require_old_tuple = true;

	if (memtx_space_replace_tuple(space, stmt, old_tuple, NULL,
				      DUP_REPLACE_OR_INSERT) != 0)
		return -1;
	*result = result_process(space, stmt->old_tuple);
	return *result == NULL ? -1 : 0;
}

static int
memtx_space_execute_update(struct space *space, struct txn *txn,
			   struct request *request, struct tuple **result)
{
	struct txn_stmt *stmt = txn_current_stmt(txn);
	/* Try to find the tuple by unique key. */
	struct index *pk = index_find_unique(space, request->index_id);
	if (pk == NULL)
		return -1;
	const char *key = request->key;
	uint32_t part_count = mp_decode_array(&key);
	if (exact_key_validate(pk->def->key_def, key, part_count) != 0)
		return -1;
	struct tuple *old_tuple;
	if (index_get_internal(pk, key, part_count, &old_tuple) != 0)
		return -1;

	if (old_tuple == NULL) {
		*result = NULL;
		return 0;
	}

	struct tuple *decompressed = memtx_tuple_decompress(old_tuple);
	if (decompressed == NULL)
		return -1;
	tuple_bless(decompressed);
	decompressed = result_process(space, decompressed);
	if (decompressed == NULL)
		return -1;

	/* Update the tuple; legacy, request ops are in request->tuple */
	uint32_t new_size = 0, bsize;
	struct tuple_format *format = space->format;
	const char *old_data = tuple_data_range(decompressed, &bsize);
	size_t region_svp = region_used(&fiber()->gc);
	const char *new_data =
		xrow_update_execute(request->tuple, request->tuple_end,
				    old_data, old_data + bsize, format,
				    &new_size, request->index_base, NULL);
	if (new_data == NULL)
		return -1;

	struct tuple *new_tuple =
		space->format->vtab.tuple_new(format, new_data,
					      new_data + new_size);
	region_truncate(&fiber()->gc, region_svp);
	if (new_tuple == NULL)
		return -1;
	tuple_ref(new_tuple);

	stmt->does_require_old_tuple = true;

	if (memtx_space_replace_tuple(space, stmt, old_tuple, new_tuple,
				      DUP_REPLACE) != 0) {
		tuple_unref(new_tuple);
		return -1;
	}
	*result = stmt->new_tuple;
	return 0;
}

static int
memtx_space_execute_upsert(struct space *space, struct txn *txn,
			   struct request *request)
{
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
	size_t region_svp = region_used(&fiber()->gc);
	/* Extract the primary key from tuple. */
	const char *key = tuple_extract_key_raw(request->tuple,
						request->tuple_end,
						index->def->key_def,
						MULTIKEY_NONE, NULL);
	if (key == NULL)
		return -1;
	/* Cut array header */
	mp_decode_array(&key);

	/* Try to find the tuple by primary key. */
	struct tuple *old_tuple;
	int rc = index_get_internal(index, key, part_count, &old_tuple);
	region_truncate(&fiber()->gc, region_svp);
	if (rc != 0)
		return -1;

	struct tuple_format *format = space->format;
	struct tuple *new_tuple = NULL;
	if (old_tuple == NULL) {
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
		if (xrow_update_check_ops(request->ops, request->ops_end,
					  format, request->index_base) != 0) {
			return -1;
		}
		new_tuple =
			space->format->vtab.tuple_new(format, request->tuple,
						      request->tuple_end);
		if (new_tuple == NULL)
			return -1;
		tuple_ref(new_tuple);
	} else {
		struct tuple *decompressed = memtx_tuple_decompress(old_tuple);
		if (decompressed == NULL)
			return -1;
		tuple_bless(decompressed);
		decompressed = result_process(space, decompressed);
		if (decompressed == NULL)
			return -1;

		uint32_t new_size = 0, bsize;
		const char *old_data = tuple_data_range(decompressed, &bsize);
		/*
		 * Update the tuple.
		 * xrow_upsert_execute() fails on totally wrong
		 * tuple ops, but ignores ops that not suitable
		 * for the tuple.
		 */
		uint64_t column_mask = COLUMN_MASK_FULL;
		size_t region_svp = region_used(&fiber()->gc);
		const char *new_data =
			xrow_upsert_execute(request->ops, request->ops_end,
					    old_data, old_data + bsize,
					    format, &new_size,
					    request->index_base, false,
					    &column_mask);
		if (new_data == NULL)
			return -1;

		new_tuple =
			space->format->vtab.tuple_new(format, new_data,
						      new_data + new_size);
		region_truncate(&fiber()->gc, region_svp);
		if (new_tuple == NULL)
			return -1;
		tuple_ref(new_tuple);

		struct index *pk = space->index[0];
		if (!key_update_can_be_skipped(pk->def->key_def->column_mask,
					       column_mask) &&
		    tuple_compare(old_tuple, HINT_NONE, new_tuple,
				  HINT_NONE, pk->def->key_def) != 0) {
			/* Primary key is changed: log error and do nothing. */
			diag_set(ClientError, ER_CANT_UPDATE_PRIMARY_KEY,
				 space_name(space));
			diag_log();
			tuple_unref(new_tuple);
			return 0;
		}
	}
	assert(new_tuple != NULL);

	stmt->does_require_old_tuple = true;

	/*
	 * It's OK to use DUP_REPLACE_OR_INSERT: we don't risk
	 * inserting a new tuple if the old one exists, since
	 * we checked this case explicitly and skipped the upsert
	 * above.
	 */
	if (memtx_space_replace_tuple(space, stmt, old_tuple, new_tuple,
				      DUP_REPLACE_OR_INSERT) != 0) {
		tuple_unref(new_tuple);
		return -1;
	}
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
	struct tuple *new_tuple =
		space->format->vtab.tuple_new(space->format, tuple, tuple_end);
	if (new_tuple == NULL)
		return -1;
	struct tuple *old_tuple;
	if (memtx_space->replace(space, NULL, new_tuple,
				 DUP_REPLACE_OR_INSERT, &old_tuple) != 0) {
		space->format->vtab.tuple_delete(space->format, new_tuple);
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

static int
memtx_space_ephemeral_rowid_next(struct space *space, uint64_t *rowid)
{
	assert(rowid != NULL);
	struct memtx_space *memtx_space = (struct memtx_space *)space;
	*rowid = memtx_space->rowid++;
	return 0;
}

/* }}} DML */

/* {{{ DDL */

static int
memtx_space_check_index_def(struct space *space, struct index_def *index_def)
{
	struct key_def *key_def = index_def->key_def;

	if (key_def->is_nullable) {
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
		if (key_def->is_multikey) {
			diag_set(ClientError, ER_MODIFY_INDEX,
				 index_def->name, space_name(space),
				 "HASH index cannot be multikey");
			return -1;
		}
		if (key_def->for_func_index) {
			diag_set(ClientError, ER_MODIFY_INDEX,
				 index_def->name, space_name(space),
				 "HASH index can not use a function");
			return -1;
		}
		break;
	case TREE:
		/* TREE index has no limitations. */
		break;
	case RTREE:
		if (key_def->part_count != 1) {
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
		if (key_def->parts[0].type != FIELD_TYPE_ARRAY) {
			diag_set(ClientError, ER_MODIFY_INDEX,
				 index_def->name, space_name(space),
				 "RTREE index field type must be ARRAY");
			return -1;
		}
		if (key_def->is_multikey) {
			diag_set(ClientError, ER_MODIFY_INDEX,
				 index_def->name, space_name(space),
				 "RTREE index cannot be multikey");
			return -1;
		}
		if (key_def->for_func_index) {
			diag_set(ClientError, ER_MODIFY_INDEX,
				 index_def->name, space_name(space),
				 "RTREE index can not use a function");
			return -1;
		}
		/* no furter checks of parts needed */
		return 0;
	case BITSET:
		if (key_def->part_count != 1) {
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
		if (key_def->parts[0].type != FIELD_TYPE_UNSIGNED &&
		    key_def->parts[0].type != FIELD_TYPE_STRING &&
		    key_def->parts[0].type != FIELD_TYPE_VARBINARY) {
			diag_set(ClientError, ER_MODIFY_INDEX,
				 index_def->name, space_name(space),
				 "BITSET index field type must be "
	 			 "NUM or STR or VARBINARY");
			return -1;
		}
		if (key_def->is_multikey) {
			diag_set(ClientError, ER_MODIFY_INDEX,
				 index_def->name, space_name(space),
				 "BITSET index cannot be multikey");
			return -1;
		}
		if (key_def->for_func_index) {
			diag_set(ClientError, ER_MODIFY_INDEX,
				 index_def->name, space_name(space),
				 "BITSET index can not use a function");
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
	for (uint32_t i = 0; i < key_def->part_count; i++) {
		struct key_part *part = &key_def->parts[i];
		if (part->type <= FIELD_TYPE_ANY ||
		    part->type >= FIELD_TYPE_INTERVAL) {
			diag_set(ClientError, ER_MODIFY_INDEX,
				 index_def->name, space_name(space),
				 tt_sprintf("field type '%s' is not supported",
					    field_type_strs[part->type]));
			return -1;
		}
	}
	return 0;
}

static struct index *
sequence_data_index_new(struct memtx_engine *memtx, struct index_def *def)
{
	struct index *index = memtx_hash_index_new(memtx, def);
	if (index == NULL)
		return NULL;

	static struct index_vtab vtab;
	static bool vtab_initialized;
	if (!vtab_initialized) {
		vtab = *index->vtab;
		vtab.create_read_view = sequence_data_read_view_create;
		vtab_initialized = true;
	}

	index->vtab = &vtab;
	return index;
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
		return memtx_hash_index_new(memtx, index_def);
	case TREE:
		return memtx_tree_index_new(memtx, index_def);
	case RTREE:
		return memtx_rtree_index_new(memtx, index_def);
	case BITSET:
		return memtx_bitset_index_new(memtx, index_def);
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

/*
 * Ongoing index build or format check state used by
 * corrseponding on_replace triggers.
 */
struct memtx_ddl_state {
	/* The index being built. */
	struct index *index;
	/* New format to be enforced. */
	struct tuple_format *format;
	/* Operation cursor. Marks the last processed tuple to date */
	struct tuple *cursor;
	/* Primary key key_def to compare new tuples with cursor. */
	struct key_def *cmp_def;
	struct diag diag;
	int rc;
};

static int
memtx_check_on_replace(struct trigger *trigger, void *event)
{
	struct txn *txn = event;
	struct memtx_ddl_state *state = trigger->data;
	struct txn_stmt *stmt = txn_current_stmt(txn);

	/* Nothing to check on DELETE. */
	if (stmt->new_tuple == NULL)
		return 0;

	/* We have already failed. */
	if (state->rc != 0)
		return 0;

	/*
	 * Only check format for already processed part of the space,
	 * all the tuples inserted below cursor will be checked by the
	 * main routine later.
	 */
	if (tuple_compare(state->cursor, HINT_NONE, stmt->new_tuple, HINT_NONE,
			  state->cmp_def) < 0)
		return 0;

	state->rc = memtx_tuple_validate(state->format, stmt->new_tuple);
	if (state->rc != 0)
		diag_move(diag_get(), &state->diag);
	return 0;
}

static int
memtx_space_check_format(struct space *space, struct tuple_format *format)
{
	struct txn *txn = in_txn();

	if (space->index_count == 0)
		return 0;
	struct index *pk = space->index[0];
	if (index_size(pk) == 0)
		return 0;

	struct iterator *it = index_create_iterator(pk, ITER_ALL, NULL, 0);
	if (it == NULL)
		return -1;

	if (txn_check_singlestatement(txn, "space format check") != 0)
		return -1;

	struct memtx_engine *memtx = (struct memtx_engine *)space->engine;
	struct memtx_ddl_state state;
	state.format = format;
	state.cmp_def = pk->def->key_def;
	state.rc = 0;
	diag_create(&state.diag);

	struct trigger on_replace;
	trigger_create(&on_replace, memtx_check_on_replace, &state, NULL);
	trigger_add(&space->on_replace, &on_replace);

	int rc;
	struct tuple *tuple;
	size_t count = 0;
	while ((rc = iterator_next_internal(it, &tuple)) == 0 &&
	       tuple != NULL) {
		/*
		 * Check that the tuple is OK according to the
		 * new format.
		 */
		rc = memtx_tuple_validate(format, tuple);
		if (rc != 0)
			break;

		state.cursor = tuple;
		tuple_ref(state.cursor);

		if (++count % MEMTX_DDL_YIELD_LOOPS == 0 &&
		    memtx->state == MEMTX_OK)
			fiber_sleep(0);

		ERROR_INJECT_YIELD(ERRINJ_CHECK_FORMAT_DELAY);

		tuple_unref(state.cursor);
		if (state.rc != 0) {
			rc = -1;
			diag_move(&state.diag, diag_get());
			break;
		}
	}
	iterator_delete(it);
	diag_destroy(&state.diag);
	trigger_clear(&on_replace);
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

/*
 * Ongoing index build state with statement used by
 * corresponding on_rollback triggers to prevent rollbacked changes appearance.
 */
struct index_build_on_rollback_data {
	struct memtx_ddl_state *state;
	struct txn_stmt *stmt;
};

static int
memtx_build_on_replace_rollback(struct trigger *trigger, void *event)
{
	(void)event;
	struct index_build_on_rollback_data *data = trigger->data;
	struct txn_stmt *stmt = data->stmt;
	struct memtx_ddl_state *state = data->state;
	/*
	 * Old tuple's format is valid if it exists.
	 */
	assert(stmt != NULL);
	assert(stmt->old_tuple == NULL ||
	       memtx_tuple_validate(state->format, stmt->old_tuple) == 0);

	struct tuple *delete = NULL;
	struct tuple *successor = NULL;
	/*
	 * Use DUP_REPLACE_OR_INSERT mode because if we tried to replace a tuple
	 * with a duplicate at a unique index, this trigger would not be called.
	 */
	state->rc = index_replace(state->index, stmt->new_tuple,
				  stmt->old_tuple, DUP_REPLACE_OR_INSERT,
				  &delete, &successor);
	if (state->rc != 0) {
		diag_move(diag_get(), &state->diag);
		return 0;
	}
	/*
	 * All tuples stored in a memtx space are
	 * referenced by the primary index. That is
	 * why we need to ref new tuple and unref old tuple.
	 */
	if (state->index->def->iid == 0) {
		if (stmt->old_tuple != NULL)
			tuple_ref(stmt->old_tuple);
		if (stmt->new_tuple != NULL)
			tuple_unref(stmt->new_tuple);
	}

	return 0;
}

/*
 * Struct to allocate by memtx_build_on_replace trigger
 * (on_rollback trigger with its data).
 */
struct on_rollback_trigger_with_data {
	struct trigger on_rollback;
	struct index_build_on_rollback_data data;
};

static int
memtx_build_on_replace(struct trigger *trigger, void *event)
{
	struct txn *txn = event;
	struct memtx_ddl_state *state = trigger->data;
	struct txn_stmt *stmt = txn_current_stmt(txn);

	struct tuple *cmp_tuple = stmt->new_tuple != NULL ? stmt->new_tuple :
							    stmt->old_tuple;
	/*
	 * Only update the already built part of an index. All the other
	 * tuples will be inserted when build continues.
	 */
	if (tuple_compare(state->cursor, HINT_NONE, cmp_tuple, HINT_NONE,
			  state->cmp_def) < 0)
		return 0;

	if (stmt->new_tuple != NULL &&
	    memtx_tuple_validate(state->format, stmt->new_tuple) != 0) {
		state->rc = -1;
		diag_move(diag_get(), &state->diag);
		return 0;
	}

	struct tuple *delete = NULL;
	enum dup_replace_mode mode =
		state->index->def->opts.is_unique ? DUP_INSERT :
						    DUP_REPLACE_OR_INSERT;
	struct tuple *successor;
	state->rc = index_replace(state->index, stmt->old_tuple,
				  stmt->new_tuple, mode, &delete, &successor);
	if (state->rc != 0) {
		diag_move(diag_get(), &state->diag);
		return 0;
	}
	/*
	 * All tuples stored in a memtx space are
	 * referenced by the primary index. That is
	 * why we need to ref new tuple and unref old tuple.
	 */
	if (state->index->def->iid == 0) {
		if (stmt->new_tuple != NULL)
			tuple_ref(stmt->new_tuple);
		if (stmt->old_tuple != NULL)
			tuple_unref(stmt->old_tuple);
	}
	/*
	 * Set on_rollback trigger on stmt to avoid
	 * problem when rollbacked changes appears in
	 * built-in-background index.
	 */
	struct on_rollback_trigger_with_data *on_rollback_associates = NULL;
	struct errinj *inj = errinj(ERRINJ_BUILD_INDEX_ON_ROLLBACK_ALLOC,
				    ERRINJ_BOOL);
	if (inj == NULL || inj->bparam == false) {
		on_rollback_associates = region_aligned_alloc(
			&in_txn()->region,
			sizeof(struct on_rollback_trigger_with_data),
			alignof(struct on_rollback_trigger_with_data));
	}
	if (on_rollback_associates == NULL) {
		diag_set(OutOfMemory,
			 sizeof(struct on_rollback_trigger_with_data),
			 "region_aligned_alloc",
			 "struct on_rollback_trigger_with_data");
		diag_move(diag_get(), &state->diag);
		state->rc = -1;
		return 0;
	}
	on_rollback_associates->data.stmt = stmt;
	on_rollback_associates->data.state = state;
	trigger_create(&on_rollback_associates->on_rollback,
		       memtx_build_on_replace_rollback,
		       &on_rollback_associates->data, NULL);
	txn_stmt_on_rollback(stmt, &on_rollback_associates->on_rollback);
	return 0;
}

static int
memtx_space_build_index(struct space *src_space, struct index *new_index,
			struct tuple_format *new_format,
			bool check_unique_constraint)
{
	/* In memtx unique check comes for free so we never skip it. */
	(void)check_unique_constraint;

	struct txn *txn = in_txn();
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
	if (index_size(pk) == 0)
		return 0;

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
	 * If we insert a tuple during index being built, new tuple will or
	 * will not be inserted in index depending on result of lexicographical
	 * comparison with tuple which was inserted into new index last.
	 * The problem is HASH index is unordered, so background
	 * build will not work properly if primary key is HASH index.
	 */
	bool can_yield = pk->def->type != HASH;

	if (txn_check_singlestatement(txn, "index build") != 0)
		return -1;

	struct memtx_engine *memtx = (struct memtx_engine *)src_space->engine;
	struct memtx_ddl_state state;
	struct trigger on_replace;
	/*
	 * Create trigger and initialize ddl state
	 * if build in background is enabled.
	 */
	if (can_yield) {
		state.index = new_index;
		state.format = new_format;
		state.cmp_def = pk->def->key_def;
		state.rc = 0;
		diag_create(&state.diag);

		trigger_create(&on_replace, memtx_build_on_replace, &state,
			       NULL);
		trigger_add(&src_space->on_replace, &on_replace);
	}

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
	size_t count = 0;
	while ((rc = iterator_next_internal(it, &tuple)) == 0 &&
	       tuple != NULL) {
		struct key_def *key_def = new_index->def->key_def;
		if (!tuple_format_is_compatible_with_key_def(tuple_format(tuple),
							     key_def)) {
			rc = -1;
			break;
		}
		/*
		 * Check that the tuple is OK according to the
		 * new format.
		 */
		rc = memtx_tuple_validate(new_format, tuple);
		if (rc != 0)
			break;
		/*
		 * @todo: better message if there is a duplicate.
		 */
		struct tuple *old_tuple;
		struct tuple *successor;
		rc = index_replace(new_index, NULL, tuple,
				   DUP_INSERT, &old_tuple, &successor);
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
		/*
		 * Do not build index in background
		 * if the feature is disabled.
		 */
		if (!can_yield)
			continue;
		/*
		 * Remember the latest inserted tuple to
		 * avoid processing yet to be added tuples
		 * in on_replace triggers.
		 */
		state.cursor = tuple;
		tuple_ref(state.cursor);
		if (++count % MEMTX_DDL_YIELD_LOOPS == 0 &&
		    memtx->state == MEMTX_OK)
			fiber_sleep(0);
		/*
		 * Sleep after at least one tuple is inserted to test
		 * on_replace triggers for index build.
		 */
		ERROR_INJECT_YIELD(ERRINJ_BUILD_INDEX_DELAY);
		tuple_unref(state.cursor);
		/*
		 * The on_replace trigger may have failed
		 * during the yield.
		 */
		if (state.rc != 0) {
			rc = -1;
			diag_move(&state.diag, diag_get());
			break;
		}
	}
	iterator_delete(it);
	if (can_yield) {
		diag_destroy(&state.diag);
		trigger_clear(&on_replace);
	}
	return rc;
}

static int
memtx_space_prepare_alter(struct space *old_space, struct space *new_space)
{
	struct memtx_space *old_memtx_space = (struct memtx_space *)old_space;
	struct memtx_space *new_memtx_space = (struct memtx_space *)new_space;

	if (old_memtx_space->bsize != 0 &&
	    space_is_temporary(old_space) != space_is_temporary(new_space)) {
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
	/* .execute_replace = */ memtx_space_execute_replace,
	/* .execute_delete = */ memtx_space_execute_delete,
	/* .execute_update = */ memtx_space_execute_update,
	/* .execute_upsert = */ memtx_space_execute_upsert,
	/* .ephemeral_replace = */ memtx_space_ephemeral_replace,
	/* .ephemeral_delete = */ memtx_space_ephemeral_delete,
	/* .ephemeral_rowid_next = */ memtx_space_ephemeral_rowid_next,
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
	/* .invalidate = */ generic_space_invalidate,
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
	size_t region_svp = region_used(&fiber()->gc);
	struct key_def **keys = index_def_to_key_def(key_list, &key_count);
	if (keys == NULL) {
		free(memtx_space);
		return NULL;
	}
	struct tuple_format *format =
		space_tuple_format_new(&memtx_tuple_format_vtab,
				       memtx, keys, key_count, def);
	region_truncate(&fiber()->gc, region_svp);
	if (format == NULL) {
		free(memtx_space);
		return NULL;
	}
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
	memtx_space->rowid = 0;
	memtx_space->replace = memtx_space_replace_no_keys;
	return (struct space *)memtx_space;
}
