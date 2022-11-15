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

#include <small/quota.h>
#include <small/small.h>
#include <small/mempool.h>

#include "fiber.h"
#include "errinj.h"
#include "coio_file.h"
#include "tuple.h"
#include "txn.h"
#include "memtx_tx.h"
#include "memtx_tree.h"
#include "iproto_constants.h"
#include "xrow.h"
#include "xstream.h"
#include "bootstrap.h"
#include "replication.h"
#include "schema.h"
#include "gc.h"
#include "raft.h"
#include "txn_limbo.h"
#include "memtx_allocator.h"
#include "index.h"
#include "read_view.h"
#include "memtx_tuple_compression.h"
#include "memtx_space.h"

#include <type_traits>

/* sync snapshot every 16MB */
#define SNAP_SYNC_INTERVAL	(1 << 24)

static void
checkpoint_cancel(struct checkpoint *ckpt);

static void
replica_join_cancel(struct cord *replica_join_cord);

enum {
	OBJSIZE_MIN = 16,
	SLAB_SIZE = 16 * 1024 * 1024,
	MAX_TUPLE_SIZE = 1 * 1024 * 1024,
};

template <class ALLOC>
static inline void
create_memtx_tuple_format_vtab(struct tuple_format_vtab *vtab);

struct tuple *
(*memtx_tuple_new_raw)(struct tuple_format *format, const char *data,
		       const char *end, bool validate);

template <class ALLOC>
static inline struct tuple *
memtx_tuple_new_raw_impl(struct tuple_format *format, const char *data,
			 const char *end, bool validate);

