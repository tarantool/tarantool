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

#include <small/small.h>
#include <small/mempool.h>

#include "coio_file.h"
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
txn_on_yield_or_stop(struct trigger *trigger, void *event)
{
	(void)trigger;
	(void)event;
	txn_rollback(); /* doesn't throw */
}

static int
memtx_end_build_primary_key(struct space *space, void *param)
{
	struct memtx_space *memtx_space = (struct memtx_space *)space;
	if (space->engine != param || space_index(space, 0) == NULL ||
	    memtx_space->replace == memtx_space_replace_all_keys)
		return 0;

	index_end_build(space->index[0]);
	memtx_space->replace = memtx_space_replace_primary_key;
	return 0;
}

/**
 * Secondary indexes are built in bulk after all data is
 * recovered. This function enables secondary keys on a space.
 * Data dictionary spaces are an exception, they are fully
 * built right from the start.
 */
static int
memtx_build_secondary_keys(struct space *space, void *param)
{
	struct memtx_space *memtx_space = (struct memtx_space *)space;
	if (space->engine != param || space_index(space, 0) == NULL ||
	    memtx_space->replace == memtx_space_replace_all_keys)
		return 0;

	if (space->index_id_max > 0) {
		struct index *pk = space->index[0];
		ssize_t n_tuples = index_size(pk);
		assert(n_tuples >= 0);

		if (n_tuples > 0) {
			say_info("Building secondary indexes in space '%s'...",
				 space_name(space));
		}

		for (uint32_t j = 1; j < space->index_count; j++) {
			if (index_build(space->index[j], pk) < 0)
				return -1;
		}

		if (n_tuples > 0) {
			say_info("Space '%s': done", space_name(space));
		}
	}
	memtx_space->replace = memtx_space_replace_all_keys;
	return 0;
}

static void
memtx_engine_shutdown(struct engine *engine)
{
	struct memtx_engine *memtx = (struct memtx_engine *)engine;
	if (mempool_is_initialized(&memtx->tree_iterator_pool))
		mempool_destroy(&memtx->tree_iterator_pool);
	if (mempool_is_initialized(&memtx->rtree_iterator_pool))
		mempool_destroy(&memtx->rtree_iterator_pool);
	if (mempool_is_initialized(&memtx->hash_iterator_pool))
		mempool_destroy(&memtx->hash_iterator_pool);
	if (mempool_is_initialized(&memtx->bitset_iterator_pool))
		mempool_destroy(&memtx->bitset_iterator_pool);
	xdir_destroy(&memtx->snap_dir);
	free(memtx);
	memtx_tuple_free();
}

static int
memtx_engine_recover_snapshot_row(struct memtx_engine *memtx,
				  struct xrow_header *row);

int
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
	if (xlog_cursor_open(&cursor, filename) < 0)
		return -1;
	INSTANCE_UUID = cursor.meta.instance_uuid;

	int rc;
	struct xrow_header row;
	uint64_t row_count = 0;
	while ((rc = xlog_cursor_next(&cursor, &row,
				      memtx->force_recovery)) == 0) {
		row.lsn = signature;
		rc = memtx_engine_recover_snapshot_row(memtx, &row);
		if (rc < 0) {
			if (!memtx->force_recovery)
				break;
			say_error("can't apply row: ");
			diag_log();
		}
		++row_count;
		if (row_count % 100000 == 0) {
			say_info("%.1fM rows processed",
				 row_count / 1000000.);
			fiber_yield_timeout(0);
		}
	}
	xlog_cursor_close(&cursor, false);
	if (rc < 0)
		return -1;

	/**
	 * We should never try to read snapshots with no EOF
	 * marker - such snapshots are very likely corrupted and
	 * should not be trusted.
	 */
	if (!xlog_cursor_is_eof(&cursor))
		panic("snapshot `%s' has no EOF marker", filename);

	return 0;
}

static int
memtx_engine_recover_snapshot_row(struct memtx_engine *memtx,
				  struct xrow_header *row)
{
	assert(row->bodycnt == 1); /* always 1 for read */
	if (row->type != IPROTO_INSERT) {
		diag_set(ClientError, ER_UNKNOWN_REQUEST_TYPE,
			 (uint32_t) row->type);
		return -1;
	}

