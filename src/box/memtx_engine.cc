/*
 * Copyright 2010-2015, Tarantool AUTHORS, please see AUTHORS file.
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
#include "memtx_engine.h"
#include "tuple.h"
#include "txn.h"
#include "index.h"
#include "memtx_hash.h"
#include "memtx_tree.h"
#include "memtx_rtree.h"
#include "memtx_bitset.h"
#include "space.h"
#include "msgpuck/msgpuck.h"
#include "small/rlist.h"
#include "request.h"
#include "box.h"
#include "iproto_constants.h"
#include "xrow.h"
#include "recovery.h"
#include "relay.h"
#include "schema.h"
#include "port.h"
#include "main.h"
#include "coeio_file.h"
#include "coeio.h"
#include "errinj.h"
#include "scoped_guard.h"

/** For all memory used by all indexes.
 * If you decide to use memtx_index_arena or
 * memtx_index_slab_cache for anything other than
 * memtx_index_extent_pool, make sure this is reflected in
 * box.slab.info(), @sa lua/slab.cc
 */
extern struct quota memtx_quota;
static bool memtx_index_arena_initialized = false;
extern struct slab_arena memtx_arena;
static struct slab_cache memtx_index_slab_cache;
struct mempool memtx_index_extent_pool;
/**
 * To ensure proper statement-level rollback in case
 * of out of memory conditions, we maintain a number
 * of slack memory extents reserved before a statement
 * is begun. If there isn't enough slack memory,
 * we don't begin the statement.
 */
static int memtx_index_num_reserved_extents;
static void *memtx_index_reserved_extents;

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
 * A version of space_replace for a space which has
 * no indexes (is not yet fully built).
 */
static void
memtx_replace_no_keys(struct txn * /* txn */, struct space *space,
		      struct tuple * /* old_tuple */,
		      struct tuple * /* new_tuple */,
		      enum dup_replace_mode /* mode */)
{
	Index *index = index_find(space, 0);
	assert(index == NULL); /* not reached. */
	(void) index;
}

struct MemtxSpace: public Handler {
	MemtxSpace(Engine *e)
		: Handler(e)
	{
		replace = memtx_replace_no_keys;
	}
	virtual ~MemtxSpace()
	{
		/* do nothing */
		/* engine->close(this); */
	}
	virtual struct tuple *
	executeReplace(struct txn *txn, struct space *space,
		       struct request *request);
	virtual struct tuple *
	executeDelete(struct txn *txn, struct space *space,
		      struct request *request);
	virtual struct tuple *
	executeUpdate(struct txn *txn, struct space *space,
		      struct request *request);
	virtual void
	executeUpsert(struct txn *txn, struct space *space,
		      struct request *request);
	virtual void
	executeSelect(struct txn *, struct space *space,
		      uint32_t index_id, uint32_t iterator,
		      uint32_t offset, uint32_t limit,
		      const char *key, const char * /* key_end */,
		      struct port *port);
	virtual void onAlter(Handler *old);
public:
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
	engine_replace_f replace;
};

static inline enum dup_replace_mode
dup_replace_mode(uint32_t op)
{
	return op == IPROTO_INSERT ? DUP_INSERT : DUP_REPLACE_OR_INSERT;
}

struct tuple *
MemtxSpace::executeReplace(struct txn *txn, struct space *space,
			   struct request *request)
{
	struct tuple *new_tuple = tuple_new(space->format, request->tuple,
					    request->tuple_end);
	/* GC the new tuple if there is an exception below. */
	TupleRef ref(new_tuple);
	space_validate_tuple(space, new_tuple);
	enum dup_replace_mode mode = dup_replace_mode(request->type);
	this->replace(txn, space, NULL, new_tuple, mode);
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
	primary_key_validate(pk->key_def, key, part_count);
	struct tuple *old_tuple = pk->findByKey(key, part_count);
	if (old_tuple == NULL)
		return NULL;

	this->replace(txn, space, old_tuple, NULL, DUP_REPLACE_OR_INSERT);
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
	primary_key_validate(pk->key_def, key, part_count);
	struct tuple *old_tuple = pk->findByKey(key, part_count);

	if (old_tuple == NULL)
		return NULL;