template <class ALLOC>
static void
memtx_alloc_init(void)
{
	memtx_tuple_new_raw = memtx_tuple_new_raw_impl<ALLOC>;
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
 * Build memtx secondary index based on the contents of primary index.
 */
static int
memtx_build_secondary_index(struct index *index, struct index *pk)
{
	ssize_t n_tuples = index_size(pk);
	if (n_tuples < 0)
		return -1;
	uint32_t estimated_tuples = n_tuples * 1.2;

	index_begin_build(index);
	if (index_reserve(index, estimated_tuples) < 0)
		return -1;

	if (n_tuples > 0) {
		say_info("Adding %zd keys to %s index '%s' ...",
			 n_tuples, index_type_strs[index->def->type],
			 index->def->name);
	}

	struct iterator *it = index_create_iterator(pk, ITER_ALL, NULL, 0);
	if (it == NULL)
		return -1;

	int rc = 0;
	while (true) {
		struct tuple *tuple;
		rc = iterator_next_internal(it, &tuple);
		if (rc != 0)
			break;
		if (tuple == NULL)
			break;
		rc = index_build_next(index, tuple);
		if (rc != 0)
			break;
	}
	iterator_delete(it);
	if (rc != 0)
		return -1;

	index_end_build(index);
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
			if (memtx_build_secondary_index(space->index[j],
							pk) < 0)
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
	if (memtx->checkpoint != NULL)
		checkpoint_cancel(memtx->checkpoint);
	if (memtx->replica_join_cord != NULL)
		replica_join_cancel(memtx->replica_join_cord);
	mempool_destroy(&memtx->iterator_pool);
	if (mempool_is_initialized(&memtx->rtree_iterator_pool))
		mempool_destroy(&memtx->rtree_iterator_pool);
	mempool_destroy(&memtx->index_extent_pool);
	slab_cache_destroy(&memtx->index_slab_cache);
	/*
	 * The order is vital: allocator destroy should take place before
	 * slab cache destroy!
	 */
	memtx_allocators_destroy();
	slab_cache_destroy(&memtx->slab_cache);
	tuple_arena_destroy(&memtx->arena);

	xdir_destroy(&memtx->snap_dir);
	tuple_format_unref(memtx->func_key_format);
	free(memtx);
}

static int
memtx_engine_recover_snapshot_row(struct memtx_engine *memtx,
				  struct xrow_header *row, int *is_space_system);

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

	int rc;
	struct xrow_header row;
	uint64_t row_count = 0;
	int is_space_system = -1;
	bool force_recovery = false;
	/*
	 * In case when we read system space, we can't ignore errors.
	 */
	while ((rc = xlog_cursor_next(&cursor, &row, force_recovery)) == 0) {
		row.lsn = signature;
		rc = memtx_engine_recover_snapshot_row(memtx, &row,
						       &is_space_system);
		force_recovery = is_space_system == 0 ?
				 memtx->force_recovery : false;
		if (rc < 0) {
			if (!force_recovery)
				break;
			say_error("can't apply row: ");
			diag_log();
		}
		++row_count;
		if (row_count % 100000 == 0) {
			say_info_ratelimited("%.1fM rows processed",
					     row_count / 1e6);
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
	if (!xlog_cursor_is_eof(&cursor)) {
		if (!memtx->force_recovery)
			panic("snapshot `%s' has no EOF marker", cursor.name);
		else
			say_error("snapshot `%s' has no EOF marker", cursor.name);
	}

	/*
	 * Snapshot entries are ordered by the space id, it means that if there
	 * are no spaces, then all system spaces are definitely missing.
	 */
	if (is_space_system < 0)
		panic("snapshot `%s' has no system spaces", cursor.name);

	return 0;
}

static int
memtx_engine_recover_raft(const struct xrow_header *row)
{
	assert(row->type == IPROTO_RAFT);
	struct raft_request req;
	/* Vclock is never persisted in WAL by Raft. */
	if (xrow_decode_raft(row, &req, NULL) != 0)
		return -1;
	box_raft_recover(&req);
	return 0;
}

static int
memtx_engine_recover_synchro(const struct xrow_header *row)
{
	assert(row->type == IPROTO_RAFT_PROMOTE);
	struct synchro_request req;
	if (xrow_decode_synchro(row, &req) != 0)
		return -1;
	/*
	 * Origin id cannot be deduced from row.replica_id in a checkpoint,
	 * because all its rows have a zero replica_id.
	 */
	req.origin_id = req.replica_id;
	return txn_limbo_process(&txn_limbo, &req);
}

static int
memtx_engine_recover_snapshot_row(struct memtx_engine *memtx,
				  struct xrow_header *row, int *is_space_system)
{
	assert(row->bodycnt == 1); /* always 1 for read */
	if (row->type != IPROTO_INSERT) {
		if (row->type == IPROTO_RAFT)
			return memtx_engine_recover_raft(row);
		if (row->type == IPROTO_RAFT_PROMOTE)
			return memtx_engine_recover_synchro(row);
		diag_set(ClientError, ER_UNKNOWN_REQUEST_TYPE,
			 (uint32_t) row->type);
		return -1;
	}
	struct request request;
	RegionGuard region_guard(&fiber()->gc);
	if (xrow_decode_dml(row, &request, dml_request_key_map(row->type)) != 0)
		return -1;
	*is_space_system = (request.space_id < BOX_SYSTEM_ID_MAX);
	struct space *space = space_cache_find(request.space_id);
	if (space == NULL)
		goto log_request;
	/* memtx snapshot must contain only memtx spaces */
	if (space->engine != (struct engine *)memtx) {
		diag_set(ClientError, ER_CROSS_ENGINE_TRANSACTION);
		goto log_request;
	}
	struct txn *txn;
	txn = txn_begin();
	if (txn == NULL)
		goto log_request;
	if (txn_begin_stmt(txn, space, request.type) != 0)
		goto rollback;
	/* no access checks here - applier always works with admin privs */
	struct tuple *unused;
	if (space_execute_dml(space, txn, &request, &unused) != 0)
		goto rollback_stmt;
	if (txn_commit_stmt(txn, &request) != 0)
		goto rollback;
	/*
	 * Snapshot rows are confirmed by definition. They don't need to go to
	 * the synchronous transactions limbo.
	 */
	txn_set_flags(txn, TXN_FORCE_ASYNC);
	return txn_commit(txn);
rollback_stmt:
	txn_rollback_stmt(txn);
rollback:
	txn_abort(txn);
log_request:
	say_error("error at request: %s", request_str(&request));
	return -1;
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

	/* Complete space initialization. */
	int rc = space_foreach(space_on_initial_recovery_complete, NULL);
	/* If failed - the snapshot has inconsistent data. We cannot start. */
	if (rc != 0) {
		diag_log();
		panic("Failed to complete recovery from snapshot!");
	}

	if (!memtx->force_recovery && !memtx_tx_manager_use_mvcc_engine) {
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
		memtx->on_indexes_built_cb();
	}
	return 0;
}

static int
memtx_engine_begin_hot_standby(struct engine *engine)
{
	struct memtx_engine *memtx = (struct memtx_engine *)engine;
	/*
	 * Build secondary indexes before entering the hot standby mode
	 * to quickly switch to the hot standby instance after the master
	 * instance exits.
	 */
	if (memtx->state != MEMTX_OK) {
		assert(memtx->state == MEMTX_FINAL_RECOVERY);
		memtx->state = MEMTX_OK;
		if (space_foreach(memtx_build_secondary_keys, memtx) != 0)
			return -1;
		memtx->on_indexes_built_cb();
	}
	return 0;
}

static int
memtx_engine_end_recovery(struct engine *engine)
{
	struct memtx_engine *memtx = (struct memtx_engine *)engine;
	/*
	 * Secondary keys have already been built in the following cases:
	 * - force_recovery is set
	 * - it's a replication join
	 * - instance was in the hot standby mode
	 */
	if (memtx->state != MEMTX_OK) {
		assert(memtx->state == MEMTX_FINAL_RECOVERY);
		memtx->state = MEMTX_OK;
		if (space_foreach(memtx_build_secondary_keys, memtx) != 0)
			return -1;
		memtx->on_indexes_built_cb();
	}
	xdir_collect_inprogress(&memtx->snap_dir);

	/* Complete space initialization. */
	int rc = space_foreach(space_on_final_recovery_complete, NULL);
	if (rc != 0) {
		diag_log();
		panic("Failed to complete recovery from WAL!");
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

/** Engine read view implementation. */
struct memtx_read_view {
	/** Base class. */
	struct engine_read_view base;
	/**
	 * Allocator read view that prevents tuples referenced by index read
	 * views from being freed.
	 */
	memtx_allocators_read_view allocators_rv;
};

static void
memtx_engine_read_view_free(struct engine_read_view *base)
{
	struct memtx_read_view *rv = (struct memtx_read_view *)base;
	memtx_allocators_close_read_view(rv->allocators_rv);
	free(rv);
}

/** Implementation of create_read_view engine callback. */
static struct engine_read_view *
memtx_engine_create_read_view(struct engine *engine,
			      const struct read_view_opts *opts)
{
	static const struct engine_read_view_vtab vtab = {
		.free = memtx_engine_read_view_free,
	};
	(void)engine;
	struct memtx_read_view *rv =
		(struct memtx_read_view *)xmalloc(sizeof(*rv));
	rv->base.vtab = &vtab;
	rv->allocators_rv = memtx_allocators_open_read_view(opts);
	return (struct engine_read_view *)rv;
}

static int
memtx_engine_begin(struct engine *engine, struct txn *txn)
{
	(void)engine;
	txn_can_yield(txn, memtx_tx_manager_use_mvcc_engine);
	return 0;
}

static int
memtx_engine_prepare(struct engine *engine, struct txn *txn)
{
	(void)engine;
	if (memtx_tx_manager_use_mvcc_engine) {
		struct txn_stmt *stmt;
		stailq_foreach_entry(stmt, &txn->stmts, next) {
			assert(stmt->space->engine == engine);
			memtx_tx_history_prepare_stmt(stmt);
		}
	}
	if (txn->is_schema_changed)
		memtx_tx_abort_all_for_ddl(txn);
	return 0;
}

static void
memtx_engine_commit(struct engine *engine, struct txn *txn)
{
	(void)engine;
	if (memtx_tx_manager_use_mvcc_engine) {
		struct txn_stmt *stmt;
		stailq_foreach_entry(stmt, &txn->stmts, next) {
			assert(stmt->space->engine == engine);
			struct memtx_space *mspace =
				(struct memtx_space *)stmt->space;
			size_t *bsize = &mspace->bsize;
			memtx_tx_history_commit_stmt(stmt, bsize);
		}
	}
}

static void
memtx_engine_rollback_statement(struct engine *engine, struct txn *txn,
				struct txn_stmt *stmt)
{
	(void)engine;
	(void)txn;
	struct tuple *old_tuple = stmt->rollback_info.old_tuple;
	struct tuple *new_tuple = stmt->rollback_info.new_tuple;
	if (old_tuple == NULL && new_tuple == NULL)
		return;
	struct space *space = stmt->space;
	if (space == NULL) {
		/* The space was deleted. Nothing to rollback. */
		return;
	}
	struct memtx_space *memtx_space = (struct memtx_space *)space;
	uint32_t index_count;

	/* Only roll back the changes if they were made. */
	if (stmt->engine_savepoint == NULL)
		return;

	if (memtx_tx_manager_use_mvcc_engine)
		return memtx_tx_history_rollback_stmt(stmt);

	if (memtx_space->replace == memtx_space_replace_all_keys)
		index_count = space->index_count;
	else if (memtx_space->replace == memtx_space_replace_primary_key)
		index_count = 1;
	else
		panic("transaction rolled back during snapshot recovery");

	for (uint32_t i = 0; i < index_count; i++) {
		struct tuple *unused;
		struct index *index = space->index[i];
		/* Rollback must not fail. */
		if (index_replace(index, new_tuple, old_tuple,
				  DUP_INSERT, &unused, &unused) != 0) {
			diag_log();
			unreachable();
			panic("failed to rollback change");
		}
	}

	memtx_space_update_bsize(space, new_tuple, old_tuple);
	if (old_tuple != NULL)
		tuple_ref(old_tuple);
	if (new_tuple != NULL)
		tuple_unref(new_tuple);
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

	int rc, is_space_system;
	struct xrow_header row;
	while ((rc = xlog_cursor_next(&cursor, &row, true)) == 0) {
		rc = memtx_engine_recover_snapshot_row(memtx, &row, &is_space_system);
		if (rc < 0)
			break;
	}
	xlog_cursor_close(&cursor, false);
	if (rc < 0)
		return -1;
	memtx->on_indexes_built_cb();
	return 0;
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

	if (xlog_write_row(l, row) < 0)
		return -1;

	if ((l->rows + l->tx_rows) % 100000 == 0) {
		say_info_ratelimited("%.1fM rows written",
				     (l->rows + l->tx_rows) / 1e6);
	}
	return 0;

}

static int
checkpoint_write_tuple(struct xlog *l, uint32_t space_id, uint32_t group_id,
		       const char *data, uint32_t size)
{
	struct request_replace_body body;
	request_replace_body_create(&body, space_id);

	struct xrow_header row;
	memset(&row, 0, sizeof(struct xrow_header));
	row.type = IPROTO_INSERT;
	row.group_id = group_id;

	row.bodycnt = 2;
	row.body[0].iov_base = &body;
	row.body[0].iov_len = sizeof(body);
	row.body[1].iov_base = (char *)data;
	row.body[1].iov_len = size;
	return checkpoint_write_row(l, &row);
}

struct checkpoint {
	/** Database read view written to the snapshot file. */
	struct read_view rv;
	struct cord cord;
	bool waiting_for_snap_thread;
	/** The vclock of the snapshot file. */
	struct vclock vclock;
	struct xdir dir;
	struct raft_request raft;
	struct synchro_request synchro_state;
	/**
	 * Do nothing, just touch the snapshot file - the
	 * checkpoint already exists.
	 */
	bool touch;
};

/** Space filter for checkpoint. */
static bool
checkpoint_space_filter(struct space *space, void *arg)
{
	(void)arg;
	return space_is_memtx(space) &&
		space_index(space, 0) != NULL;
}

/**
 * In case of checkpoint and replica join, we're only interested in tuples
 * stored in space so we don't need to include secondary indexes into the read
 * view. This function filters out all secondary indexes.
 */
static bool
primary_index_filter(struct space *space, struct index *index, void *arg)
{
	(void)space;
	(void)arg;
	return index->def->iid == 0;
}

static struct checkpoint *
checkpoint_new(const char *snap_dirname, uint64_t snap_io_rate_limit)
{
	struct checkpoint *ckpt = (struct checkpoint *)malloc(sizeof(*ckpt));
	if (ckpt == NULL) {
		diag_set(OutOfMemory, sizeof(*ckpt), "malloc",
			 "struct checkpoint");
		return NULL;
	}
	struct read_view_opts rv_opts;
	read_view_opts_create(&rv_opts);
	rv_opts.filter_space = checkpoint_space_filter;
	rv_opts.filter_index = primary_index_filter;
	if (read_view_open(&ckpt->rv, &rv_opts) != 0) {
		free(ckpt);
		return NULL;
	}
	ckpt->waiting_for_snap_thread = false;
	struct xlog_opts opts = xlog_opts_default;
	opts.rate_limit = snap_io_rate_limit;
	opts.sync_interval = SNAP_SYNC_INTERVAL;
	opts.free_cache = true;
	xdir_create(&ckpt->dir, snap_dirname, SNAP, &INSTANCE_UUID, &opts);
	vclock_create(&ckpt->vclock);
	box_raft_checkpoint_local(&ckpt->raft);
	txn_limbo_checkpoint(&txn_limbo, &ckpt->synchro_state);
	ckpt->touch = false;
	return ckpt;
}

static void
checkpoint_delete(struct checkpoint *ckpt)
{
	read_view_close(&ckpt->rv);
	xdir_destroy(&ckpt->dir);
	free(ckpt);
}

static void
checkpoint_cancel(struct checkpoint *ckpt)
{
	/*
	 * Cancel the checkpoint thread if it's running and wait
	 * for it to terminate so as to eliminate the possibility
	 * of use-after-free.
	 */
	if (ckpt->waiting_for_snap_thread)
		cord_cancel_and_join(&ckpt->cord);
	checkpoint_delete(ckpt);
}

static void
replica_join_cancel(struct cord *replica_join_cord)
{
	/*
	 * Cancel the thread being used to join replica if it's
	 * running and wait for it to terminate so as to
	 * eliminate the possibility of use-after-free.
	 */
	cord_cancel_and_join(replica_join_cord);
}

static int
checkpoint_write_raft(struct xlog *l, const struct raft_request *req)
{
	struct xrow_header row;
	struct region *region = &fiber()->gc;
	RegionGuard region_guard(&fiber()->gc);
	xrow_encode_raft(&row, region, req);
	if (checkpoint_write_row(l, &row) != 0)
		return -1;
	return 0;
}

static int
checkpoint_write_synchro(struct xlog *l, const struct synchro_request *req)
{
	struct xrow_header row;
	char body[XROW_SYNCHRO_BODY_LEN_MAX];
	xrow_encode_synchro(&row, body, req);
	return checkpoint_write_row(l, &row);
}

static int
checkpoint_f(va_list ap)
{
	int rc = 0;
	struct checkpoint *ckpt = va_arg(ap, struct checkpoint *);

	if (ckpt->touch) {
		if (xdir_touch_xlog(&ckpt->dir, &ckpt->vclock) == 0)
			return 0;
		/*
		 * Failed to touch an existing snapshot, create
		 * a new one.
		 */
		ckpt->touch = false;
	}

	struct xlog snap;
	if (xdir_create_xlog(&ckpt->dir, &snap, &ckpt->vclock) != 0)
		return -1;

	say_info("saving snapshot `%s'", snap.filename);
	ERROR_INJECT_SLEEP(ERRINJ_SNAP_WRITE_DELAY);
	struct space_read_view *space_rv;
	read_view_foreach_space(space_rv, &ckpt->rv) {
		FiberGCChecker gc_check;
		uint32_t size;
		const char *data;
		struct index_read_view *index_rv =
			space_read_view_index(space_rv, 0);
		assert(index_rv != NULL);
		struct index_read_view_iterator it;
		if (index_read_view_create_iterator(index_rv, ITER_ALL,
						    NULL, 0, &it) != 0) {
			rc = -1;
			break;
		}
		while (true) {
			RegionGuard region_guard(&fiber()->gc);
			rc = index_read_view_iterator_next_raw(&it, &data,
							       &size);
			if (rc != 0 || data == NULL)
				break;
			rc = checkpoint_write_tuple(&snap, space_rv->id,
						    space_rv->group_id,
						    data, size);
			if (rc != 0)
				break;
		}
		index_read_view_iterator_destroy(&it);
		if (rc != 0)
			break;
	}
	if (rc != 0)
		goto fail;
	if (checkpoint_write_raft(&snap, &ckpt->raft) != 0)
		goto fail;
	if (checkpoint_write_synchro(&snap, &ckpt->synchro_state) != 0)
		goto fail;
	if (xlog_flush(&snap) < 0)
		goto fail;

	xlog_close(&snap, false);
	say_info("done");
	return 0;
fail:
	xlog_close(&snap, false);
	return -1;
}

static int
memtx_engine_begin_checkpoint(struct engine *engine, bool is_scheduled)
{
	(void) is_scheduled;
	struct memtx_engine *memtx = (struct memtx_engine *)engine;

	assert(memtx->checkpoint == NULL);
	memtx->checkpoint = checkpoint_new(memtx->snap_dir.dirname,
					   memtx->snap_io_rate_limit);
	if (memtx->checkpoint == NULL)
		return -1;
	return 0;
}

static int
memtx_engine_wait_checkpoint(struct engine *engine,
			     const struct vclock *vclock)
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
	vclock_copy(&memtx->checkpoint->vclock, vclock);

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
memtx_engine_commit_checkpoint(struct engine *engine,
			       const struct vclock *vclock)
{
	ERROR_INJECT_TERMINATE(ERRINJ_SNAP_COMMIT_FAIL);
	(void) vclock;
	struct memtx_engine *memtx = (struct memtx_engine *)engine;

	/* beginCheckpoint() must have been done */
	assert(memtx->checkpoint != NULL);
	/* waitCheckpoint() must have been done. */
	assert(!memtx->checkpoint->waiting_for_snap_thread);

	if (!memtx->checkpoint->touch) {
		int64_t lsn = vclock_sum(&memtx->checkpoint->vclock);
		struct xdir *dir = &memtx->checkpoint->dir;
		/* rename snapshot on completion */
		char to[PATH_MAX];
		snprintf(to, sizeof(to), "%s",
			 xdir_format_filename(dir, lsn, NONE));
		const char *from = xdir_format_filename(dir, lsn, INPROGRESS);
		ERROR_INJECT_YIELD(ERRINJ_SNAP_COMMIT_DELAY);
		int rc = coio_rename(from, to);
		if (rc != 0)
			panic("can't rename .snap.inprogress");
	}

	struct vclock last;
	if (xdir_last_vclock(&memtx->snap_dir, &last) < 0 ||
	    vclock_compare(&last, vclock) != 0) {
		/* Add the new checkpoint to the set. */
		xdir_add_vclock(&memtx->snap_dir, &memtx->checkpoint->vclock);
	}

	checkpoint_delete(memtx->checkpoint);
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

	/** Remove garbage .inprogress file. */
	const char *filename =
		xdir_format_filename(&memtx->checkpoint->dir,
				     vclock_sum(&memtx->checkpoint->vclock),
				     INPROGRESS);
	(void) coio_unlink(filename);

	checkpoint_delete(memtx->checkpoint);
	memtx->checkpoint = NULL;
}

static void
memtx_engine_collect_garbage(struct engine *engine, const struct vclock *vclock)
{
	struct memtx_engine *memtx = (struct memtx_engine *)engine;
	xdir_collect_garbage(&memtx->snap_dir, vclock_sum(vclock),
			     XDIR_GC_ASYNC);
}

static int
memtx_engine_backup(struct engine *engine, const struct vclock *vclock,
		    engine_backup_cb cb, void *cb_arg)
{
	struct memtx_engine *memtx = (struct memtx_engine *)engine;
	const char *filename = xdir_format_filename(&memtx->snap_dir,
						    vclock_sum(vclock), NONE);
	return cb(filename, cb_arg);
}

struct memtx_join_ctx {
	/** Database read view sent to the replica. */
	struct read_view rv;
	struct xstream *stream;
};

/** Space filter for replica join. */
static bool
memtx_join_space_filter(struct space *space, void *arg)
{
	(void)arg;
	return space_is_memtx(space) &&
	       !space_is_local(space) &&
	       space_index(space, 0) != NULL;
}

static int
memtx_engine_prepare_join(struct engine *engine, void **arg)
{
	(void)engine;
	struct memtx_join_ctx *ctx =
		(struct memtx_join_ctx *)malloc(sizeof(*ctx));
	if (ctx == NULL) {
		diag_set(OutOfMemory, sizeof(*ctx),
			 "malloc", "struct memtx_join_ctx");
		return -1;
	}
	struct read_view_opts rv_opts;
	read_view_opts_create(&rv_opts);
	rv_opts.filter_space = memtx_join_space_filter;
	rv_opts.filter_index = primary_index_filter;
	if (read_view_open(&ctx->rv, &rv_opts) != 0) {
		free(ctx);
		return -1;
	}
	*arg = ctx;
	return 0;
}

static int
memtx_join_send_tuple(struct xstream *stream, uint32_t space_id,
		      const char *data, size_t size)
{
	struct request_replace_body body;
	request_replace_body_create(&body, space_id);

	struct xrow_header row;
	memset(&row, 0, sizeof(row));
	row.type = IPROTO_INSERT;

	row.bodycnt = 2;
	row.body[0].iov_base = &body;
	row.body[0].iov_len = sizeof(body);
	row.body[1].iov_base = (char *)data;
	row.body[1].iov_len = size;

	return xstream_write(stream, &row);
}

static int
memtx_join_f(va_list ap)
{
	int rc = 0;
	struct memtx_join_ctx *ctx = va_arg(ap, struct memtx_join_ctx *);
	struct space_read_view *space_rv;
	read_view_foreach_space(space_rv, &ctx->rv) {
		FiberGCChecker gc_check;
		struct index_read_view *index_rv =
			space_read_view_index(space_rv, 0);
		assert(index_rv != NULL);
		struct index_read_view_iterator it;
		if (index_read_view_create_iterator(index_rv, ITER_ALL,
						    NULL, 0, &it) != 0) {
			rc = -1;
			break;
		}
		uint32_t size;
		const char *data;
		while (true) {
			RegionGuard region_guard(&fiber()->gc);
			rc = index_read_view_iterator_next_raw(&it, &data,
							       &size);
			if (rc != 0 || data == NULL)
				break;
			rc = memtx_join_send_tuple(ctx->stream, space_rv->id,
						   data, size);
			if (rc != 0)
				break;
		}
		index_read_view_iterator_destroy(&it);
		if (rc != 0)
			break;
	}
	return rc;
}

static int
memtx_engine_join(struct engine *engine, void *arg, struct xstream *stream)
{
	(void)engine;
	struct memtx_join_ctx *ctx = (struct memtx_join_ctx *)arg;
	ctx->stream = stream;
	/*
	 * Memtx snapshot iterators are safe to use from another
	 * thread and so we do so as not to consume too much of
	 * precious tx cpu time while a new replica is joining.
	 */
	struct cord cord;
	if (cord_costart(&cord, "initial_join", memtx_join_f, ctx) != 0)
		return -1;
	struct memtx_engine *memtx = (struct memtx_engine *)engine;
	memtx->replica_join_cord = &cord;
	int res = cord_cojoin(&cord);
	memtx->replica_join_cord = NULL;
	xstream_reset(stream);
	return res;
}

static void
memtx_engine_complete_join(struct engine *engine, void *arg)
{
	(void)engine;
	struct memtx_join_ctx *ctx = (struct memtx_join_ctx *)arg;
	read_view_close(&ctx->rv);
	free(ctx);
}

static void
memtx_engine_memory_stat(struct engine *engine, struct engine_memory_stat *stat)
{
	struct memtx_engine *memtx = (struct memtx_engine *)engine;
	struct allocator_stats data_stats;
	struct mempool_stats index_stats;
	mempool_stats(&memtx->index_extent_pool, &index_stats);
	memset(&data_stats, 0, sizeof(data_stats));
	allocators_stats(&data_stats);
	stat->data += data_stats.small.used;
	stat->data += data_stats.sys.used;
	stat->index += index_stats.totals.used;
}

static const struct engine_vtab memtx_engine_vtab = {
	/* .shutdown = */ memtx_engine_shutdown,
	/* .create_space = */ memtx_engine_create_space,
	/* .create_read_view = */ memtx_engine_create_read_view,
	/* .prepare_join = */ memtx_engine_prepare_join,
	/* .join = */ memtx_engine_join,
	/* .complete_join = */ memtx_engine_complete_join,
	/* .begin = */ memtx_engine_begin,
	/* .begin_statement = */ generic_engine_begin_statement,
	/* .prepare = */ memtx_engine_prepare,
	/* .commit = */ memtx_engine_commit,
	/* .rollback_statement = */ memtx_engine_rollback_statement,
	/* .rollback = */ generic_engine_rollback,
	/* .switch_to_ro = */ generic_engine_switch_to_ro,
	/* .bootstrap = */ memtx_engine_bootstrap,
	/* .begin_initial_recovery = */ memtx_engine_begin_initial_recovery,
	/* .begin_final_recovery = */ memtx_engine_begin_final_recovery,
	/* .begin_hot_standby = */ memtx_engine_begin_hot_standby,
	/* .end_recovery = */ memtx_engine_end_recovery,
	/* .begin_checkpoint = */ memtx_engine_begin_checkpoint,
	/* .wait_checkpoint = */ memtx_engine_wait_checkpoint,
	/* .commit_checkpoint = */ memtx_engine_commit_checkpoint,
	/* .abort_checkpoint = */ memtx_engine_abort_checkpoint,
	/* .collect_garbage = */ memtx_engine_collect_garbage,
	/* .backup = */ memtx_engine_backup,
	/* .memory_stat = */ memtx_engine_memory_stat,
	/* .reset_stat = */ generic_engine_reset_stat,
	/* .check_space_def = */ generic_engine_check_space_def,
};

/**
 * Run one iteration of garbage collection. Set @stop if
 * there is no more objects to free.
 */
static void
memtx_engine_run_gc(struct memtx_engine *memtx, bool *stop)
{
	*stop = stailq_empty(&memtx->gc_queue);
	if (*stop)
		return;

	struct memtx_gc_task *task = stailq_first_entry(&memtx->gc_queue,
					struct memtx_gc_task, link);
	bool task_done;
	task->vtab->run(task, &task_done);
	if (task_done) {
		stailq_shift(&memtx->gc_queue);
		task->vtab->free(task);
	}
}

static int
memtx_engine_gc_f(va_list va)
{
	struct memtx_engine *memtx = va_arg(va, struct memtx_engine *);
	while (!fiber_is_cancelled()) {
		FiberGCChecker gc_check;
		bool stop;
		ERROR_INJECT_YIELD(ERRINJ_MEMTX_DELAY_GC);
		memtx_engine_run_gc(memtx, &stop);
		if (stop) {
			fiber_yield_timeout(TIMEOUT_INFINITY);
			continue;
		}
		/*
		 * Yield after each iteration so as not to block
		 * tx thread for too long.
		 */
		fiber_sleep(0);
	}
	return 0;
}

void
memtx_set_tuple_format_vtab(const char *allocator_name)
{
	if (strncmp(allocator_name, "small", strlen("small")) == 0) {
		memtx_alloc_init<SmallAlloc>();
		create_memtx_tuple_format_vtab<SmallAlloc>
			(&memtx_tuple_format_vtab);
	} else if (strncmp(allocator_name, "system", strlen("system")) == 0) {
		memtx_alloc_init<SysAlloc>();
		create_memtx_tuple_format_vtab<SysAlloc>
			(&memtx_tuple_format_vtab);
	} else {
		unreachable();
	}
}

int
memtx_tuple_validate(struct tuple_format *format, struct tuple *tuple)
{
	tuple = memtx_tuple_decompress(tuple);
	if (tuple == NULL)
		return -1;
	tuple_ref(tuple);
	int rc = tuple_validate_raw(format, tuple_data(tuple));
	tuple_unref(tuple);
	return rc;
}

struct memtx_engine *
memtx_engine_new(const char *snap_dirname, bool force_recovery,
		 uint64_t tuple_arena_max_size, uint32_t objsize_min,
		 bool dontdump, unsigned granularity,
		 const char *allocator, float alloc_factor,
		 memtx_on_indexes_built_cb on_indexes_built)
{
	int64_t snap_signature;
	struct memtx_engine *memtx =
		(struct memtx_engine *)calloc(1, sizeof(*memtx));
	if (memtx == NULL) {
		diag_set(OutOfMemory, sizeof(*memtx),
			 "malloc", "struct memtx_engine");
		return NULL;
	}

	xdir_create(&memtx->snap_dir, snap_dirname, SNAP, &INSTANCE_UUID,
		    &xlog_opts_default);
	memtx->snap_dir.force_recovery = force_recovery;

	if (xdir_scan(&memtx->snap_dir, true) != 0)
		goto fail;

	/*
	 * To check if the instance needs to be rebootstrapped, we
	 * need to connect it to remote peers before proceeding to
	 * local recovery. In order to do that, we have to start
	 * listening for incoming connections, because one of remote
	 * peers may be self. This, in turn, requires us to know the
	 * instance UUID, as it is a part of a greeting message.
	 * So if the local directory isn't empty, read the snapshot
	 * signature right now to initialize the instance UUID.
	 */
	snap_signature = xdir_last_vclock(&memtx->snap_dir, NULL);
	if (snap_signature >= 0) {
		struct xlog_cursor cursor;
		if (xdir_open_cursor(&memtx->snap_dir,
				     snap_signature, &cursor) != 0)
			goto fail;
		INSTANCE_UUID = cursor.meta.instance_uuid;
		xlog_cursor_close(&cursor, false);
	}

	/* Apprise the garbage collector of available checkpoints. */
	for (struct vclock *vclock = vclockset_first(&memtx->snap_dir.index);
	     vclock != NULL;
	     vclock = vclockset_next(&memtx->snap_dir.index, vclock)) {
		gc_add_checkpoint(vclock);
	}

	stailq_create(&memtx->gc_queue);
	memtx->gc_fiber = fiber_new_system("memtx.gc", memtx_engine_gc_f);
	if (memtx->gc_fiber == NULL)
		goto fail;

	/* Apply lowest allowed objsize bound. */
	if (objsize_min < OBJSIZE_MIN)
		objsize_min = OBJSIZE_MIN;

	if (alloc_factor > 2) {
		say_error("Alloc factor must be less than or equal to 2.0. It "
			  "will be reduced to 2.0");
		alloc_factor = 2.0;
	} else if (alloc_factor <= 1.0) {
		say_error("Alloc factor must be greater than 1.0. It will be "
			  "increased to 1.001");
		alloc_factor = 1.001;
	}

	/* Initialize tuple allocator. */
	quota_init(&memtx->quota, tuple_arena_max_size);
	tuple_arena_create(&memtx->arena, &memtx->quota, tuple_arena_max_size,
			   SLAB_SIZE, dontdump, "memtx");
	slab_cache_create(&memtx->slab_cache, &memtx->arena);
	float actual_alloc_factor;
	allocator_settings alloc_settings;
	allocator_settings_init(&alloc_settings, &memtx->slab_cache,
				objsize_min, granularity, alloc_factor,
				&actual_alloc_factor, &memtx->quota);
	memtx_allocators_init(&alloc_settings);
	memtx_set_tuple_format_vtab(allocator);

	say_info("Actual slab_alloc_factor calculated on the basis of desired "
		 "slab_alloc_factor = %f", actual_alloc_factor);

	/* Initialize index extent allocator. */
	slab_cache_create(&memtx->index_slab_cache, &memtx->arena);
	mempool_create(&memtx->index_extent_pool, &memtx->index_slab_cache,
		       MEMTX_EXTENT_SIZE);
	mempool_create(&memtx->iterator_pool, cord_slab_cache(),
		       MEMTX_ITERATOR_SIZE);
	memtx->num_reserved_extents = 0;
	memtx->reserved_extents = NULL;

	memtx->state = MEMTX_INITIALIZED;
	memtx->max_tuple_size = MAX_TUPLE_SIZE;
	memtx->force_recovery = force_recovery;

	memtx->replica_join_cord = NULL;

	memtx->base.vtab = &memtx_engine_vtab;
	memtx->base.name = "memtx";
	memtx->base.flags = ENGINE_SUPPORTS_READ_VIEW;

	memtx->func_key_format = simple_tuple_format_new(
		&memtx_tuple_format_vtab, &memtx->base, NULL, 0);
	if (memtx->func_key_format == NULL) {
		diag_log();
		panic("failed to create functional index key format");
	}
	tuple_format_ref(memtx->func_key_format);

	memtx->on_indexes_built_cb = on_indexes_built;

	fiber_start(memtx->gc_fiber, memtx);
	return memtx;
fail:
	xdir_destroy(&memtx->snap_dir);
	free(memtx);
	return NULL;
}

void
memtx_engine_schedule_gc(struct memtx_engine *memtx,
			 struct memtx_gc_task *task)
{
	stailq_add_tail_entry(&memtx->gc_queue, task, link);
	fiber_wakeup(memtx->gc_fiber);
}

void
memtx_engine_set_snap_io_rate_limit(struct memtx_engine *memtx, double limit)
{
	memtx->snap_io_rate_limit = limit * 1024 * 1024;
}

int
memtx_engine_set_memory(struct memtx_engine *memtx, size_t size)
{
	if (size < quota_total(&memtx->quota)) {
		diag_set(ClientError, ER_CFG, "memtx_memory",
			 "cannot decrease memory size at runtime");
		return -1;
	}
	quota_set(&memtx->quota, size);
	return 0;
}

void
memtx_engine_set_max_tuple_size(struct memtx_engine *memtx, size_t max_size)
{
	memtx->max_tuple_size = max_size;
}

template<class ALLOC>
static struct tuple *
memtx_tuple_new_raw_impl(struct tuple_format *format, const char *data,
			 const char *end, bool validate)
{
	struct memtx_engine *memtx = (struct memtx_engine *)format->engine;
	assert(mp_typeof(*data) == MP_ARRAY);
	struct tuple *tuple = NULL;
	struct region *region = &fiber()->gc;
	size_t region_svp = region_used(region);
	struct field_map_builder builder;
	size_t total, tuple_len;
	uint32_t data_offset, field_map_size;
	char *raw;
	bool make_compact;
	if (tuple_field_map_create(format, data, validate, &builder) != 0)
		goto end;
	field_map_size = field_map_build_size(&builder);
	data_offset = sizeof(struct tuple) + field_map_size;
	if (tuple_check_data_offset(data_offset) != 0)
		goto end;

	tuple_len = end - data;
	assert(tuple_len <= UINT32_MAX); /* bsize is UINT32_MAX */
	total = sizeof(struct tuple) + field_map_size + tuple_len;

	make_compact = tuple_can_be_compact(data_offset, tuple_len);
	if (make_compact) {
		data_offset -= TUPLE_COMPACT_SAVINGS;
		total -= TUPLE_COMPACT_SAVINGS;
	}

	ERROR_INJECT(ERRINJ_TUPLE_ALLOC, {
		diag_set(OutOfMemory, total, "slab allocator", "memtx_tuple");
		goto end;
	});
	if (unlikely(total > memtx->max_tuple_size)) {
		diag_set(ClientError, ER_MEMTX_MAX_TUPLE_SIZE, total);
		error_log(diag_last_error(diag_get()));
		goto end;
	}

	while ((tuple = MemtxAllocator<ALLOC>::alloc_tuple(total)) == NULL) {
		bool stop;
		memtx_engine_run_gc(memtx, &stop);
		if (stop)
			break;
	}
	if (tuple == NULL) {
		diag_set(OutOfMemory, total, "slab allocator", "memtx_tuple");
		goto end;
	}
	tuple_create(tuple, 0, tuple_format_id(format),
		     data_offset, tuple_len, make_compact);
	if (format->is_temporary)
		tuple_set_flag(tuple, TUPLE_IS_TEMPORARY);
	tuple_format_ref(format);
	raw = (char *) tuple + data_offset;
	field_map_build(&builder, raw - field_map_size);
	memcpy(raw, data, tuple_len);
	say_debug("%s(%zu) = %p", __func__, tuple_len, tuple);
end:
	region_truncate(region, region_svp);
	return tuple;
}

template<class ALLOC>
static inline struct tuple *
memtx_tuple_new(struct tuple_format *format, const char *data, const char *end)
{
	return memtx_tuple_new_raw_impl<ALLOC>(format, data, end, true);
}

template<class ALLOC>
static void
memtx_tuple_delete(struct tuple_format *format, struct tuple *tuple)
{
	assert(tuple_is_unreferenced(tuple));
	say_debug("%s(%p)", __func__, tuple);
	MemtxAllocator<ALLOC>::free_tuple(tuple);
	tuple_format_unref(format);
}

struct tuple_format_vtab memtx_tuple_format_vtab;

template <class ALLOC>
static inline void
create_memtx_tuple_format_vtab(struct tuple_format_vtab *vtab)
{
	vtab->tuple_delete = memtx_tuple_delete<ALLOC>;
	vtab->tuple_new = memtx_tuple_new<ALLOC>;
}

/**
 * Allocate a block of size MEMTX_EXTENT_SIZE for memtx index
 */
void *
memtx_index_extent_alloc(void *ctx)
{
	struct memtx_engine *memtx = (struct memtx_engine *)ctx;
	if (memtx->reserved_extents) {
		assert(memtx->num_reserved_extents > 0);
		memtx->num_reserved_extents--;
		void *result = memtx->reserved_extents;
		memtx->reserved_extents = *(void **)memtx->reserved_extents;
		return result;
	}
	ERROR_INJECT(ERRINJ_INDEX_ALLOC, {
		/* same error as in mempool_alloc */
		diag_set(OutOfMemory, MEMTX_EXTENT_SIZE,
			 "mempool", "new slab");
		return NULL;
	});
	void *ret;
	while ((ret = mempool_alloc(&memtx->index_extent_pool)) == NULL) {
		bool stop;
		memtx_engine_run_gc(memtx, &stop);
		if (stop)
			break;
	}
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
	struct memtx_engine *memtx = (struct memtx_engine *)ctx;
	return mempool_free(&memtx->index_extent_pool, extent);
}

/**
 * Reserve num extents in pool.
 * Ensure that next num extent_alloc will succeed w/o an error
 */
int
memtx_index_extent_reserve(struct memtx_engine *memtx, int num)
{
	ERROR_INJECT(ERRINJ_INDEX_ALLOC, {
		/* same error as in mempool_alloc */
		diag_set(OutOfMemory, MEMTX_EXTENT_SIZE,
			 "mempool", "new slab");
		return -1;
	});
	struct mempool *pool = &memtx->index_extent_pool;
	while (memtx->num_reserved_extents < num) {
		void *ext;
		while ((ext = mempool_alloc(pool)) == NULL) {
			bool stop;
			memtx_engine_run_gc(memtx, &stop);
			if (stop)
				break;
		}
		if (ext == NULL) {
			diag_set(OutOfMemory, MEMTX_EXTENT_SIZE,
				 "mempool", "new slab");
			return -1;
		}
		*(void **)ext = memtx->reserved_extents;
		memtx->reserved_extents = ext;
		memtx->num_reserved_extents++;
	}
	return 0;
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
	if (old_def->opts.func_id != new_def->opts.func_id)
		return true;
	if (old_def->opts.hint != new_def->opts.hint)
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
		if (json_path_cmp(old_part->path, old_part->path_len,
				  new_part->path, new_part->path_len,
				  TUPLE_INDEX_BASE) != 0)
			return true;
		if (old_part->exclude_null != new_part->exclude_null)
			return true;
	}
	assert(old_cmp_def->is_multikey == new_cmp_def->is_multikey);
	return false;
}

int
memtx_prepare_result_tuple(struct tuple **result)
{
	if (*result != NULL) {
		*result = memtx_tuple_decompress(*result);
		if (*result == NULL)
			return -1;
		tuple_bless(*result);
	}
	return 0;
}

int
memtx_prepare_read_view_tuple(struct tuple *tuple,
			      struct memtx_tx_snapshot_cleaner *cleaner,
			      bool disable_decompression,
			      const char **data, uint32_t *size)
{
	tuple = memtx_tx_snapshot_clarify(cleaner, tuple);
	if (tuple == NULL) {
		*data = NULL;
		*size = 0;
		return 0;
	}
	*data = tuple_data_range(tuple, size);
	if (!disable_decompression) {
		*data = memtx_tuple_decompress_raw(*data, *data + *size, size);
		if (*data == NULL)
			return -1;
	}
	return 0;
}

int
memtx_index_get(struct index *index, const char *key, uint32_t part_count,
		struct tuple **result)
{
	if (index->vtab->get_internal(index, key, part_count, result) != 0)
		return -1;
	return memtx_prepare_result_tuple(result);
}

int
memtx_iterator_next(struct iterator *it, struct tuple **ret)
{
	if (it->next_internal(it, ret) != 0)
		return -1;
	return memtx_prepare_result_tuple(ret);
}