	struct request request;
	if (xrow_decode_dml(row, &request, dml_request_key_map(row->type)) != 0)
		return -1;
	struct space *space = space_cache_find(request.space_id);
	if (space == NULL)
		return -1;
	/* memtx snapshot must contain only memtx spaces */
	if (space->engine != (struct engine *)memtx) {
		diag_set(ClientError, ER_CROSS_ENGINE_TRANSACTION);
		return -1;
	}
	/* no access checks here - applier always works with admin privs */
	if (space_apply_initial_join_row(space, &request) != 0)
		return -1;
	/*
	 * Don't let gc pool grow too much. Yet to
	 * it before reading the next row, to make
	 * sure it's not freed along here.
	 */
	fiber_gc();
	return 0;
}

/** Called at start to tell memtx to recover to a given LSN. */
static int
memtx_engine_begin_initial_recovery(struct engine *engine,
				    const struct vclock *vclock)
{
	(void)vclock;
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
	return 0;
}

static int
memtx_engine_begin_final_recovery(struct engine *engine)
{
	struct memtx_engine *memtx = (struct memtx_engine *)engine;
	if (memtx->state == MEMTX_OK)
		return 0;

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
		if (space_foreach(memtx_build_secondary_keys, memtx) != 0)
			return -1;
	}
	return 0;
}

static int
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
		if (space_foreach(memtx_build_secondary_keys, memtx) != 0)
			return -1;
	}
	return 0;
}

static struct space *
memtx_engine_create_space(struct engine *engine, struct space_def *def,
			  struct rlist *key_list)
{
	struct memtx_engine *memtx = (struct memtx_engine *)engine;
	return memtx_space_new(memtx, def, key_list);
}

static int
memtx_engine_prepare(struct engine *engine, struct txn *txn)
{
	(void)engine;
	if (txn->is_autocommit)
		return 0;
	/*
	 * These triggers are only used for memtx and only
	 * when autocommit == false, so we are saving
	 * on calls to trigger_create/trigger_clear.
	 */
	trigger_clear(&txn->fiber_on_yield);
	trigger_clear(&txn->fiber_on_stop);
	return 0;
}

static int
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
	return 0;
}

static int
memtx_engine_begin_statement(struct engine *engine, struct txn *txn)
{
	(void)engine;
	(void)txn;
	return 0;
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
		struct tuple *unused;
		struct index *index = space->index[i];
		/* Rollback must not fail. */
		if (index_replace(index, stmt->new_tuple, stmt->old_tuple,
				  DUP_INSERT, &unused) != 0) {
			diag_log();
			unreachable();
			panic("failed to rollback change");
		}
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

static int
memtx_engine_bootstrap(struct engine *engine)
{
	struct memtx_engine *memtx = (struct memtx_engine *)engine;

	assert(memtx->state == MEMTX_INITIALIZED);
	memtx->state = MEMTX_OK;

	/* Recover from bootstrap.snap */
	say_info("initializing an empty data directory");
	struct xlog_cursor cursor;
	if (xlog_cursor_openmem(&cursor, (const char *)bootstrap_bin,
				sizeof(bootstrap_bin), "bootstrap") < 0)
		return -1;

	int rc;
	struct xrow_header row;
	while ((rc = xlog_cursor_next(&cursor, &row, true)) == 0) {
		rc = memtx_engine_recover_snapshot_row(memtx, &row);
		if (rc < 0)
			break;
	}
	xlog_cursor_close(&cursor, false);
	return rc < 0 ? -1 : 0;
}

static int
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
	if (written < 0)
		return -1;

	if ((l->rows + l->tx_rows) % 100000 == 0)
		say_crit("%.1fM rows written", (l->rows + l->tx_rows) / 1000000.0);
	return 0;

}

static int
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
	return checkpoint_write_row(l, &row);
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

static int
checkpoint_init(struct checkpoint *ckpt, const char *snap_dirname,
		uint64_t snap_io_rate_limit)
{
	rlist_create(&ckpt->entries);
	ckpt->waiting_for_snap_thread = false;
	xdir_create(&ckpt->dir, snap_dirname, SNAP, &INSTANCE_UUID);
	ckpt->snap_io_rate_limit = snap_io_rate_limit;
	/* May be used in abortCheckpoint() */
	ckpt->vclock = malloc(sizeof(*ckpt->vclock));
	if (ckpt->vclock == NULL) {
		diag_set(OutOfMemory, sizeof(*ckpt->vclock),
			 "malloc", "vclock");
		return -1;
	}
	vclock_create(ckpt->vclock);
	ckpt->touch = false;
	return 0;
}