	/* Update the tuple; legacy, request ops are in request->tuple */
	struct tuple *new_tuple = tuple_update(space->format,
					       region_alloc_ex_cb,
					       &fiber()->gc,
					       old_tuple, request->tuple,
					       request->tuple_end,
					       request->index_base);
	TupleRef ref(new_tuple);
	space_validate_tuple(space, new_tuple);
	this->replace(txn, space, old_tuple, new_tuple, DUP_REPLACE);
	return new_tuple;
}

void
MemtxSpace::executeUpsert(struct txn *txn, struct space *space,
			  struct request *request)
{
	Index *pk = index_find_unique(space, request->index_id);

	/* Check field count in tuple */
	space_validate_tuple_raw(space, request->tuple);
	tuple_field_count_validate(space->format, request->tuple);
	uint32_t part_count = pk->key_def->part_count;
	/*
	 * Extract the primary key from tuple.
	 * Allocate enough memory to store the key.
	 */
	uint32_t key_len = request->tuple_end - request->tuple;
	char *key = (char *) region_alloc_xc(&fiber()->gc, key_len);
	key_len = key_parts_create_from_tuple(pk->key_def, request->tuple,
					      key, key_len);

	/* Try to find the tuple by primary key. */
	primary_key_validate(pk->key_def, key, part_count);
	struct tuple *old_tuple = pk->findByKey(key, part_count);

	if (old_tuple == NULL) {
		/**
		 * Old tuple was not found. In a "true"
		 * non-reading-write engine, this is known only
		 * after commit. Thus any error that can happen
		 * at this point is ignored. Emulate this by
		 * suppressing the error. It's logged and ignored.
		 *
		 * What sort of exception can happen here:
		 * - the format of the default tuple is incorrect
		 *   or not acceptable by this space.
		 * - we're out of memory for a new tuple.
		 * - unique key validation failure for the new tuple
		 */
		try {
			struct tuple *new_tuple = tuple_new(space->format,
							    request->tuple,
							    request->tuple_end);
			TupleRef ref(new_tuple);
			space_validate_tuple(space, new_tuple);
			this->replace(txn, space, NULL,
				      new_tuple, DUP_INSERT);
		} catch (ClientError *e) {
			say_error("UPSERT failed:");
			e->log();
		}
	} else {
		/* Update the tuple. */
		struct tuple *new_tuple =
			tuple_upsert(space->format, region_alloc_ex_cb,
				     &fiber()->gc, old_tuple,
				     request->ops, request->ops_end,
				     request->index_base);
		TupleRef ref(new_tuple);

		/** The rest must remain silent. */
		try {
			space_validate_tuple(space, new_tuple);
			this->replace(txn, space, old_tuple, new_tuple,
				      DUP_REPLACE);
		} catch (ClientError *e) {
			say_error("UPSERT failed:");
			e->log();
		}
	}
	/* Return nothing: UPSERT does not return data. */
}

void
MemtxSpace::onAlter(Handler *old)
{
	MemtxSpace *handler = (MemtxSpace *) old;
	replace = handler->replace;
}

void
MemtxSpace::executeSelect(struct txn *, struct space *space,
			  uint32_t index_id, uint32_t iterator,
			  uint32_t offset, uint32_t limit,
			  const char *key, const char * /* key_end */,
			  struct port *port)
{
	MemtxIndex *index = (MemtxIndex *) index_find(space, index_id);

	ERROR_INJECT_EXCEPTION(ERRINJ_TESTING);

	uint32_t found = 0;
	if (iterator >= iterator_type_MAX)
		tnt_raise(IllegalParams, "Invalid iterator type");
	enum iterator_type type = (enum iterator_type) iterator;

