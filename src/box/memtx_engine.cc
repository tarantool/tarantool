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
#include "memtx_engine.h"
#include "memtx_space.h"
#include "memtx_tuple.h"

#include "coio_file.h"
#include "scoped_guard.h"

#include "tuple.h"
#include "txn.h"
#include "memtx_tree.h"
#include "iproto_constants.h"
#include "xrow.h"
#include "xstream.h"
#include "bootstrap.h"
#include "replication.h"
#include "schema.h"

#include "gc.h"

/** For all memory used by all indexes.
 * If you decide to use memtx_index_arena or
 * memtx_index_slab_cache for anything other than
 * memtx_index_extent_pool, make sure this is reflected in
 * box.slab.info(), @sa lua/slab.cc
 */
extern struct quota memtx_quota;
static bool memtx_index_arena_initialized = false;
struct slab_arena memtx_arena; /* used by memtx_tuple.cc */
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

static void
txn_on_yield_or_stop(struct trigger * /* trigger */, void * /* event */)
{
	txn_rollback(); /* doesn't throw */
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

MemtxEngine::MemtxEngine(const char *snap_dirname, bool force_recovery,
			 uint64_t tuple_arena_max_size, uint32_t objsize_min,
			 uint32_t objsize_max, float alloc_factor)
	:Engine("memtx", &memtx_tuple_format_vtab),
	m_state(MEMTX_INITIALIZED),
	m_checkpoint(0),
	m_snap_io_rate_limit(0),
	m_force_recovery(force_recovery)
{
	memtx_tuple_init(tuple_arena_max_size, objsize_min, objsize_max,
			 alloc_factor);

	xdir_create(&m_snap_dir, snap_dirname, SNAP, &INSTANCE_UUID);
	m_snap_dir.force_recovery = force_recovery;
	xdir_scan_xc(&m_snap_dir);
}

MemtxEngine::~MemtxEngine()
{
	xdir_destroy(&m_snap_dir);

	memtx_tuple_free();
}

void
MemtxEngine::recoverSnapshot(const struct vclock *vclock)
{
	/* Process existing snapshot */
	say_info("recovery start");
	int64_t signature = vclock_sum(vclock);
	const char *filename = xdir_format_filename(&m_snap_dir, signature,
						    NONE);

	say_info("recovering from `%s'", filename);
	struct xlog_cursor cursor;
	xlog_cursor_open_xc(&cursor, filename);
	INSTANCE_UUID = cursor.meta.instance_uuid;
	auto reader_guard = make_scoped_guard([&]{
		xlog_cursor_close(&cursor, false);
	});

	struct xrow_header row;
	uint64_t row_count = 0;
	while (xlog_cursor_next_xc(&cursor, &row, m_force_recovery) == 0) {
		row.lsn = signature;
		try {
			recoverSnapshotRow(&row);
		} catch (ClientError *e) {
			if (!m_force_recovery)
				throw;
			say_error("can't apply row: ");
			e->log();
		}
		++row_count;
		if (row_count % 100000 == 0) {
			say_info("%.1fM rows processed",
				 row_count / 1000000.);
			fiber_yield_timeout(0);
		}
	}

	/**
	 * We should never try to read snapshots with no EOF
	 * marker - such snapshots are very likely corrupted and
	 * should not be trusted.
	 */
	if (cursor.state != XLOG_CURSOR_EOF)
		panic("snapshot `%s' has no EOF marker", filename);

}

void
MemtxEngine::recoverSnapshotRow(struct xrow_header *row)
{
	assert(row->bodycnt == 1); /* always 1 for read */
	if (row->type != IPROTO_INSERT) {
		tnt_raise(ClientError, ER_UNKNOWN_REQUEST_TYPE,
			  (uint32_t) row->type);
	}

	struct request *request = xrow_decode_request_xc(row);
	struct space *space = space_cache_find(request->space_id);
	/* memtx snapshot must contain only memtx spaces */
	if (space->handler->engine != this)
		tnt_raise(ClientError, ER_CROSS_ENGINE_TRANSACTION);
	/* no access checks here - applier always works with admin privs */
	space->handler->applyInitialJoinRow(space, request);
	/*
	 * Don't let gc pool grow too much. Yet to
	 * it before reading the next row, to make
	 * sure it's not freed along here.
	 */
	fiber_gc();
}

/** Called at start to tell memtx to recover to a given LSN. */
void
MemtxEngine::beginInitialRecovery(const struct vclock *)
{
	assert(m_state == MEMTX_INITIALIZED);
	/*
	 * By default, enable fast start: bulk read of tuples
	 * from the snapshot, in which they are stored in key
	 * order, and bulk build of the primary key.
	 *
	 * If force_recovery = true, it's a disaster
	 * recovery mode. Enable all keys on start, to detect and
	 * discard duplicates in the snapshot.
	 */
	m_state = (m_force_recovery ? MEMTX_OK : MEMTX_INITIAL_RECOVERY);
}

void
MemtxEngine::beginFinalRecovery()
{
	if (m_state == MEMTX_OK)
		return;

	assert(m_state == MEMTX_INITIAL_RECOVERY);
	/* End of the fast path: loaded the primary key. */
	space_foreach(memtx_end_build_primary_key, this);

	if (!m_force_recovery) {
		/*
		 * Fast start path: "play out" WAL
		 * records using the primary key only,
		 * then bulk-build all secondary keys.
		 */
		m_state = MEMTX_FINAL_RECOVERY;
	} else {
		/*
		 * If force_recovery = true, it's
		 * a disaster recovery mode. Build
		 * secondary keys before reading the WAL,
		 * to detect and discard duplicates in
		 * unique keys.
		 */
		m_state = MEMTX_OK;
		space_foreach(memtx_build_secondary_keys, this);
	}
}

void
MemtxEngine::endRecovery()
{
	/*
	 * Recovery is started with enabled keys when:
	 * - either of force_recovery
	 *   is false
	 * - it's a replication join
	 */
	if (m_state != MEMTX_OK) {
		assert(m_state == MEMTX_FINAL_RECOVERY);
		m_state = MEMTX_OK;
		space_foreach(memtx_build_secondary_keys, this);
	}
}

Handler *MemtxEngine::createSpace()
{
	return new MemtxSpace(this);
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

		trigger_create(&txn->fiber_on_yield, txn_on_yield_or_stop,
				NULL, NULL);
		trigger_create(&txn->fiber_on_stop, txn_on_yield_or_stop,
				NULL, NULL);
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
MemtxEngine::rollbackStatement(struct txn *, struct txn_stmt *stmt)
{
	if (stmt->old_tuple == NULL && stmt->new_tuple == NULL)
		return;
	struct space *space = stmt->space;
	int index_count;
	struct MemtxSpace *handler = (struct MemtxSpace *) space->handler;

	/* Only roll back the changes if they were made. */
	if (stmt->engine_savepoint == NULL)
		index_count = 0;
	else if (handler->replace == memtx_replace_all_keys)
		index_count = space->index_count;
	else if (handler->replace == memtx_replace_primary_key)
		index_count = 1;
	else
		panic("transaction rolled back during snapshot recovery");

	for (int i = 0; i < index_count; i++) {
		Index *index = space->index[i];
		index->replace(stmt->new_tuple, stmt->old_tuple, DUP_INSERT);
	}
	/** Rollback change of bsize */
	space_bsize_rollback(space, stmt->bsize_change);

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
		rollbackStatement(txn, stmt);
}

void
MemtxEngine::commit(struct txn *txn)
{
	struct txn_stmt *stmt;
	stailq_foreach_entry(stmt, &txn->stmts, next) {
		if (stmt->old_tuple)
			tuple_unref(stmt->old_tuple);
	}
}

void
MemtxEngine::bootstrap()
{
	assert(m_state == MEMTX_INITIALIZED);
	m_state = MEMTX_OK;

	/* Recover from bootstrap.snap */
	say_info("initializing an empty data directory");
	struct xdir dir;
	xdir_create(&dir, "", SNAP, &uuid_nil);
	struct xlog_cursor cursor;
	if (xlog_cursor_openmem(&cursor, (const char *)bootstrap_bin,
				sizeof(bootstrap_bin), "bootstrap") < 0) {
		diag_raise();
	};
	auto guard = make_scoped_guard([&]{
		xlog_cursor_close(&cursor, false);
		xdir_destroy(&dir);
	});

	struct xrow_header row;
	while (xlog_cursor_next_xc(&cursor, &row, true) == 0)
		recoverSnapshotRow(&row);
}

static void
checkpoint_write_row(struct xlog *l, struct xrow_header *row)
{
	static ev_tstamp last = 0;
	if (last == 0) {
		ev_now_update(loop());
		last = ev_now(loop());
	}

	row->tm = last;
	row->replica_id = 0;
	/**
	 * Rows in snapshot are numbered from 1 to %rows.
	 * This makes streaming such rows to a replica or
	 * to recovery look similar to streaming a normal
	 * WAL. @sa the place which skips old rows in
	 * recovery_apply_row().
	 */
	row->lsn = l->rows + l->tx_rows;
	row->sync = 0; /* don't write sync to wal */

	ssize_t written = xlog_write_row(l, row);
	fiber_gc();
	if (written < 0) {
		diag_raise();
	}

	if ((l->rows + l->tx_rows) % 100000 == 0)
		say_crit("%.1fM rows written", (l->rows + l->tx_rows) / 1000000.0);

}

static void
checkpoint_write_tuple(struct xlog *l, uint32_t n, struct tuple *tuple)
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
	uint32_t bsize;
	row.body[1].iov_base = (char *) tuple_data_range(tuple, &bsize);
	row.body[1].iov_len = bsize;
	checkpoint_write_row(l, &row);
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
	uint64_t snap_io_rate_limit;
	struct cord cord;
	bool waiting_for_snap_thread;
	/** The vclock of the snapshot file. */
	struct vclock *vclock;
	struct xdir dir;
	/**
	 * Do nothing, just touch the snapshot file - the
	 * checkpoint already exists.
	 */
	bool touch;
};

static void
checkpoint_init(struct checkpoint *ckpt, const char *snap_dirname,
		uint64_t snap_io_rate_limit)
{
	ckpt->entries = RLIST_HEAD_INITIALIZER(ckpt->entries);
	ckpt->waiting_for_snap_thread = false;
	xdir_create(&ckpt->dir, snap_dirname, SNAP, &INSTANCE_UUID);
	ckpt->snap_io_rate_limit = snap_io_rate_limit;
	/* May be used in abortCheckpoint() */
	ckpt->vclock = (struct vclock *) malloc(sizeof(*ckpt->vclock));
	if (ckpt->vclock == NULL)
		tnt_raise(OutOfMemory, sizeof(*ckpt->vclock),
			  "malloc", "vclock");
	vclock_create(ckpt->vclock);
	ckpt->touch = false;
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
	free(ckpt->vclock);
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
	entry = region_alloc_object_xc(&fiber()->gc, struct checkpoint_entry);
	rlist_add_tail_entry(&ckpt->entries, entry, link);

	entry->space = sp;
	entry->iterator = pk->allocIterator();

	pk->initIterator(entry->iterator, ITER_ALL, NULL, 0);
	pk->createReadViewForIterator(entry->iterator);
};

int
checkpoint_f(va_list ap)
{
	struct checkpoint *ckpt = va_arg(ap, struct checkpoint *);

	if (ckpt->touch) {
		if (xdir_touch_xlog(&ckpt->dir, ckpt->vclock) == 0)
			return 0;
		/*
		 * Failed to touch an existing snapshot, create
		 * a new one.
		 */
		ckpt->touch = false;
	}

	struct xlog snap;
	if (xdir_create_xlog(&ckpt->dir, &snap, ckpt->vclock) != 0)
		diag_raise();

	auto guard = make_scoped_guard([&]{ xlog_close(&snap, false); });
	snap.rate_limit = ckpt->snap_io_rate_limit;

	say_info("saving snapshot `%s'", snap.filename);
	struct checkpoint_entry *entry;
	rlist_foreach_entry(entry, &ckpt->entries, link) {
		struct tuple *tuple;
		struct iterator *it = entry->iterator;
		for (tuple = it->next(it); tuple; tuple = it->next(it)) {
			checkpoint_write_tuple(&snap, space_id(entry->space),
					       tuple);
		}
	}
	xlog_flush(&snap);
	say_info("done");
	return 0;
}

int
MemtxEngine::beginCheckpoint()
{
	assert(m_checkpoint == 0);

	m_checkpoint = region_alloc_object_xc(&fiber()->gc, struct checkpoint);

	checkpoint_init(m_checkpoint, m_snap_dir.dirname, m_snap_io_rate_limit);
	space_foreach(checkpoint_add_space, m_checkpoint);

	/* increment snapshot version; set tuple deletion to delayed mode */
	memtx_tuple_begin_snapshot();
	return 0;
}

int
MemtxEngine::waitCheckpoint(struct vclock *vclock)
{
	assert(m_checkpoint);
	/*
	 * If a snapshot already exists, do not create a new one.
	 */
	struct vclock last;
	m_checkpoint->touch = (lastSnapshot(&last) >= 0 &&
			       vclock_compare(&last, vclock) == 0);
	vclock_copy(m_checkpoint->vclock, vclock);

	if (cord_costart(&m_checkpoint->cord, "snapshot",
			 checkpoint_f, m_checkpoint)) {
		return -1;
	}
	m_checkpoint->waiting_for_snap_thread = true;

	/* wait for memtx-part snapshot completion */
	int result = cord_cojoin(&m_checkpoint->cord);
	if (result != 0)
		error_log(diag_last_error(diag_get()));

	m_checkpoint->waiting_for_snap_thread = false;
	return result;
}

void
MemtxEngine::commitCheckpoint(struct vclock *vclock)
{
	(void) vclock;
	/* beginCheckpoint() must have been done */
	assert(m_checkpoint);
	/* waitCheckpoint() must have been done. */
	assert(!m_checkpoint->waiting_for_snap_thread);

	memtx_tuple_end_snapshot();

	if (!m_checkpoint->touch) {
		int64_t lsn = vclock_sum(m_checkpoint->vclock);
		struct xdir *dir = &m_checkpoint->dir;
		/* rename snapshot on completion */
		char to[PATH_MAX];
		snprintf(to, sizeof(to), "%s",
			 xdir_format_filename(dir, lsn, NONE));
		char *from = xdir_format_filename(dir, lsn, INPROGRESS);
		int rc = coio_rename(from, to);
		if (rc != 0)
			panic("can't rename .snap.inprogress");
	}

	struct vclock last;
	if (lastSnapshot(&last) < 0 || vclock_compare(&last, vclock) != 0) {
		/* Add the new checkpoint to the set. */
		xdir_add_vclock(&m_snap_dir, m_checkpoint->vclock);
		/* Prevent checkpoint_destroy() from freeing vclock. */
		m_checkpoint->vclock = NULL;
	}

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
		if (cord_cojoin(&m_checkpoint->cord) != 0)
			error_log(diag_last_error(diag_get()));
		m_checkpoint->waiting_for_snap_thread = false;
	}

	memtx_tuple_end_snapshot();

	/** Remove garbage .inprogress file. */
	char *filename =
		xdir_format_filename(&m_checkpoint->dir,
				     vclock_sum(m_checkpoint->vclock),
				     INPROGRESS);
	(void) coio_unlink(filename);

	checkpoint_destroy(m_checkpoint);
	m_checkpoint = 0;
}