static void
checkpoint_destroy(struct checkpoint *ckpt)
{
	struct checkpoint_entry *entry;
	rlist_foreach_entry(entry, &ckpt->entries, link) {
		entry->iterator->free(entry->iterator);
	}
	rlist_create(&ckpt->entries);
	xdir_destroy(&ckpt->dir);
	free(ckpt->vclock);
}


static int
checkpoint_add_space(struct space *sp, void *data)
{
	if (space_is_temporary(sp))
		return 0;
	if (!space_is_memtx(sp))
		return 0;
	struct index *pk = space_index(sp, 0);
	if (!pk)
		return 0;
	struct checkpoint *ckpt = (struct checkpoint *)data;
	struct checkpoint_entry *entry;
	entry = region_alloc_object(&fiber()->gc, struct checkpoint_entry);
	if (entry == NULL) {
		diag_set(OutOfMemory, sizeof(*entry),
			 "region", "struct checkpoint_entry");
		return -1;
	}
	rlist_add_tail_entry(&ckpt->entries, entry, link);

	entry->space = sp;
	entry->iterator = index_create_snapshot_iterator(pk);
	if (entry->iterator == NULL)
		return -1;

	return 0;
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
		return -1;

	snap.rate_limit = ckpt->snap_io_rate_limit;

	say_info("saving snapshot `%s'", snap.filename);
	struct checkpoint_entry *entry;
	rlist_foreach_entry(entry, &ckpt->entries, link) {
		uint32_t size;
		const char *data;
		struct snapshot_iterator *it = entry->iterator;
		for (data = it->next(it, &size); data != NULL;
		     data = it->next(it, &size)) {
			if (checkpoint_write_tuple(&snap,
					space_id(entry->space),
					data, size) != 0) {
				xlog_close(&snap, false);
				return -1;
			}
		}
	}
	if (xlog_flush(&snap) < 0) {
		xlog_close(&snap, false);
		return -1;
	}
	xlog_close(&snap, false);
	say_info("done");
	return 0;
}

static int
memtx_engine_begin_checkpoint(struct engine *engine)
{
	struct memtx_engine *memtx = (struct memtx_engine *)engine;

	assert(memtx->checkpoint == NULL);
	memtx->checkpoint = region_alloc_object(&fiber()->gc, struct checkpoint);
	if (memtx->checkpoint == NULL) {
		diag_set(OutOfMemory, sizeof(*memtx->checkpoint),
			 "region", "struct checkpoint");
		return -1;
	}

	if (checkpoint_init(memtx->checkpoint, memtx->snap_dir.dirname,
			    memtx->snap_io_rate_limit) != 0)
		return -1;

	if (space_foreach(checkpoint_add_space, memtx->checkpoint) != 0) {
		checkpoint_destroy(memtx->checkpoint);
		memtx->checkpoint = NULL;
		return -1;
	}

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
	struct xlog_cursor cursor;
	int rc = xdir_open_cursor(&dir, checkpoint_lsn, &cursor);
	xdir_destroy(&dir);
	if (rc < 0)
		return -1;

	struct xrow_header row;
	while ((rc = xlog_cursor_next(&cursor, &row, true)) == 0) {
		rc = xstream_write(stream, &row);
		if (rc < 0)
			break;
	}
	xlog_cursor_close(&cursor, false);
	if (rc < 0)
		return -1;

	/**
	 * We should never try to read snapshots with no EOF
	 * marker - such snapshots are very likely corrupted and
	 * should not be trusted.
	 */
	/* TODO: replace panic with diag_set() */
	if (!xlog_cursor_is_eof(&cursor))
		panic("snapshot `%s' has no EOF marker", cursor.name);
	return 0;
}

static int
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
	return cord_cojoin(&cord);
}

static int
small_stats_noop_cb(const struct mempool_stats *stats, void *cb_ctx)
{
	(void)stats;
	(void)cb_ctx;
	return 0;
}