	uint32_t part_count = key ? mp_decode_array(&key) : 0;
	key_validate(index->key_def, type, key, part_count);

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

static void
txn_on_yield_or_stop(struct trigger * /* trigger */, void * /* event */)
{
	txn_rollback(); /* doesn't throw */
}

/**
 * Do the plumbing necessary for correct statement-level
 * and transaction rollback.
 */
static void
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

/**
 * A short-cut version of replace() used during bulk load
 * from snapshot.
 */
void
memtx_replace_build_next(struct txn * /* txn */, struct space *space,
			 struct tuple *old_tuple, struct tuple *new_tuple,
			 enum dup_replace_mode mode)
{
	assert(old_tuple == NULL && mode == DUP_INSERT);
	(void) mode;
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
	((MemtxIndex *) space->index[0])->buildNext(new_tuple);
	tuple_ref(new_tuple);
}

/**
 * A short-cut version of replace() used when loading
 * data from XLOG files.
 */
void
memtx_replace_primary_key(struct txn *txn, struct space *space,
			  struct tuple *old_tuple, struct tuple *new_tuple,
			  enum dup_replace_mode mode)
{
	old_tuple = space->index[0]->replace(old_tuple, new_tuple, mode);
	if (new_tuple)
		tuple_ref(new_tuple);
	memtx_txn_add_undo(txn, old_tuple, new_tuple);
}

static void
memtx_replace_all_keys(struct txn *txn, struct space *space,
		       struct tuple *old_tuple, struct tuple *new_tuple,
		       enum dup_replace_mode mode)
{
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
		Index *pk = index_find(space, 0);
		assert(pk->key_def->opts.is_unique);
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
	if (new_tuple)
		tuple_ref(new_tuple);
	memtx_txn_add_undo(txn, old_tuple, new_tuple);
}

static void
memtx_end_build_primary_key(struct space *space, void *param)
{
	struct MemtxSpace *handler = (struct MemtxSpace *) space->handler;
	if (handler->engine != param || space_index(space, 0) == NULL ||
	    handler->replace == memtx_replace_all_keys)
		return;

	((MemtxIndex *) space->index[0])->endBuild();
	handler->replace = memtx_replace_primary_key;
}

/**
 * Secondary indexes are built in bulk after all data is
 * recovered. This function enables secondary keys on a space.
 * Data dictionary spaces are an exception, they are fully
 * built right from the start.
 */
void
memtx_build_secondary_keys(struct space *space, void *param)
{
	struct MemtxSpace *handler = (struct MemtxSpace *) space->handler;
	if (handler->engine != param || space_index(space, 0) == NULL ||
	    handler->replace == memtx_replace_all_keys)
		return;

	if (space->index_id_max > 0) {
		MemtxIndex *pk = (MemtxIndex *) space->index[0];
		uint32_t n_tuples = pk->size();

		if (n_tuples > 0) {
			say_info("Building secondary indexes in space '%s'...",
				 space_name(space));
		}

		for (uint32_t j = 1; j < space->index_count; j++)
			index_build((MemtxIndex *) space->index[j], pk);

		if (n_tuples > 0) {
			say_info("Space '%s': done", space_name(space));
		}
	}
	handler->replace = memtx_replace_all_keys;
}

MemtxEngine::MemtxEngine()
	:Engine("memtx"),
	m_checkpoint(0),
	m_state(MEMTX_INITIALIZED)
{
	flags = ENGINE_CAN_BE_TEMPORARY;
}

/**
 * Read a snapshot and call apply_row for every snapshot row.
 * Panic in case of error.
 *
 * @pre there is an existing snapshot. Otherwise
 * recovery_bootstrap() should be used instead.
 */
void
recover_snap(struct recovery *r)
{
	/* There's no current_wal during initial recover. */
	assert(r->current_wal == NULL);
	say_info("recovery start");
	/**
	 * Don't rescan the directory, it's done when
	 * recovery is initialized.
	 */
	struct vclock *res = vclockset_last(&r->snap_dir.index);
	/*
	 * The only case when the directory index is empty is
	 * when someone has deleted a snapshot and tries to join
	 * as a replica. Our best effort is to not crash in such case.
	 */
	if (res == NULL)
		tnt_raise(ClientError, ER_MISSING_SNAPSHOT);
	int64_t signature = vclock_sum(res);

	struct xlog *snap = xlog_open(&r->snap_dir, signature);
	auto guard = make_scoped_guard([=]{ xlog_close(snap); });
	/* Save server UUID */
	r->server_uuid = snap->server_uuid;

	/* Add a surrogate server id for snapshot rows */
	vclock_add_server(&r->vclock, 0);

	say_info("recovering from `%s'", snap->filename);
	recover_xlog(r, snap);
}

/** Called at start to tell memtx to recover to a given LSN. */
void
MemtxEngine::recoverToCheckpoint(int64_t /* lsn */)
{
	struct recovery *r = ::recovery;
	assert(m_state == MEMTX_INITIALIZED);
	/*
	 * By default, enable fast start: bulk read of tuples
	 * from the snapshot, in which they are stored in key
	 * order, and bulk build of the primary key.
	 *
	 * If panic_on_snap_error = false, it's a disaster
	 * recovery mode. Enable all keys on start, to detect and
	 * discard duplicates in the snapshot.
	 */
	m_state = (r->snap_dir.panic_if_error ?
		   MEMTX_READING_SNAPSHOT : MEMTX_OK);

	/* Process existing snapshot */
	recover_snap(r);
	/* Replace server vclock using the data from snapshot */
	vclock_copy(&r->vclock, vclockset_last(&r->snap_dir.index));

	if (m_state == MEMTX_READING_SNAPSHOT) {
		/* End of the fast path: loaded the primary key. */
		space_foreach(memtx_end_build_primary_key, this);

		if (r->wal_dir.panic_if_error) {
			/*
			 * Fast start path: "play out" WAL
			 * records using the primary key only,
			 * then bulk-build all secondary keys.
			 */
			m_state = MEMTX_READING_WAL;
		} else {
			/*
			 * If panic_on_wal_error = false, it's
			 * a disaster recovery mode. Build
			 * secondary keys before reading the WAL,
			 * to detect and discard duplicates in
			 * unique keys.
			 */
			m_state = MEMTX_OK;
			space_foreach(memtx_build_secondary_keys, this);
		}
	}
}

void
MemtxEngine::endRecovery()
{
	/*
	 * Recovery is started with enabled keys when:
	 * - either of panic_on_snap_error/panic_on_wal_error
	 *   is true
	 * - it's a replication join
	 */
	if (m_state != MEMTX_OK) {
		m_state = MEMTX_OK;
		space_foreach(memtx_build_secondary_keys, this);
	}
}

Handler *MemtxEngine::open()
{
	return new MemtxSpace(this);
}


static void
memtx_add_primary_key(struct space *space, enum memtx_recovery_state state)
{
	struct MemtxSpace *handler = (struct MemtxSpace *) space->handler;
	switch (state) {
	case MEMTX_INITIALIZED:
		panic("can't create a new space before snapshot recovery");
		break;
	case MEMTX_READING_SNAPSHOT:
		((MemtxIndex *) space->index[0])->beginBuild();
		handler->replace = memtx_replace_build_next;
		break;
	case MEMTX_READING_WAL:
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
MemtxEngine::addPrimaryKey(struct space *space)
{
	memtx_add_primary_key(space, m_state);
}

void
MemtxEngine::dropPrimaryKey(struct space *space)
{
	struct MemtxSpace *handler = (struct MemtxSpace *) space->handler;
	handler->replace = memtx_replace_no_keys;
}

void
MemtxEngine::initSystemSpace(struct space *space)
{
	memtx_add_primary_key(space, MEMTX_OK);
}

bool
MemtxEngine::needToBuildSecondaryKey(struct space *space)
{
	struct MemtxSpace *handler = (struct MemtxSpace *) space->handler;
	return handler->replace == memtx_replace_all_keys;
}

Index *
MemtxEngine::createIndex(struct key_def *key_def)
{
	switch (key_def->type) {
	case HASH:
		return new MemtxHash(key_def);
	case TREE:
		return new MemtxTree(key_def);
	case RTREE:
		return new MemtxRTree(key_def);
	case BITSET:
		return new MemtxBitset(key_def);
	default:
		assert(false);
		return NULL;
	}
}

void
MemtxEngine::dropIndex(Index *index)
{
	struct iterator *it = ((MemtxIndex*) index)->position();
	index->initIterator(it, ITER_ALL, NULL, 0);
	struct tuple *tuple;
	while ((tuple = it->next(it)))
		tuple_unref(tuple);
}

void
MemtxEngine::keydefCheck(struct space *space, struct key_def *key_def)
{
	switch (key_def->type) {
	case HASH:
		if (! key_def->opts.is_unique) {
			tnt_raise(ClientError, ER_MODIFY_INDEX,
				  key_def->name,
				  space_name(space),
				  "HASH index must be unique");
		}
		break;
	case TREE:
		/* TREE index has no limitations. */
		break;
	case RTREE:
		if (key_def->part_count != 1) {
			tnt_raise(ClientError, ER_MODIFY_INDEX,
				  key_def->name,
				  space_name(space),
				  "RTREE index key can not be multipart");
		}
		if (key_def->opts.is_unique) {
			tnt_raise(ClientError, ER_MODIFY_INDEX,
				  key_def->name,
				  space_name(space),
				  "RTREE index can not be unique");
		}
		break;
	case BITSET:
		if (key_def->part_count != 1) {
			tnt_raise(ClientError, ER_MODIFY_INDEX,
				  key_def->name,
				  space_name(space),
				  "BITSET index key can not be multipart");
		}
		if (key_def->opts.is_unique) {
			tnt_raise(ClientError, ER_MODIFY_INDEX,
				  key_def->name,
				  space_name(space),
				  "BITSET can not be unique");
		}
		break;
	default:
		tnt_raise(ClientError, ER_INDEX_TYPE,
			  key_def->name,
			  space_name(space));
		break;
	}
	for (uint32_t i = 0; i < key_def->part_count; i++) {
		switch (key_def->parts[i].type) {
		case NUM:
		case STRING:
			if (key_def->type == RTREE) {
				tnt_raise(ClientError, ER_MODIFY_INDEX,
					  key_def->name,
					  space_name(space),
					  "RTREE index field type must be ARRAY");
			}
			break;
		case ARRAY:
			if (key_def->type != RTREE) {
				tnt_raise(ClientError, ER_MODIFY_INDEX,
					  key_def->name,
					  space_name(space),
					  "ARRAY field type is not supported");
			}
			break;
		default:
			assert(false);
			break;
		}
	}
}

void
MemtxEngine::prepare(struct txn *txn)
{
	if (txn->is_autocommit)
		return;
	/*
	 * These triggers are only used for memtx and only
	 * when autocommit == false, so we are saving
	 * on calls to trigger_create/trigger_clear.
	 */
	trigger_clear(&txn->fiber_on_yield);
	trigger_clear(&txn->fiber_on_stop);
}

void
MemtxEngine::begin(struct txn *txn)
{
	/*
	 * Register a trigger to rollback transaction on yield.
	 * This must be done in begin(), since it's
	 * the first thing txn invokes after txn->n_stmts++,
	 * to match with trigger_clear() in rollbackStatement().
	 */
	if (txn->is_autocommit == false) {

		txn->fiber_on_yield = {
			RLIST_LINK_INITIALIZER, txn_on_yield_or_stop, NULL, NULL
		};
		txn->fiber_on_stop = {
			RLIST_LINK_INITIALIZER, txn_on_yield_or_stop, NULL, NULL
		};
		/*
		 * Memtx doesn't allow yields between statements of
		 * a transaction. Set a trigger which would roll
		 * back the transaction if there is a yield.
		 */
		trigger_add(&fiber()->on_yield, &txn->fiber_on_yield);
		trigger_add(&fiber()->on_stop, &txn->fiber_on_stop);
	}
}

void
MemtxEngine::rollbackStatement(struct txn_stmt *stmt)
{
	if (stmt->old_tuple == NULL && stmt->new_tuple == NULL)
		return;
	struct space *space = stmt->space;
	int index_count;
	struct MemtxSpace *handler = (struct MemtxSpace *) space->handler;

	if (handler->replace == memtx_replace_all_keys)
		index_count = space->index_count;
	else if (handler->replace == memtx_replace_primary_key)
		index_count = 1;
	else
		panic("transaction rolled back during snapshot recovery");

	for (int i = 0; i < index_count; i++) {
		Index *index = space->index[i];
		index->replace(stmt->new_tuple, stmt->old_tuple, DUP_INSERT);
	}
	if (stmt->new_tuple)
		tuple_unref(stmt->new_tuple);

	stmt->old_tuple = NULL;
	stmt->new_tuple = NULL;
}

void
MemtxEngine::rollback(struct txn *txn)
{
	prepare(txn);
	struct txn_stmt *stmt;
	stailq_reverse(&txn->stmts);
	stailq_foreach_entry(stmt, &txn->stmts, next)
		rollbackStatement(stmt);
}

void
MemtxEngine::commit(struct txn *txn, int64_t signature)
{
	(void) signature;
	struct txn_stmt *stmt;
	stailq_foreach_entry(stmt, &txn->stmts, next) {
		if (stmt->old_tuple)
			tuple_unref(stmt->old_tuple);
	}
}

void
MemtxEngine::beginJoin()
{
	m_state = MEMTX_OK;
}

static void
checkpoint_write_row(struct xlog *l, struct xrow_header *row,
		     uint64_t snap_io_rate_limit)
{
	static uint64_t bytes;
	ev_tstamp elapsed;
	static ev_tstamp last = 0;
	ev_loop *loop = loop();

	row->tm = last;
	row->server_id = 0;
	/**
	 * Rows in snapshot are numbered from 1 to %rows.
	 * This makes streaming such rows to a replica or
	 * to recovery look similar to streaming a normal
	 * WAL. @sa the place which skips old rows in
	 * recovery_apply_row().
	 */
	row->lsn = ++l->rows;
	row->sync = 0; /* don't write sync to wal */

	struct iovec iov[XROW_IOVMAX];
	int iovcnt = xlog_encode_row(row, iov);

	/* TODO: use writev here */
	for (int i = 0; i < iovcnt; i++) {
		if (fwrite(iov[i].iov_base, iov[i].iov_len, 1, l->f) != 1) {
			say_syserror("Can't write row (%zu bytes)",
				     iov[i].iov_len);
			tnt_raise(SystemError, "fwrite");
		}
		bytes += iov[i].iov_len;
	}

	if (l->rows % 100000 == 0)
		say_crit("%.1fM rows written", l->rows / 1000000.);

	fiber_gc();

	if (snap_io_rate_limit != UINT64_MAX) {
		if (last == 0) {
			/*
			 * Remember the time of first
			 * write to disk.
			 */
			ev_now_update(loop);
			last = ev_now(loop);
		}
		/**
		 * If io rate limit is set, flush the
		 * filesystem cache, otherwise the limit is
		 * not really enforced.
		 */
		if (bytes > snap_io_rate_limit)
			fdatasync(fileno(l->f));
	}
	while (bytes > snap_io_rate_limit) {
		ev_now_update(loop);
		/*
		 * How much time have passed since
		 * last write?
		 */
		elapsed = ev_now(loop) - last;
		/*
		 * If last write was in less than
		 * a second, sleep until the
		 * second is reached.
		 */
		if (elapsed < 1)
			usleep(((1 - elapsed) * 1000000));

		ev_now_update(loop);
		last = ev_now(loop);
		bytes -= snap_io_rate_limit;
	}
}

static void
checkpoint_write_tuple(struct xlog *l, uint32_t n, struct tuple *tuple,
		       uint64_t snap_io_rate_limit)
{
	struct request_replace_body body;
	body.m_body = 0x82; /* map of two elements. */
	body.k_space_id = IPROTO_SPACE_ID;
	body.m_space_id = 0xce; /* uint32 */
	body.v_space_id = mp_bswap_u32(n);
	body.k_tuple = IPROTO_TUPLE;

	struct xrow_header row;
	memset(&row, 0, sizeof(struct xrow_header));
	row.type = IPROTO_INSERT;

	row.bodycnt = 2;
	row.body[0].iov_base = &body;
	row.body[0].iov_len = sizeof(body);
	row.body[1].iov_base = tuple->data;
	row.body[1].iov_len = tuple->bsize;
	checkpoint_write_row(l, &row, snap_io_rate_limit);
}

struct checkpoint_entry {
	struct space *space;
	struct iterator *iterator;
	struct rlist link;
};

struct checkpoint {
	/**
	 * List of MemTX spaces to snapshot, with consistent
	 * read view iterators.
	 */
	struct rlist entries;
	/** The signature of the snapshot file (lsn sum) */
	int64_t lsn;
	uint64_t snap_io_rate_limit;
	struct cord cord;
	bool waiting_for_snap_thread;
	struct vclock vclock;
	struct xdir dir;
};

static void
checkpoint_init(struct checkpoint *ckpt, struct recovery *recovery,
		int64_t lsn_arg)
{
	ckpt->entries = RLIST_HEAD_INITIALIZER(ckpt->entries);
	ckpt->waiting_for_snap_thread = false;
	ckpt->lsn = lsn_arg;
	vclock_copy(&ckpt->vclock, &recovery->vclock);
	xdir_create(&ckpt->dir, recovery->snap_dir.dirname, SNAP,
		    &recovery->server_uuid);
	ckpt->snap_io_rate_limit = recovery->snap_io_rate_limit;
}

static void
checkpoint_destroy(struct checkpoint *ckpt)
{
	struct checkpoint_entry *entry;
	rlist_foreach_entry(entry, &ckpt->entries, link) {
		Index *pk = space_index(entry->space, 0);
		pk->destroyReadViewForIterator(entry->iterator);
		entry->iterator->free(entry->iterator);
	}
	ckpt->entries = RLIST_HEAD_INITIALIZER(ckpt->entries);
	xdir_destroy(&ckpt->dir);
}


static void
checkpoint_add_space(struct space *sp, void *data)
{
	if (space_is_temporary(sp))
		return;
	if (!space_is_memtx(sp))
		return;
	Index *pk = space_index(sp, 0);
	if (!pk)
		return;
	struct checkpoint *ckpt = (struct checkpoint *)data;
	struct checkpoint_entry *entry;
	entry = (struct checkpoint_entry *)
		region_alloc_xc(&fiber()->gc, sizeof(*entry));
	rlist_add_tail_entry(&ckpt->entries, entry, link);

	entry->space = sp;
	entry->iterator = pk->allocIterator();

	pk->initIterator(entry->iterator, ITER_ALL, NULL, 0);
	pk->createReadViewForIterator(entry->iterator);
};

void
checkpoint_f(va_list ap)
{
	struct checkpoint *ckpt = va_arg(ap, struct checkpoint *);

	struct xlog *snap = xlog_create(&ckpt->dir, &ckpt->vclock);

	if (snap == NULL)
		tnt_raise(SystemError, "xlog_open");

	auto guard = make_scoped_guard([=]{ xlog_close(snap); });

	say_info("saving snapshot `%s'", snap->filename);
	struct checkpoint_entry *entry;
	rlist_foreach_entry(entry, &ckpt->entries, link) {
		struct tuple *tuple;
		struct iterator *it = entry->iterator;
		for (tuple = it->next(it); tuple; tuple = it->next(it)) {
			checkpoint_write_tuple(snap, space_id(entry->space),
					       tuple, ckpt->snap_io_rate_limit);
		}
	}
	say_info("done");
}

int
MemtxEngine::beginCheckpoint(int64_t lsn)
{
	assert(m_checkpoint == 0);

	m_checkpoint = (struct checkpoint *)
		region_alloc_xc(&fiber()->gc, sizeof(*m_checkpoint));

	checkpoint_init(m_checkpoint, ::recovery, lsn);
	space_foreach(checkpoint_add_space, m_checkpoint);

	if (cord_costart(&m_checkpoint->cord, "snapshot",
			 checkpoint_f, m_checkpoint)) {
		return -1;
	}
	m_checkpoint->waiting_for_snap_thread = true;

	/* increment snapshot version; set tuple deletion to delayed mode */
	tuple_begin_snapshot();
	return 0;
}

int
MemtxEngine::waitCheckpoint()
{
	assert(m_checkpoint);
	assert(m_checkpoint->waiting_for_snap_thread);

	/* wait for memtx-part snapshot completion */
	int result = cord_cojoin(&m_checkpoint->cord);

	struct error *e = diag_last_error(&fiber()->diag);
	if (e != NULL) {
		error_log(e);
		result = -1;
		SystemError *se = type_cast(SystemError, e);
		if (se)
			errno = se->get_errno();
	}

	m_checkpoint->waiting_for_snap_thread = false;
	return result;
}

void
MemtxEngine::commitCheckpoint()
{
	/* beginCheckpoint() must have been done */
	assert(m_checkpoint);
	/* waitCheckpoint() must have been done. */
	assert(!m_checkpoint->waiting_for_snap_thread);

	tuple_end_snapshot();

	struct xdir *dir = &m_checkpoint->dir;
	/* rename snapshot on completion */
	char to[PATH_MAX];
	snprintf(to, sizeof(to), "%s",
		 format_filename(dir, m_checkpoint->lsn, NONE));
	char *from = format_filename(dir, m_checkpoint->lsn, INPROGRESS);
	int rc = coeio_rename(from, to);
	if (rc != 0)
		panic("can't rename .snap.inprogress");

	checkpoint_destroy(m_checkpoint);
	m_checkpoint = 0;
}

void
MemtxEngine::abortCheckpoint()
{
	/**
	 * An error in the other engine's first phase.
	 */
	if (m_checkpoint->waiting_for_snap_thread) {
		/* wait for memtx-part snapshot completion */
		cord_cojoin(&m_checkpoint->cord);

		struct error *e = diag_last_error(&fiber()->diag);
		if (e)
			error_log(e);
		m_checkpoint->waiting_for_snap_thread = false;
	}

	tuple_end_snapshot();

	/** Remove garbage .inprogress file. */
	char *filename = format_filename(&m_checkpoint->dir,
					 m_checkpoint->lsn,
					 INPROGRESS);
	(void) coeio_unlink(filename);

	checkpoint_destroy(m_checkpoint);
	m_checkpoint = 0;
}

void
MemtxEngine::join(struct relay *relay)
{
	recover_snap(relay->r);
}

/**
 * Initialize arena for indexes.
 * The arena is used for memtx_index_extent_alloc
 *  and memtx_index_extent_free.
 * Can be called several times, only first call do the work.
 */
void
memtx_index_arena_init()
{
	if (memtx_index_arena_initialized) {
		/* already done.. */
		return;
	}
	/* Creating slab cache */
	slab_cache_create(&memtx_index_slab_cache, &memtx_arena);
	/* Creating mempool */
	mempool_create(&memtx_index_extent_pool,
		       &memtx_index_slab_cache,
		       MEMTX_EXTENT_SIZE);
	/* Empty reserved list */
	memtx_index_num_reserved_extents = 0;
	memtx_index_reserved_extents = 0;
	/* Done */
	memtx_index_arena_initialized = true;
}

/**
 * Allocate a block of size MEMTX_EXTENT_SIZE for memtx index
 */
void *
memtx_index_extent_alloc()
{
	if (memtx_index_reserved_extents) {
		assert(memtx_index_num_reserved_extents > 0);
		memtx_index_num_reserved_extents--;
		void *result = memtx_index_reserved_extents;
		memtx_index_reserved_extents = *(void **)
			memtx_index_reserved_extents;
		return result;
	}
	ERROR_INJECT(ERRINJ_INDEX_ALLOC,
		     /* same error as in mempool_alloc */
		     tnt_raise(OutOfMemory, MEMTX_EXTENT_SIZE,
			       "mempool", "new slab")
		    );
	return mempool_alloc_xc(&memtx_index_extent_pool);
}

/**
 * Free a block previously allocated by memtx_index_extent_alloc
 */
void
memtx_index_extent_free(void *extent)
{
	return mempool_free(&memtx_index_extent_pool, extent);
}

/**
 * Reserve num extents in pool.
 * Ensure that next num extent_alloc will succeed w/o an error
 */
void
memtx_index_extent_reserve(int num)
{
	ERROR_INJECT(ERRINJ_INDEX_ALLOC,
		     /* same error as in mempool_alloc */
		     tnt_raise(OutOfMemory, MEMTX_EXTENT_SIZE,
			       "mempool", "new slab")
		    );
	while (memtx_index_num_reserved_extents < num) {
		void *ext = mempool_alloc_xc(&memtx_index_extent_pool);
		*(void **)ext = memtx_index_reserved_extents;
		memtx_index_reserved_extents = ext;
		memtx_index_num_reserved_extents++;
	}
}