int
MemtxEngine::collectGarbage(int64_t lsn)
{
	/*
	 * We recover the checkpoint list by scanning the snapshot
	 * directory so deletion of an xlog file or a file that
	 * belongs to another engine without the corresponding snap
	 * file would result in a corrupted checkpoint on the list.
	 * That said, we have to abort garbage collection if we
	 * fail to delete a snap file.
	 */
	if (xdir_collect_garbage(&m_snap_dir, lsn, true) != 0)
		return -1;

	return 0;
}

int
MemtxEngine::backup(struct vclock *vclock, engine_backup_cb cb, void *cb_arg)
{
	char *filename = xdir_format_filename(&m_snap_dir,
					      vclock_sum(vclock), NONE);
	return cb(filename, cb_arg);
}

/** Used to pass arguments to memtx_initial_join_f */
struct memtx_join_arg {
	const char *snap_dirname;
	int64_t checkpoint_lsn;
	struct xstream *stream;
};

/**
 * Invoked from a thread to feed snapshot rows.
 */
static int
memtx_initial_join_f(va_list ap)
{
	struct memtx_join_arg *arg = va_arg(ap, struct memtx_join_arg *);
	const char *snap_dirname = arg->snap_dirname;
	int64_t checkpoint_lsn = arg->checkpoint_lsn;
	struct xstream *stream = arg->stream;

	struct xdir dir;
	/*
	 * snap_dirname and INSTANCE_UUID don't change after start,
	 * safe to use in another thread.
	 */
	xdir_create(&dir, snap_dirname, SNAP, &INSTANCE_UUID);
	auto guard = make_scoped_guard([&]{
		xdir_destroy(&dir);
	});
	struct xlog_cursor cursor;
	xdir_open_cursor_xc(&dir, checkpoint_lsn, &cursor);
	auto reader_guard = make_scoped_guard([&]{
		xlog_cursor_close(&cursor, false);
	});

	struct xrow_header row;
	while (xlog_cursor_next_xc(&cursor, &row, true) == 0) {
		xstream_write_xc(stream, &row);
	}

	/**
	 * We should never try to read snapshots with no EOF
	 * marker - such snapshots are very likely corrupted and
	 * should not be trusted.
	 */
	/* TODO: replace panic with tnt_raise() */
	if (cursor.state != XLOG_CURSOR_EOF)
		panic("snapshot `%s' has no EOF marker",
		      cursor.name);
	return 0;
}

void
MemtxEngine::join(struct vclock *vclock, struct xstream *stream)
{
	/*
	 * cord_costart() passes only void * pointer as an argument.
	 */
	struct memtx_join_arg arg = {
		/* .snap_dirname   = */ m_snap_dir.dirname,
		/* .checkpoint_lsn = */ vclock_sum(vclock),
		/* .stream         = */ stream
	};

	/* Send snapshot using a thread */
	struct cord cord;
	cord_costart(&cord, "initial_join", memtx_initial_join_f, &arg);
	if (cord_cojoin(&cord) != 0)
		diag_raise();
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
memtx_index_extent_alloc(void *ctx)
{
	(void)ctx;
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
memtx_index_extent_free(void *ctx, void *extent)
{
	(void)ctx;
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