static void
memtx_engine_memory_stat(struct engine *engine, struct engine_memory_stat *stat)
{
	(void)engine;
	struct small_stats data_stats;
	struct mempool_stats index_stats;
	mempool_stats(&memtx_index_extent_pool, &index_stats);
	small_stats(&memtx_alloc, &data_stats, small_stats_noop_cb, NULL);
	stat->data += data_stats.used;
	stat->index += index_stats.totals.used;
}

static void
memtx_engine_reset_stat(struct engine *engine)
{
	(void)engine;
}

static int
memtx_engine_check_space_def(struct space_def *def)
{
	(void)def;
	return 0;
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
	/* .memory_stat = */ memtx_engine_memory_stat,
	/* .reset_stat = */ memtx_engine_reset_stat,
	/* .check_space_def = */ memtx_engine_check_space_def,
};

struct memtx_engine *
memtx_engine_new(const char *snap_dirname, bool force_recovery,
		 uint64_t tuple_arena_max_size, uint32_t objsize_min,
		 float alloc_factor)
{
	memtx_tuple_init(tuple_arena_max_size, objsize_min, alloc_factor);

	struct memtx_engine *memtx = calloc(1, sizeof(*memtx));
	if (memtx == NULL) {
		diag_set(OutOfMemory, sizeof(*memtx),
			 "malloc", "struct memtx_engine");
		return NULL;
	}

	xdir_create(&memtx->snap_dir, snap_dirname, SNAP, &INSTANCE_UUID);
	memtx->snap_dir.force_recovery = force_recovery;

	if (xdir_scan(&memtx->snap_dir) != 0) {
		xdir_destroy(&memtx->snap_dir);
		free(memtx);
		return NULL;
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
memtx_index_arena_init(void)
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

void
memtx_index_prune(struct index *index)
{
	if (index->def->iid > 0)
		return;

	/*
	 * Tuples stored in a memtx space are referenced by the
	 * primary index so when the primary index is dropped we
	 * should delete them.
	 */
	struct iterator *it = index_create_iterator(index, ITER_ALL, NULL, 0);
	if (it == NULL)
		goto fail;
	int rc;
	struct tuple *tuple;
	while ((rc = iterator_next(it, &tuple)) == 0 && tuple != NULL)
		tuple_unref(tuple);
	iterator_delete(it);
	if (rc != 0)
		goto fail;

	return;
fail:
	/*
	 * This function is called after WAL write so we have no
	 * other choice but panic in case of any error. The good
	 * news is memtx iterators do not fail so we should not
	 * normally get here.
	 */
	diag_log();
	unreachable();
	panic("failed to drop index");
}

void
memtx_index_abort_create(struct index *index)
{
	memtx_index_prune(index);
}

void
memtx_index_commit_drop(struct index *index)
{
	memtx_index_prune(index);
}

bool
memtx_index_def_change_requires_rebuild(struct index *index,
					const struct index_def *new_def)
{
	struct index_def *old_def = index->def;

	assert(old_def->iid == new_def->iid);
	assert(old_def->space_id == new_def->space_id);

	if (old_def->type != new_def->type)
		return true;
	if (!old_def->opts.is_unique && new_def->opts.is_unique)
		return true;

	const struct key_def *old_cmp_def, *new_cmp_def;
	if (index_depends_on_pk(index)) {
		old_cmp_def = old_def->cmp_def;
		new_cmp_def = new_def->cmp_def;
	} else {
		old_cmp_def = old_def->key_def;
		new_cmp_def = new_def->key_def;
	}

	/*
	 * Compatibility of field types is verified by CheckSpaceFormat
	 * so it suffices to check that the new key definition indexes
	 * the same set of fields in the same order.
	 */
	if (old_cmp_def->part_count != new_cmp_def->part_count)
		return true;

	for (uint32_t i = 0; i < new_cmp_def->part_count; i++) {
		const struct key_part *old_part = &old_cmp_def->parts[i];
		const struct key_part *new_part = &new_cmp_def->parts[i];
		if (old_part->fieldno != new_part->fieldno)
			return true;
		if (old_part->coll != new_part->coll)
			return true;
	}
	return false;
}
