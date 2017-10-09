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
	struct memtx_space *memtx_space = (struct memtx_space *)space;
	if (space->engine != param || space_index(space, 0) == NULL ||
	    memtx_space->replace == memtx_space_replace_all_keys)
		return;

	index_end_build(space->index[0]);
	memtx_space->replace = memtx_space_replace_primary_key;
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
	struct memtx_space *memtx_space = (struct memtx_space *)space;
	if (space->engine != param || space_index(space, 0) == NULL ||
	    memtx_space->replace == memtx_space_replace_all_keys)
		return;

	if (space->index_id_max > 0) {
		struct index *pk = space->index[0];
		uint32_t n_tuples = index_size_xc(pk);

		if (n_tuples > 0) {
			say_info("Building secondary indexes in space '%s'...",
				 space_name(space));
		}

		for (uint32_t j = 1; j < space->index_count; j++)
			index_build_xc(space->index[j], pk);

		if (n_tuples > 0) {
			say_info("Space '%s': done", space_name(space));
		}
	}
	memtx_space->replace = memtx_space_replace_all_keys;
}

static void
memtx_engine_shutdown(struct engine *engine)
{
	struct memtx_engine *memtx = (struct memtx_engine *)engine;
	xdir_destroy(&memtx->snap_dir);
	free(memtx);
	memtx_tuple_free();
}

static void
memtx_engine_recover_snapshot_row(struct memtx_engine *memtx,
				  struct xrow_header *row);

void
memtx_engine_recover_snapshot(struct memtx_engine *memtx,
			      const struct vclock *vclock)
{
	/* Process existing snapshot */
	say_info("recovery start");
	int64_t signature = vclock_sum(vclock);
	const char *filename = xdir_format_filename(&memtx->snap_dir,
						    signature, NONE);

	say_info("recovering from `%s'", filename);
	struct xlog_cursor cursor;
	xlog_cursor_open_xc(&cursor, filename);
	INSTANCE_UUID = cursor.meta.instance_uuid;
	auto reader_guard = make_scoped_guard([&]{
		xlog_cursor_close(&cursor, false);
	});

	struct xrow_header row;
	uint64_t row_count = 0;
	while (xlog_cursor_next_xc(&cursor, &row, memtx->force_recovery) == 0) {
		row.lsn = signature;
		try {
			memtx_engine_recover_snapshot_row(memtx, &row);
		} catch (ClientError *e) {
			if (!memtx->force_recovery)
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
	if (!xlog_cursor_is_eof(&cursor))
		panic("snapshot `%s' has no EOF marker", filename);

}

static void
memtx_engine_recover_snapshot_row(struct memtx_engine *memtx,
				  struct xrow_header *row)
{
	assert(row->bodycnt == 1); /* always 1 for read */
	if (row->type != IPROTO_INSERT) {
		tnt_raise(ClientError, ER_UNKNOWN_REQUEST_TYPE,
			  (uint32_t) row->type);
	}

	struct request *request = xrow_decode_dml_gc_xc(row);
	struct space *space = space_cache_find_xc(request->space_id);
	/* memtx snapshot must contain only memtx spaces */
	if (space->engine != (struct engine *)memtx)
		tnt_raise(ClientError, ER_CROSS_ENGINE_TRANSACTION);
	/* no access checks here - applier always works with admin privs */
	space_apply_initial_join_row_xc(space, request);
	/*
	 * Don't let gc pool grow too much. Yet to
	 * it before reading the next row, to make
	 * sure it's not freed along here.
	 */
	fiber_gc();
}

/** Called at start to tell memtx to recover to a given LSN. */
static void
memtx_engine_begin_initial_recovery(struct engine *engine,
				    const struct vclock *)
{
	struct memtx_engine *memtx = (struct memtx_engine *)engine;
	assert(memtx->state == MEMTX_INITIALIZED);
	/*
	 * By default, enable fast start: bulk read of tuples
	 * from the snapshot, in which they are stored in key
	 * order, and bulk build of the primary key.
	 *
	 * If force_recovery = true, it's a disaster
	 * recovery mode. Enable all keys on start, to detect and
	 * discard duplicates in the snapshot.
	 */
	memtx->state = (memtx->force_recovery ?
			MEMTX_OK : MEMTX_INITIAL_RECOVERY);
}

static void
memtx_engine_begin_final_recovery(struct engine *engine)
{
	struct memtx_engine *memtx = (struct memtx_engine *)engine;
	if (memtx->state == MEMTX_OK)
		return;

	assert(memtx->state == MEMTX_INITIAL_RECOVERY);
	/* End of the fast path: loaded the primary key. */
	space_foreach(memtx_end_build_primary_key, memtx);

	if (!memtx->force_recovery) {
		/*
		 * Fast start path: "play out" WAL
		 * records using the primary key only,
		 * then bulk-build all secondary keys.
		 */
		memtx->state = MEMTX_FINAL_RECOVERY;
	} else {
		/*
		 * If force_recovery = true, it's
		 * a disaster recovery mode. Build
		 * secondary keys before reading the WAL,
		 * to detect and discard duplicates in
		 * unique keys.
		 */
		memtx->state = MEMTX_OK;
		space_foreach(memtx_build_secondary_keys, memtx);
	}
}

static void
memtx_engine_end_recovery(struct engine *engine)
{
	struct memtx_engine *memtx = (struct memtx_engine *)engine;
	/*
	 * Recovery is started with enabled keys when:
	 * - either of force_recovery
	 *   is false
	 * - it's a replication join
	 */
	if (memtx->state != MEMTX_OK) {
		assert(memtx->state == MEMTX_FINAL_RECOVERY);
		memtx->state = MEMTX_OK;
		space_foreach(memtx_build_secondary_keys, memtx);
	}
}

static struct space *
memtx_engine_create_space(struct engine *engine, struct space_def *def,
			  struct rlist *key_list)
{
	struct memtx_engine *memtx = (struct memtx_engine *)engine;
	struct space *space = memtx_space_new(memtx, def, key_list);
	if (space == NULL)
		diag_raise();
	return space;
}

static void
memtx_engine_prepare(struct engine *engine, struct txn *txn)
{
	(void)engine;
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

static void
memtx_engine_begin(struct engine *engine, struct txn *txn)
{
	(void)engine;
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

static void
memtx_engine_begin_statement(struct engine *, struct txn *)
{
}

static void
memtx_engine_rollback_statement(struct engine *engine, struct txn *txn,
				struct txn_stmt *stmt)
{
	(void)engine;
	(void)txn;
	if (stmt->old_tuple == NULL && stmt->new_tuple == NULL)
		return;
	struct space *space = stmt->space;
	struct memtx_space *memtx_space = (struct memtx_space *)space;
	int index_count;

	/* Only roll back the changes if they were made. */
	if (stmt->engine_savepoint == NULL)
		index_count = 0;
	else if (memtx_space->replace == memtx_space_replace_all_keys)
		index_count = space->index_count;
	else if (memtx_space->replace == memtx_space_replace_primary_key)
		index_count = 1;
	else
		panic("transaction rolled back during snapshot recovery");

	for (int i = 0; i < index_count; i++) {
		struct index *index = space->index[i];
		index_replace_xc(index, stmt->new_tuple,
				 stmt->old_tuple, DUP_INSERT);
	}
	/** Reset to old bsize, if it was changed. */
	if (stmt->engine_savepoint != NULL)
		memtx_space_update_bsize(space, stmt->new_tuple,
					 stmt->old_tuple);

	if (stmt->new_tuple)
		tuple_unref(stmt->new_tuple);

	stmt->old_tuple = NULL;
	stmt->new_tuple = NULL;
}

static void
memtx_engine_rollback(struct engine *engine, struct txn *txn)
{
	memtx_engine_prepare(engine, txn);
	struct txn_stmt *stmt;
	stailq_reverse(&txn->stmts);
	stailq_foreach_entry(stmt, &txn->stmts, next)
		memtx_engine_rollback_statement(engine, txn, stmt);
}

static void
memtx_engine_commit(struct engine *engine, struct txn *txn)
{
	(void)engine;
	struct txn_stmt *stmt;
	stailq_foreach_entry(stmt, &txn->stmts, next) {
		if (stmt->old_tuple)
			tuple_unref(stmt->old_tuple);
	}
}

static void
memtx_engine_bootstrap(struct engine *engine)
{
	struct memtx_engine *memtx = (struct memtx_engine *)engine;

	assert(memtx->state == MEMTX_INITIALIZED);
	memtx->state = MEMTX_OK;

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
		memtx_engine_recover_snapshot_row(memtx, &row);
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
checkpoint_write_tuple(struct xlog *l, uint32_t space_id,
		       const char *data, uint32_t size)
{
	struct request_replace_body body;
	body.m_body = 0x82; /* map of two elements. */
	body.k_space_id = IPROTO_SPACE_ID;
	body.m_space_id = 0xce; /* uint32 */
	body.v_space_id = mp_bswap_u32(space_id);
	body.k_tuple = IPROTO_TUPLE;

	struct xrow_header row;
	memset(&row, 0, sizeof(struct xrow_header));
	row.type = IPROTO_INSERT;

	row.bodycnt = 2;
	row.body[0].iov_base = &body;
	row.body[0].iov_len = sizeof(body);
	row.body[1].iov_base = (char *)data;
	row.body[1].iov_len = size;
	checkpoint_write_row(l, &row);
}

struct checkpoint_entry {
	struct space *space;
	struct snapshot_iterator *iterator;
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
	struct index *pk = space_index(sp, 0);
	if (!pk)
		return;
	struct checkpoint *ckpt = (struct checkpoint *)data;
	struct checkpoint_entry *entry;
	entry = region_alloc_object_xc(&fiber()->gc, struct checkpoint_entry);
	rlist_add_tail_entry(&ckpt->entries, entry, link);

	entry->space = sp;
	entry->iterator = index_create_snapshot_iterator_xc(pk);
};

static int
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
		uint32_t size;
		const char *data;
		struct snapshot_iterator *it = entry->iterator;
		for (data = it->next(it, &size); data != NULL;
		     data = it->next(it, &size)) {
			checkpoint_write_tuple(&snap, space_id(entry->space),
					       data, size);
		}
	}
	xlog_flush(&snap);
	say_info("done");
	return 0;
}

static int
memtx_engine_begin_checkpoint(struct engine *engine)
{
	struct memtx_engine *memtx = (struct memtx_engine *)engine;

	assert(memtx->checkpoint == NULL);
	memtx->checkpoint = region_alloc_object_xc(&fiber()->gc, struct checkpoint);

	checkpoint_init(memtx->checkpoint, memtx->snap_dir.dirname,
			memtx->snap_io_rate_limit);
	space_foreach(checkpoint_add_space, memtx->checkpoint);

	/* increment snapshot version; set tuple deletion to delayed mode */
	memtx_tuple_begin_snapshot();
	return 0;
}

static int
memtx_engine_wait_checkpoint(struct engine *engine, struct vclock *vclock)
{
	struct memtx_engine *memtx = (struct memtx_engine *)engine;

	assert(memtx->checkpoint != NULL);
	/*
	 * If a snapshot already exists, do not create a new one.
	 */
	struct vclock last;
	if (xdir_last_vclock(&memtx->snap_dir, &last) >= 0 &&
	    vclock_compare(&last, vclock) == 0) {
		memtx->checkpoint->touch = true;
	}
	vclock_copy(memtx->checkpoint->vclock, vclock);

	if (cord_costart(&memtx->checkpoint->cord, "snapshot",
			 checkpoint_f, memtx->checkpoint)) {
		return -1;
	}
	memtx->checkpoint->waiting_for_snap_thread = true;

	/* wait for memtx-part snapshot completion */
	int result = cord_cojoin(&memtx->checkpoint->cord);
	if (result != 0)
		diag_log();

	memtx->checkpoint->waiting_for_snap_thread = false;
	return result;
}

static void
memtx_engine_commit_checkpoint(struct engine *engine, struct vclock *vclock)
{
	(void) vclock;
	struct memtx_engine *memtx = (struct memtx_engine *)engine;

	/* beginCheckpoint() must have been done */
	assert(memtx->checkpoint != NULL);
	/* waitCheckpoint() must have been done. */
	assert(!memtx->checkpoint->waiting_for_snap_thread);

	memtx_tuple_end_snapshot();

	if (!memtx->checkpoint->touch) {
		int64_t lsn = vclock_sum(memtx->checkpoint->vclock);
		struct xdir *dir = &memtx->checkpoint->dir;
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
	if (xdir_last_vclock(&memtx->snap_dir, &last) < 0 ||
	    vclock_compare(&last, vclock) != 0) {
		/* Add the new checkpoint to the set. */
		xdir_add_vclock(&memtx->snap_dir, memtx->checkpoint->vclock);
		/* Prevent checkpoint_destroy() from freeing vclock. */
		memtx->checkpoint->vclock = NULL;
	}

	checkpoint_destroy(memtx->checkpoint);
	memtx->checkpoint = NULL;
}

static void
memtx_engine_abort_checkpoint(struct engine *engine)
{
	struct memtx_engine *memtx = (struct memtx_engine *)engine;

	/**
	 * An error in the other engine's first phase.
	 */
	if (memtx->checkpoint->waiting_for_snap_thread) {
		/* wait for memtx-part snapshot completion */
		if (cord_cojoin(&memtx->checkpoint->cord) != 0)
			diag_log();
		memtx->checkpoint->waiting_for_snap_thread = false;
	}

	memtx_tuple_end_snapshot();

	/** Remove garbage .inprogress file. */
	char *filename =
		xdir_format_filename(&memtx->checkpoint->dir,
				     vclock_sum(memtx->checkpoint->vclock),
				     INPROGRESS);
	(void) coio_unlink(filename);

	checkpoint_destroy(memtx->checkpoint);
	memtx->checkpoint = NULL;
}

static int
memtx_engine_collect_garbage(struct engine *engine, int64_t lsn)
{
	struct memtx_engine *memtx = (struct memtx_engine *)engine;
	/*
	 * We recover the checkpoint list by scanning the snapshot
	 * directory so deletion of an xlog file or a file that
	 * belongs to another engine without the corresponding snap
	 * file would result in a corrupted checkpoint on the list.
	 * That said, we have to abort garbage collection if we
	 * fail to delete a snap file.
	 */
	if (xdir_collect_garbage(&memtx->snap_dir, lsn, true) != 0)
		return -1;

	return 0;
}

static int
memtx_engine_backup(struct engine *engine, struct vclock *vclock,
		    engine_backup_cb cb, void *cb_arg)
{
	struct memtx_engine *memtx = (struct memtx_engine *)engine;
	char *filename = xdir_format_filename(&memtx->snap_dir,
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
	if (!xlog_cursor_is_eof(&cursor))
		panic("snapshot `%s' has no EOF marker", cursor.name);
	return 0;
}

static void
memtx_engine_join(struct engine *engine, struct vclock *vclock,
		  struct xstream *stream)
{
	struct memtx_engine *memtx = (struct memtx_engine *)engine;

	/*
	 * cord_costart() passes only void * pointer as an argument.
	 */
	struct memtx_join_arg arg = {
		/* .snap_dirname   = */ memtx->snap_dir.dirname,
		/* .checkpoint_lsn = */ vclock_sum(vclock),
		/* .stream         = */ stream
	};

	/* Send snapshot using a thread */
	struct cord cord;
	cord_costart(&cord, "initial_join", memtx_initial_join_f, &arg);
	if (cord_cojoin(&cord) != 0)
		diag_raise();
}

static void
memtx_engine_check_space_def(struct space_def *)
{
}

static const struct engine_vtab memtx_engine_vtab = {
	/* .shutdown = */ memtx_engine_shutdown,
	/* .create_space = */ memtx_engine_create_space,
	/* .join = */ memtx_engine_join,
	/* .begin = */ memtx_engine_begin,
	/* .begin_statement = */ memtx_engine_begin_statement,
	/* .prepare = */ memtx_engine_prepare,
	/* .commit = */ memtx_engine_commit,
	/* .rollback_statement = */ memtx_engine_rollback_statement,
	/* .rollback = */ memtx_engine_rollback,
	/* .bootstrap = */ memtx_engine_bootstrap,
	/* .begin_initial_recovery = */ memtx_engine_begin_initial_recovery,
	/* .begin_final_recovery = */ memtx_engine_begin_final_recovery,
	/* .end_recovery = */ memtx_engine_end_recovery,
	/* .begin_checkpoint = */ memtx_engine_begin_checkpoint,
	/* .wait_checkpoint = */ memtx_engine_wait_checkpoint,
	/* .commit_checkpoint = */ memtx_engine_commit_checkpoint,
	/* .abort_checkpoint = */ memtx_engine_abort_checkpoint,
	/* .collect_garbage = */ memtx_engine_collect_garbage,
	/* .backup = */ memtx_engine_backup,
	/* .check_space_def = */ memtx_engine_check_space_def,
};

struct memtx_engine *
memtx_engine_new(const char *snap_dirname, bool force_recovery,
		 uint64_t tuple_arena_max_size, uint32_t objsize_min,
		 float alloc_factor)
{
	memtx_tuple_init(tuple_arena_max_size, objsize_min, alloc_factor);

	struct memtx_engine *memtx =
		(struct memtx_engine *)calloc(1, sizeof(*memtx));
	if (memtx == NULL) {
		tnt_raise(OutOfMemory, sizeof(*memtx),
			  "malloc", "struct memtx_engine");
	}

	xdir_create(&memtx->snap_dir, snap_dirname, SNAP, &INSTANCE_UUID);
	memtx->snap_dir.force_recovery = force_recovery;

	if (xdir_scan(&memtx->snap_dir) != 0) {
		xdir_destroy(&memtx->snap_dir);
		free(memtx);
		diag_raise();
	}

	memtx->state = MEMTX_INITIALIZED;
	memtx->force_recovery = force_recovery;

	memtx->base.vtab = &memtx_engine_vtab;
	memtx->base.name = "memtx";
	return memtx;
}

void
memtx_engine_set_snap_io_rate_limit(struct memtx_engine *memtx, double limit)
{
	memtx->snap_io_rate_limit = limit * 1024 * 1024;
}

void
memtx_engine_set_max_tuple_size(struct memtx_engine *memtx, size_t max_size)
{
	(void)memtx;
	memtx_max_tuple_size = max_size;
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
	ERROR_INJECT(ERRINJ_INDEX_ALLOC, {
		/* same error as in mempool_alloc */
		diag_set(OutOfMemory, MEMTX_EXTENT_SIZE,
			 "mempool", "new slab");
		return NULL;
	});
	void *ret = mempool_alloc(&memtx_index_extent_pool);
	if (ret == NULL)
		diag_set(OutOfMemory, MEMTX_EXTENT_SIZE,
			 "mempool", "new slab");
	return ret;
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
int
memtx_index_extent_reserve(int num)
{
	ERROR_INJECT(ERRINJ_INDEX_ALLOC, {
		/* same error as in mempool_alloc */
		diag_set(OutOfMemory, MEMTX_EXTENT_SIZE,
			 "mempool", "new slab");
		return -1;
	});
	while (memtx_index_num_reserved_extents < num) {
		void *ext = mempool_alloc(&memtx_index_extent_pool);
		if (ext == NULL) {
			diag_set(OutOfMemory, MEMTX_EXTENT_SIZE,
				 "mempool", "new slab");
			return -1;
		}
		*(void **)ext = memtx_index_reserved_extents;
		memtx_index_reserved_extents = ext;
		memtx_index_num_reserved_extents++;
	}
	return 0;
}
