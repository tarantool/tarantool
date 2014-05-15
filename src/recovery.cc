/*
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
#define MH_SOURCE 1
#include "recovery.h"

#include <fcntl.h>

#include "log_io.h"
#include "fiber.h"
#include "tt_pthread.h"
#include "fio.h"
#include "sio.h"
#include "errinj.h"
#include "bootstrap.h"

#include "replica.h"
#include "fiber.h"
#include "msgpuck/msgpuck.h"
#include "iproto_constants.h"
#include "crc32.h"
#include "scoped_guard.h"
#include "box/cluster.h"

/*
 * Recovery subsystem
 * ------------------
 *
 * A facade of the recovery subsystem is struct recovery_state,
 * which is a singleton.
 *
 * Depending on the configuration, start-up parameters, the
 * actual task being performed, the recovery can be
 * in a different state.
 *
 * The main factors influencing recovery state are:
 * - temporal: whether or not the instance is just booting
 *   from a snapshot, is in 'local hot standby mode', or
 *   is already accepting requests
 * - topological: whether or not it is a master instance
 *   or a replica
 * - task based: whether it's a master process,
 *   snapshot saving process or a replication relay.
 *
 * Depending on the above factors, recovery can be in two main
 * operation modes: "read mode", recovering in-memory state
 * from existing data, and "write mode", i.e. recording on
 * disk changes of the in-memory state.
 *
 * Let's enumerate all possible distinct states of recovery:
 *
 * Read mode
 * ---------
 * IR - initial recovery, initiated right after server start:
 * reading data from the snapshot and existing WALs
 * and restoring the in-memory state
 * IRR - initial replication relay mode, reading data from
 * existing WALs (xlogs) and sending it to the client.
 *
 * HS - standby mode, entered once all existing WALs are read:
 * following the WAL directory for all changes done by the master
 * and updating the in-memory state
 * RR - replication relay, following the WAL directory for all
 * changes done by the master and sending them to the
 * replica
 *
 * Write mode
 * ----------
 * M - master mode, recording in-memory state changes in the WAL
 * R - replica mode, receiving changes from the master and
 * recording them in the WAL
 * S - snapshot mode, writing entire in-memory state to a compact
 * snapshot file.
 *
 * The following state transitions are possible/supported:
 *
 * recovery_init() -> IR | IRR # recover()
 * IR -> HS         # recovery_follow_local()
 * IRR -> RR        # recovery_follow_local()
 * HS -> M          # recovery_finalize()
 * M -> R           # recovery_follow_remote()
 * R -> M           # recovery_stop_remote()
 * M -> S           # snapshot()
 * R -> S           # snapshot()
 */

struct recovery_state *recovery_state;

const char *wal_mode_STRS[] = { "none", "write", "fsync", "fsync_delay", NULL };

/* {{{ mh_cluster definition */

/** Removes all nodes from mhash */
void
mh_cluster_clean(struct mh_cluster_t *hash)
{
	while (mh_size(hash) > 0) {
		mh_int_t k = mh_first(hash);
		struct node *node = *mh_cluster_node(hash, k);
		mh_cluster_del(hash, k, NULL);
		free(node);
	}
}

/** Gets or creates a node */
struct node *
mh_cluster_fetch(struct mh_cluster_t *hash, uint32_t node_id)
{
	uint32_t k = mh_cluster_find(hash, node_id, NULL);
	if (k != mh_end(hash))
		return *mh_cluster_node(hash, k);

	/* Create node if it doesn't exist */
	struct node *node = (struct node *) calloc(1, sizeof(*node));
	if (node == NULL)
		return NULL;
	node->id = node_id;
	k = mh_cluster_put(hash, (const struct node **) &node, NULL, NULL);
	if (k == mh_end(hash))
		return NULL;
	return node;
}

/** Calculates sum([node.current_lsn]) */
static int64_t
mh_cluster_current_sum(struct mh_cluster_t *cluster)
{
	int64_t sum = 0;
	uint32_t k;
	mh_foreach(cluster, k) {
		struct node *node = *mh_cluster_node(cluster, k);
		sum += node->current_lsn;
	}

	return sum;
}

/* }}} */

/* {{{ LSN API */

static struct node *
fill_lsn(struct recovery_state *r, struct iproto_packet *packet)
{
	struct node *node = r->local_node;
	assert(packet != NULL || node != NULL);
	if (packet == NULL || packet->node_id == 0) {
		/* Local request */
		if (node == NULL)
			tnt_raise(ClientError, ER_LOCAL_NODE_IS_NOT_ACTIVE);
		++node->current_lsn;
		if (packet != NULL) {
			packet->lsn = node->current_lsn;
			packet->node_id = node->id;
		}
	} else {
		/* Remote request */
		uint32_t k = mh_cluster_find(r->cluster, packet->node_id, NULL);
		if (k == mh_end(r->cluster))
			tnt_raise(ClientError, ER_UNKNOWN_NODE, packet->node_id);
		node = *mh_cluster_node(r->cluster, k);
		if (node->current_lsn >= packet->lsn) {
			tnt_raise(ClientError, ER_INVALID_ORDER,
				  node->id, (long long) node->current_lsn,
				  (long long) packet->lsn);
		} else if (node->current_lsn + 1 != packet->lsn) {
			say_warn("non consecutive LSN for node %u (%s) "
				 "confirmed: %lld, new: %lld, diff: %lld",
				 (unsigned) node->id,
				 tt_uuid_str(&node->uuid),
				 (long long) node->current_lsn,
				 (long long) packet->lsn,
				 (long long) (packet->lsn - node->current_lsn));
		}
		node->current_lsn = packet->lsn;
	}

	return node;
}

/* }}} */

/* {{{ Initial recovery */

static int
wal_writer_start(struct recovery_state *state);
void
wal_writer_stop(struct recovery_state *r);
static void
recovery_stop_local(struct recovery_state *r);

void
recovery_init(const char *snap_dirname, const char *wal_dirname,
	      row_handler row_handler, void *row_handler_param,
	      snapshot_handler snapshot_handler, join_handler join_handler,
	      int rows_per_wal)
{
	assert(recovery_state == NULL);
	recovery_state = (struct recovery_state *) calloc(1, sizeof(struct recovery_state));
	struct recovery_state *r = recovery_state;
	recovery_update_mode(r, WAL_NONE);
	recovery_update_fsync_delay(r, 0);

	assert(rows_per_wal > 1);

	r->row_handler = row_handler;
	r->row_handler_param = row_handler_param;

	r->snapshot_handler = snapshot_handler;
	r->join_handler = join_handler;

	log_dir_create(&r->snap_dir);
	r->snap_dir.panic_if_error = false;
	r->snap_dir.sync_is_async = false;
	strcpy(r->snap_dir.open_wflags, "wxd");
	r->snap_dir.filetype = "SNAP\n";
	r->snap_dir.filename_ext = ".snap";
	r->snap_dir.dirname = strdup(snap_dirname);
	r->snap_dir.mode = 0660;
	r->snap_dir.ignore_initial_setlsn = true;

	log_dir_create(&r->wal_dir);
	r->wal_dir.panic_if_error = false;
	r->wal_dir.sync_is_async = true;
	strcpy(r->wal_dir.open_wflags, "wx");
	r->wal_dir.filetype = "XLOG\n";
	r->wal_dir.filename_ext = ".xlog";
	r->wal_dir.dirname = strdup(wal_dirname);
	r->wal_dir.mode = 0660;

	if (r->wal_mode == WAL_FSYNC) {
		(void) strcat(r->wal_dir.open_wflags, "s");
	}
	r->rows_per_wal = rows_per_wal;

	r->cluster = mh_cluster_new();
	if (r->cluster == NULL)
		panic("cannot reallocate r->cluster");

	/* Add a fake node for snapshot/bootstrap */
	struct node *node = (struct node *) calloc(1, sizeof(*node));
	if (node == NULL)
		panic("cannot allocate struct node");
	node->id = 0;
	assert(tt_uuid_is_nil(&node->uuid));
	uint32_t k = mh_cluster_put(r->cluster,
		(const struct node **) &node, NULL, NULL);
	if (k == mh_end(r->cluster))
		panic("cannot reallocate r->cluster");
	r->local_node = node;

	if (log_dir_scan(&r->snap_dir) != 0)
		panic("can't scan snap directory");
	if (log_dir_scan(&r->wal_dir) != 0)
		panic("can't scan wal directory");
}

void
recovery_update_mode(struct recovery_state *r, enum wal_mode mode)
{
	assert(mode < WAL_MODE_MAX);
	r->wal_mode = mode;
}

void
recovery_update_fsync_delay(struct recovery_state *r, double new_delay)
{
	/* No mutex lock: let's not bother with whether
	 * or not a WAL writer thread is present, and
	 * if it's present, the delay will be propagated
	 * to it whenever there is a next lock/unlock of
	 * wal_writer->mutex.
	 */
	r->wal_fsync_delay = new_delay;
}

void
recovery_update_io_rate_limit(struct recovery_state *r, double new_limit)
{
	r->snap_io_rate_limit = new_limit * 1024 * 1024;
	if (r->snap_io_rate_limit == 0)
		r->snap_io_rate_limit = UINT64_MAX;
}

void
recovery_free()
{
	struct recovery_state *r = recovery_state;
	if (r == NULL)
		return;

	if (r->watcher)
		recovery_stop_local(r);

	if (r->writer)
		wal_writer_stop(r);

	log_dir_destroy(&r->snap_dir);
	log_dir_destroy(&r->wal_dir);
	if (r->current_wal) {
		/*
		 * Possible if shutting down a replication
		 * relay or if error during startup.
		 */
		log_io_close(&r->current_wal);
	}

	mh_cluster_clean(r->cluster);
	mh_cluster_delete(r->cluster);

	recovery_state = NULL;
}

void
recovery_setup_panic(struct recovery_state *r, bool on_snap_error, bool on_wal_error)
{
	r->wal_dir.panic_if_error = on_wal_error;
	r->snap_dir.panic_if_error = on_snap_error;
}

static void
recovery_process_setlsn(struct recovery_state *r, struct iproto_packet *packet)
{
	say_debug("SETLSN");
	uint32_t row_count;
	struct log_setlsn_row *rows = log_decode_setlsn(packet, &row_count);
	auto rows_guard = make_scoped_guard([=]{
		free(rows);
	});

	for (uint32_t i = 0; i < row_count; i++) {
		uint32_t k = mh_cluster_find(r->cluster, rows[i].node_id, NULL);
		if (k == mh_end(r->cluster))
			tnt_raise(ClientError, ER_UNKNOWN_NODE, rows[i].node_id);

		struct node *node = *mh_cluster_node(r->cluster, k);

		if (node->current_lsn <= rows[i].lsn) {
			say_debug("setting\t(%2u, %020lld)",
				  node->id, (long long) rows[i].lsn);
			node->current_lsn = rows[i].lsn;
		} else {
			/* Ignore outdated SETLSN rows */
			say_debug("skipping\t(%2u, %020lld)",
				  node->id, (long long) rows[i].lsn);
		}
	}
	say_debug("--");
}

void
recovery_process(struct recovery_state *r, struct iproto_packet *packet)
{
	if (r->relay)
		return r->row_handler(r->row_handler_param, packet);

	if (!iproto_request_is_dml(packet->code)) {
		/* Process admin commands (node_id, lsn are ignored) */
		switch (packet->code) {
		case IPROTO_SETLSN:
			recovery_process_setlsn(r, packet);
			break;
		default:
			tnt_raise(ClientError, ER_UNKNOWN_REQUEST_TYPE,
				  packet->code);
		}
		return;
	}

	/* Check node_id and lsn */
	uint32_t k = mh_cluster_find(r->cluster, packet->node_id, NULL);
	if (k != mh_end(r->cluster)) {
		struct node *node = *mh_cluster_node(r->cluster, k);
		if (packet->lsn <= node->current_lsn) {
			say_debug("skipping too young row");
			return;
		}
	} else {
		say_warn("skipping row with unknown node_id");
		return;
	}

	return r->row_handler(r->row_handler_param, packet);
}

void
cluster_bootstrap(struct recovery_state *r)
{
	/* Generate Node-UUID */
	tt_uuid_create(&r->node_uuid);

	/* Recover from bootstrap.snap */
	say_info("initializing cluster");
	FILE *f = fmemopen((void *) &bootstrap_bin,
			   sizeof(bootstrap_bin), "r");
	tt_uuid bootstrap_uuid; /* ignored */
	struct log_io *snap = log_io_open(&r->snap_dir, LOG_READ,
		"bootstrap.snap", &bootstrap_uuid, NONE, f);
	assert(snap != NULL);
	auto snap_guard = make_scoped_guard([&]{
		log_io_close(&snap);
	});

	int rc = recover_wal(r, snap);

	if (rc != 0)
		panic("failed to bootstrap data directory");

	/* Initialize local node */
	r->join_handler(&r->node_uuid);
	assert(r->local_node != NULL);
	assert(r->local_node->id == 1);
	assert(tt_uuid_cmp(&r->local_node->uuid, &r->node_uuid) == 0);

	say_info("done");
}

/**
 * Read a snapshot and call row_handler for every snapshot row.
 * Panic in case of error.
 */
void
recover_snap(struct recovery_state *r)
{
	/*  current_wal isn't open during initial recover. */
	assert(r->current_wal == NULL);
	say_info("recovery start");

	struct log_io *snap;
	int64_t lsn;

	if (log_dir_scan(&r->snap_dir) != 0) {
		say_error("can't find snapshot");
		goto error;
	}
	lsn = log_dir_greatest(&r->snap_dir);
	if (lsn <= 0) {
		say_error("can't find snapshot");
		goto error;
	}
	snap = log_io_open_for_read(&r->snap_dir, lsn, &r->node_uuid, NONE);
	if (snap == NULL) {
		say_error("can't find/open snapshot");
		goto error;
	}

	if (tt_uuid_is_nil(&r->node_uuid)) {
		say_error("can't find node uuid in snapshot");
		goto error;
	}

	say_info("recover from `%s'", snap->filename);
	if (recover_wal(r, snap) == 0)
		return;
error:
	if (log_dir_greatest(&r->snap_dir) <= 0) {
		say_crit("didn't you forget to initialize storage with --init-storage switch?");
		_exit(1);
	}
	panic("snapshot recovery failed");
}

#define LOG_EOF 0

/**
 * @retval -1 error
 * @retval 0 EOF
 * @retval 1 ok, maybe read something
 */
int
recover_wal(struct recovery_state *r, struct log_io *l)
{
	int res = -1;
	struct log_io_cursor i;

	log_io_cursor_open(&i, l);

	struct iproto_packet packet;
	while (log_io_cursor_next(&i, &packet) == 0) {
		/*
		 * After handler(row) returned, row may be
		 * modified, do not use it.
		 */
		try {
			recovery_process(r, &packet);
		} catch (SocketError *e) {
			say_error("can't apply row: %s", e->errmsg());
			goto end;
		} catch (Exception *e) {
			say_error("can't apply row: %s", e->errmsg());
			if (l->dir->panic_if_error)
				goto end;
		}
	}
	res = i.eof_read ? LOG_EOF : 1;
end:
	log_io_cursor_close(&i);
	/* Sic: we don't close the log here. */
	return res;
}

/** Find out if there are new .xlog files since the current
 * LSN, and read them all up.
 *
 * This function will not close r->current_wal if
 * recovery was successful.
 */
static int
recover_remaining_wals(struct recovery_state *r)
{
	int result = 0;
	struct log_io *next_wal;
	int64_t current_lsn, wal_greatest_lsn;
	size_t rows_before;
	FILE *f;
	char *filename;
	enum log_suffix suffix;

	if (log_dir_scan(&r->wal_dir) != 0)
		return -1;

	wal_greatest_lsn = log_dir_greatest(&r->wal_dir);
	/* if the caller already opened WAL for us, recover from it first */
	if (r->current_wal != NULL)
		goto recover_current_wal;

	while (1) {
find_next_wal:
		current_lsn = log_dir_next(&r->wal_dir, r->cluster);
		if (current_lsn == INT64_MAX)
			break; /* No more WALs */

		if (current_lsn == r->lsnsum) {
			if (current_lsn != wal_greatest_lsn) {
				say_error("missing xlog between %020lld and %020lld",
					  (long long) current_lsn,
					  (long long) wal_greatest_lsn);
			}
			break;
		}

		/*
		 * For the last WAL, first try to open .inprogress
		 * file: if it doesn't exist, we can safely try an
		 * .xlog, with no risk of a concurrent
		 * inprogress_log_rename().
		 */
		f = NULL;
		suffix = INPROGRESS;
		if (current_lsn == wal_greatest_lsn) {
			/* Last WAL present at the time of rescan. */
			filename = format_filename(&r->wal_dir,
						   current_lsn, suffix);
			f = fopen(filename, "r");
		}
		if (f == NULL) {
			suffix = NONE;
			filename = format_filename(&r->wal_dir,
						   current_lsn, suffix);
			f = fopen(filename, "r");
			/*
			 * Try finding wal for the next lsn if there is a
			 * gap in LSNs.
			 */
			if (f == NULL && errno == ENOENT &&
			    current_lsn < wal_greatest_lsn)
				goto find_next_wal;
		}
		next_wal = log_io_open(&r->wal_dir, LOG_READ, filename,
				       &r->node_uuid, suffix, f);
		/*
		 * When doing final recovery, and dealing with the
		 * last file, try opening .<ext>.inprogress.
		 */
		if (next_wal == NULL) {
			say_warn("open fail: %lld",
				 (long long) current_lsn);
			if (r->finalize && suffix == INPROGRESS) {
				/*
				 * There is an .inprogress file, but
				 * we failed to open it. Try to
				 * delete it.
				 */
				say_warn("unlink broken %s WAL", filename);
				if (inprogress_log_unlink(filename) != 0)
					panic("can't unlink 'inprogres' WAL");
			}
			result = 0;
			break;
		}
		assert(r->current_wal == NULL);
		r->lsnsum = current_lsn;
		r->current_wal = next_wal;
		say_info("recover from `%s'", r->current_wal->filename);

recover_current_wal:
		rows_before = r->current_wal->rows;
		result = recover_wal(r, r->current_wal);
		if (result < 0) {
			say_error("failure reading from %s",
				  r->current_wal->filename);
			break;
		}

		if (r->current_wal->rows > 0 &&
		    r->current_wal->rows != rows_before) {
			r->current_wal->retry = 0;
		}
		/* rows == 0 could indicate an empty WAL */
		if (r->current_wal->rows == 0) {
			say_error("read zero records from %s",
				  r->current_wal->filename);
			break;
		}
		if (result == LOG_EOF) {
			say_info("done `%s'", r->current_wal->filename);
			log_io_close(&r->current_wal);
			/* goto find_next_wal; */
		} else if (r->lsnsum == wal_greatest_lsn) {
			/* last file is not finished */
			break;
		} else if (r->finalize && r->current_wal->is_inprogress) {
			say_warn("fail to find eof on inprogress");
			/* Let recovery_finalize deal with last file */
			break;
		} else if (r->current_wal->retry++ < 3) {
			/*
			 * If a newer WAL appeared in the directory before
			 * current_wal was fully read, try re-reading
			 * one last time. */
			say_warn("`%s' has no EOF marker, yet a newer WAL file exists:"
				 " trying to re-read (attempt #%d)",
				 r->current_wal->filename, r->current_wal->retry);
			goto recover_current_wal;
		} else {
			say_warn("WAL `%s' wasn't correctly closed",
				 r->current_wal->filename);
			log_io_close(&r->current_wal);
		}
	}

	/*
	 * It's not a fatal error when last WAL is empty, but if
	 * we lose some logs it is a fatal error.
	 */
	if (wal_greatest_lsn > r->lsnsum) {
		say_error("not all WALs have been successfully read");
		result = -1;
	}

#if 0
	region_free(&fiber()->gc);
#endif
	return result;
}

void
recovery_fix_lsn(struct recovery_state *r, bool master_bootstrap)
{
	/* Remove fake snapshot/bootstrap node */
	uint32_t k = mh_cluster_find(r->cluster, 0, NULL);
	assert(k != mh_end(r->cluster));
	struct node *node = *mh_cluster_node(r->cluster, k);
	if (master_bootstrap) {
		assert(r->local_node != NULL);
		r->local_node->current_lsn += node->current_lsn;
	}
	mh_cluster_del(r->cluster, k, NULL);
	if (r->local_node == node)
		r->local_node = NULL;
	free(node);
}

void
recovery_finalize(struct recovery_state *r)
{
	int result;

	if (r->watcher)
		recovery_stop_local(r);

	r->finalize = true;

	result = recover_remaining_wals(r);

	if (result < 0)
		panic("unable to successfully finalize recovery");

	if (r->current_wal != NULL && result != LOG_EOF) {
		say_warn("WAL `%s' wasn't correctly closed", r->current_wal->filename);

		if (!r->current_wal->is_inprogress) {
			if (r->current_wal->rows == 0)
			        /* Regular WAL (not inprogress) must contain at least one row */
				panic("zero rows was successfully read from last WAL `%s'",
				      r->current_wal->filename);
		} else if (r->current_wal->rows == 0) {
			/* Unlink empty inprogress WAL */
			say_warn("unlink broken %s WAL", r->current_wal->filename);
			if (inprogress_log_unlink(r->current_wal->filename) != 0)
				panic("can't unlink 'inprogress' WAL");
		} else if (r->current_wal->rows <= 2 /* SETLSN + one row */) {
			/* Rename inprogress wal with one row */
			say_warn("rename unfinished %s WAL", r->current_wal->filename);
			if (inprogress_log_rename(r->current_wal) != 0)
				panic("can't rename 'inprogress' WAL");
		} else
			panic("too many rows in inprogress WAL `%s'", r->current_wal->filename);

		log_io_close(&r->current_wal);
	}

	wal_writer_start(r);
}


/* }}} */

/* {{{ Local recovery: support of hot standby and replication relay */

/**
 * This is used in local hot standby or replication
 * relay mode: look for changes in the wal_dir and apply them
 * locally or send to the replica.
 */
struct wal_watcher {
	/**
	 * Rescan the WAL directory in search for new WAL files
	 * every wal_dir_rescan_delay seconds.
	 */
	ev_timer dir_timer;
	/**
	 * When the latest WAL does not contain a EOF marker,
	 * re-read its tail on every change in file metadata.
	 */
	ev_stat stat;
	/** Path to the file being watched with 'stat'. */
	char filename[PATH_MAX+1];
};

static struct wal_watcher wal_watcher;

static void recovery_rescan_file(ev_loop *, ev_stat *w, int /* revents */);

static void
recovery_watch_file(ev_loop *loop, struct wal_watcher *watcher,
		    struct log_io *wal)
{
	strncpy(watcher->filename, wal->filename, PATH_MAX);
	ev_stat_init(&watcher->stat, recovery_rescan_file,
		     watcher->filename, 0.);
	ev_stat_start(loop, &watcher->stat);
}

static void
recovery_stop_file(struct wal_watcher *watcher)
{
	ev_stat_stop(loop(), &watcher->stat);
}

static void
recovery_rescan_dir(ev_loop * loop, ev_timer *w, int /* revents */)
{
	struct recovery_state *r = (struct recovery_state *) w->data;
	struct wal_watcher *watcher = r->watcher;
	struct log_io *save_current_wal = r->current_wal;

	int result = recover_remaining_wals(r);
	if (result < 0)
		panic("recover failed: %i", result);
	if (save_current_wal != r->current_wal) {
		if (save_current_wal != NULL)
			recovery_stop_file(watcher);
		if (r->current_wal != NULL)
			recovery_watch_file(loop, watcher, r->current_wal);
	}
}

static void
recovery_rescan_file(ev_loop * loop, ev_stat *w, int /* revents */)
{
	struct recovery_state *r = (struct recovery_state *) w->data;
	struct wal_watcher *watcher = r->watcher;
	int result = recover_wal(r, r->current_wal);
	if (result < 0)
		panic("recover failed");
	if (result == LOG_EOF) {
		say_info("done `%s'", r->current_wal->filename);
		log_io_close(&r->current_wal);
		recovery_stop_file(watcher);
		/* Don't wait for wal_dir_rescan_delay. */
		recovery_rescan_dir(loop, &watcher->dir_timer, 0);
	}
}

void
recovery_follow_local(struct recovery_state *r, ev_tstamp wal_dir_rescan_delay)
{
	assert(r->watcher == NULL);
	assert(r->writer == NULL);
	ev_loop *loop = loop();

	struct wal_watcher  *watcher = r->watcher= &wal_watcher;

	ev_timer_init(&watcher->dir_timer, recovery_rescan_dir,
		      wal_dir_rescan_delay, wal_dir_rescan_delay);
	watcher->dir_timer.data = watcher->stat.data = r;
	ev_timer_start(loop, &watcher->dir_timer);
	/*
	 * recover() leaves the current wal open if it has no
	 * EOF marker.
	 */
	if (r->current_wal != NULL)
		recovery_watch_file(loop, watcher, r->current_wal);
}

static void
recovery_stop_local(struct recovery_state *r)
{
	struct wal_watcher *watcher = r->watcher;
	assert(ev_is_active(&watcher->dir_timer));
	ev_timer_stop(loop(), &watcher->dir_timer);
	if (ev_is_active(&watcher->stat))
		ev_stat_stop(loop(), &watcher->stat);

	r->watcher = NULL;
}

/* }}} */

/* {{{ WAL writer - maintain a Write Ahead Log for every change
 * in the data state.
 */

struct wal_write_request {
	STAILQ_ENTRY(wal_write_request) wal_fifo_entry;
	/* Auxiliary. */
	int64_t prev_lsn;
	struct fiber *fiber;
	struct iproto_packet *packet;
	char wal_fixheader[XLOG_FIXHEADER_SIZE];
	struct node *node;
};

/* Context of the WAL writer thread. */
STAILQ_HEAD(wal_fifo, wal_write_request);

struct wal_writer
{
	struct wal_fifo input;
	struct wal_fifo commit;
	struct cord cord;
	pthread_mutex_t mutex;
	pthread_cond_t cond;
	ev_async write_event;
	struct fio_batch *batch;
	bool is_shutdown;
	bool is_rollback;
	ev_loop *txn_loop;
	struct mh_cluster_t *cluster;
};

static pthread_once_t wal_writer_once = PTHREAD_ONCE_INIT;

static struct wal_writer wal_writer;

/**
 * A pthread_atfork() callback for a child process. Today we only
 * fork the master process to save a snapshot, and in the child
 * the WAL writer thread is not necessary and not present.
 */
static void
wal_writer_child()
{
	log_io_atfork(&recovery_state->current_wal);
	if (wal_writer.batch) {
		free(wal_writer.batch);
		wal_writer.batch = NULL;
	}
	/*
	 * Make sure that atexit() handlers in the child do
	 * not try to stop the non-existent thread.
	 * The writer is not used in the child.
	 */
	recovery_state->writer = NULL;
}

/**
 * Today a WAL writer is started once at start of the
 * server.  Nevertheless, use pthread_once() to make
 * sure we can start/stop the writer many times.
 */
static void
wal_writer_init_once()
{
	(void) tt_pthread_atfork(NULL, NULL, wal_writer_child);
}

/**
 * A commit watcher callback is invoked whenever there
 * are requests in wal_writer->commit. This callback is
 * associated with an internal WAL writer watcher and is
 * invoked in the front-end main event loop.
 *
 * A rollback watcher callback is invoked only when there is
 * a rollback request and commit is empty.
 * We roll back the entire input queue.
 *
 * ev_async, under the hood, is a simple pipe. The WAL
 * writer thread writes to that pipe whenever it's done
 * handling a pack of requests (look for ev_async_send()
 * call in the writer thread loop).
 */
static void
wal_schedule_queue(struct wal_fifo *queue)
{
	/*
	 * Can't use STAILQ_FOREACH since fiber_call()
	 * destroys the list entry.
	 */
	struct wal_write_request *req, *tmp;
	STAILQ_FOREACH_SAFE(req, queue, wal_fifo_entry, tmp)
		fiber_call(req->fiber);
}

static void
wal_schedule(ev_loop * /* loop */, ev_async *watcher, int /* event */)
{
	struct wal_writer *writer = (struct wal_writer *) watcher->data;
	struct wal_fifo commit = STAILQ_HEAD_INITIALIZER(commit);
	struct wal_fifo rollback = STAILQ_HEAD_INITIALIZER(rollback);

	(void) tt_pthread_mutex_lock(&writer->mutex);
	STAILQ_CONCAT(&commit, &writer->commit);
	if (writer->is_rollback) {
		STAILQ_CONCAT(&rollback, &writer->input);
		writer->is_rollback = false;
	}
	(void) tt_pthread_mutex_unlock(&writer->mutex);

	wal_schedule_queue(&commit);
	/*
	 * Perform a cascading abort of all transactions which
	 * depend on the transaction which failed to get written
	 * to the write ahead log. Abort transactions
	 * in reverse order, performing a playback of the
	 * in-memory database state.
	 */
	STAILQ_REVERSE(&rollback, wal_write_request, wal_fifo_entry);
	wal_schedule_queue(&rollback);
}

/**
 * Initialize WAL writer context. Even though it's a singleton,
 * encapsulate the details just in case we may use
 * more writers in the future.
 */
static void
wal_writer_init(struct wal_writer *writer, struct mh_cluster_t *cluster)
{
	/* I. Initialize the state. */
	pthread_mutexattr_t errorcheck;

	(void) tt_pthread_mutexattr_init(&errorcheck);

#ifndef NDEBUG
	(void) tt_pthread_mutexattr_settype(&errorcheck, PTHREAD_MUTEX_ERRORCHECK);
#endif
	/* Initialize queue lock mutex. */
	(void) tt_pthread_mutex_init(&writer->mutex, &errorcheck);
	(void) tt_pthread_mutexattr_destroy(&errorcheck);

	(void) tt_pthread_cond_init(&writer->cond, NULL);

	STAILQ_INIT(&writer->input);
	STAILQ_INIT(&writer->commit);

	ev_async_init(&writer->write_event, wal_schedule);
	writer->write_event.data = writer;
	writer->txn_loop = loop();

	(void) tt_pthread_once(&wal_writer_once, wal_writer_init_once);

	writer->batch = fio_batch_alloc(sysconf(_SC_IOV_MAX));

	if (writer->batch == NULL)
		panic_syserror("fio_batch_alloc");

	/* Create and fill writer->cluster hash */
	writer->cluster = mh_cluster_new();
	if (writer->cluster == NULL)
		panic_syserror("can't reallocate writer->cluster");
	uint32_t k;
	mh_foreach(cluster, k) {
		struct node *node = *mh_cluster_node(cluster, k);
		struct node *wnode = mh_cluster_fetch(writer->cluster,
						      node->id);
		if (wnode == NULL)
			panic_syserror("can't reallocate writer->cluster");
		wnode->current_lsn = node->current_lsn;
	}
}

/** Destroy a WAL writer structure. */
static void
wal_writer_destroy(struct wal_writer *writer)
{
	(void) tt_pthread_mutex_destroy(&writer->mutex);
	(void) tt_pthread_cond_destroy(&writer->cond);
	free(writer->batch);
	mh_cluster_clean(writer->cluster);
	mh_cluster_delete(writer->cluster);
}

/** WAL writer thread routine. */
static void *wal_writer_thread(void *worker_args);

/**
 * Initialize WAL writer, start the thread.
 *
 * @pre   The server has completed recovery from a snapshot
 *        and/or existing WALs. All WALs opened in read-only
 *        mode are closed.
 *
 * @param state			WAL writer meta-data.
 *
 * @return 0 success, -1 on error. On success, recovery->writer
 *         points to a newly created WAL writer.
 */
static int
wal_writer_start(struct recovery_state *r)
{
	assert(r->writer == NULL);
	assert(r->watcher == NULL);
	assert(r->current_wal == NULL);
	assert(! wal_writer.is_shutdown);
	assert(STAILQ_EMPTY(&wal_writer.input));
	assert(STAILQ_EMPTY(&wal_writer.commit));

	/* I. Initialize the state. */
	wal_writer_init(&wal_writer, r->cluster);
	r->writer = &wal_writer;

	ev_async_start(wal_writer.txn_loop, &wal_writer.write_event);

	/* II. Start the thread. */

	if (cord_start(&wal_writer.cord, "wal", wal_writer_thread, r)) {
		wal_writer_destroy(&wal_writer);
		r->writer = NULL;
		return -1;
	}
	return 0;
}

/** Stop and destroy the writer thread (at shutdown). */
void
wal_writer_stop(struct recovery_state *r)
{
	struct wal_writer *writer = r->writer;

	/* Stop the worker thread. */

	(void) tt_pthread_mutex_lock(&writer->mutex);
	writer->is_shutdown= true;
	(void) tt_pthread_cond_signal(&writer->cond);
	(void) tt_pthread_mutex_unlock(&writer->mutex);
	if (cord_join(&writer->cord)) {
		/* We can't recover from this in any reasonable way. */
		panic_syserror("WAL writer: thread join failed");
	}

	ev_async_stop(writer->txn_loop, &writer->write_event);
	wal_writer_destroy(writer);

	r->writer = NULL;
}

/**
 * Pop a bulk of requests to write to disk to process.
 * Block on the condition only if we have no other work to
 * do. Loop in case of a spurious wakeup.
 */
void
wal_writer_pop(struct wal_writer *writer, struct wal_fifo *input)
{
	while (! writer->is_shutdown)
	{
		if (! writer->is_rollback && ! STAILQ_EMPTY(&writer->input)) {
			STAILQ_CONCAT(input, &writer->input);
			break;
		}
		(void) tt_pthread_cond_wait(&writer->cond, &writer->mutex);
	}
}

int
wal_write_setlsn(struct log_io *wal, struct fio_batch *batch,
		 struct mh_cluster_t *cluster)
{
	/* Write SETLSN command */
	struct iproto_packet setlsn;
	char fixheader[XLOG_FIXHEADER_SIZE];
	struct iovec iov[XLOG_ROW_IOVMAX];
	log_encode_setlsn(&setlsn, cluster);
	int iovcnt = xlog_encode_row(&setlsn, iov, fixheader);
	fio_batch_start(batch, 1);
	fio_batch_add(batch, iov, iovcnt);
	if (fio_batch_write(batch, fileno(wal->f)) != 1) {
		say_error("wal_write_setlsn failed");
		return -1;
	}

	return 0;
}

/**
 * If there is no current WAL, try to open it, and close the
 * previous WAL. We close the previous WAL only after opening
 * a new one to smoothly move local hot standby and replication
 * over to the next WAL.
 * If the current WAL has only 1 record, it means we need to
 * rename it from '.inprogress' to '.xlog'. We maintain
 * '.inprogress' WALs to ensure that, at any point in time,
 * an .xlog file contains at least 1 valid record.
 * In case of error, we try to close any open WALs.
 *
 * @post r->current_wal is in a good shape for writes or is NULL.
 * @return 0 in case of success, -1 on error.
 */
static int
wal_opt_rotate(struct log_io **wal, struct fio_batch *batch,
	       struct recovery_state *r, struct mh_cluster_t *cluster)
{
	struct log_io *l = *wal, *wal_to_close = NULL;

	ERROR_INJECT_RETURN(ERRINJ_WAL_ROTATE);

	if (l != NULL && l->rows >= r->rows_per_wal) {
		/*
		 * if l->rows == 1, log_io_close() does
		 * inprogress_log_rename() for us.
		 */
		wal_to_close = l;
		l = NULL;
	}
	if (l == NULL) {
		/* Open WAL with '.inprogress' suffix. */
		int64_t lsnsum = mh_cluster_current_sum(cluster);
		l = log_io_open_for_write(&r->wal_dir, lsnsum, &r->node_uuid,
					  INPROGRESS);
		if (l != NULL) {
			if (wal_write_setlsn(l, batch, cluster) != 0)
				log_io_close(&l);
		}
		/*
		 * Close the file *after* we create the new WAL, since
		 * this is when replication relays get an inotify alarm
		 * (when we close the file), and try to reopen the next
		 * WAL. In other words, make sure that replication relays
		 * try to open the next WAL only when it exists.
		 */
		if (wal_to_close) {
			/*
			 * We can not handle log_io_close()
			 * failure in any reasonable way.
			 * A warning is written to the server
			 * log file.
			 */
			wal_write_setlsn(wal_to_close, batch, cluster);
			log_io_close(&wal_to_close);
		}
	} else if (l->rows == 1) {
		/*
		 * Rename WAL after the first successful write
		 * to a name  without .inprogress suffix.
		 */
		if (inprogress_log_rename(l))
			log_io_close(&l);       /* error. */
	}
	assert(wal_to_close == NULL);
	*wal = l;
	return l ? 0 : -1;
}

static void
wal_opt_sync(struct log_io *wal, double sync_delay)
{
	static ev_tstamp last_sync = 0;

	if (sync_delay <= 0)
		return;

	/* Don't use ev_now() since it requires a working event loop. */
	ev_tstamp now = ev_time();
	if (now - last_sync >= sync_delay) {
		/*
		 * XXX: in case of error, we don't really know how
		 * many records were not written to disk: probably
		 * way more than the last one.
		 */
		(void) log_io_sync(wal);
		last_sync = now;
	}
}

static struct wal_write_request *
wal_fill_batch(struct log_io *wal, struct fio_batch *batch, int rows_per_wal,
	       struct wal_write_request *req, struct mh_cluster_t *cluster)
{
	int max_rows = wal->is_inprogress ? 1 : rows_per_wal - wal->rows;
	/* Post-condition of successful wal_opt_rotate(). */
	assert(max_rows > 0);
	fio_batch_start(batch, max_rows);

	struct iovec iov[XLOG_ROW_IOVMAX];
	while (req != NULL && !fio_batch_has_space(batch, nelem(iov))) {
		req->node = mh_cluster_fetch(cluster, req->packet->node_id);
		if (req->node == NULL) {
			say_syserror("can't reallocate writer->cluster");
			return NULL;
		}
		int iovcnt = xlog_encode_row(req->packet, iov, req->wal_fixheader);
		fio_batch_add(batch, iov, iovcnt);
		req = STAILQ_NEXT(req, wal_fifo_entry);
	}
	return req;
}

static struct wal_write_request *
wal_write_batch(struct log_io *wal, struct fio_batch *batch,
		struct wal_write_request *req, struct wal_write_request *end)
{
	int rows_written = fio_batch_write(batch, fileno(wal->f));
	wal->rows += rows_written;
	while (req != end && rows_written-- != 0)  {
		assert(req->node->id == req->packet->node_id);
		assert(req->node->current_lsn < req->packet->lsn);
		req->prev_lsn = req->node->current_lsn;
		req->node->current_lsn = req->packet->lsn;
		req = STAILQ_NEXT(req, wal_fifo_entry);
	}
	return req;
}

static void
wal_write_to_disk(struct recovery_state *r, struct wal_writer *writer,
		  struct wal_fifo *input, struct wal_fifo *commit,
		  struct wal_fifo *rollback)
{
	struct log_io **wal = &r->current_wal;
	struct fio_batch *batch = writer->batch;

	struct wal_write_request *req = STAILQ_FIRST(input);
	struct wal_write_request *write_end = req;

	while (req) {
		if (wal_opt_rotate(wal, batch, r, writer->cluster) != 0)
			break;
		struct wal_write_request *batch_end;
		batch_end = wal_fill_batch(*wal, batch, r->rows_per_wal, req,
					   writer->cluster);
		write_end = wal_write_batch(*wal, batch, req, batch_end);
		if (batch_end != write_end)
			break;
		wal_opt_sync(*wal, r->wal_fsync_delay);
		req = write_end;
	}
	STAILQ_SPLICE(input, write_end, wal_fifo_entry, rollback);
	STAILQ_CONCAT(commit, input);
}

/** WAL writer thread main loop.  */
static void *
wal_writer_thread(void *worker_args)
{
	struct recovery_state *r = (struct recovery_state *) worker_args;
	struct wal_writer *writer = r->writer;
	struct wal_fifo input = STAILQ_HEAD_INITIALIZER(input);
	struct wal_fifo commit = STAILQ_HEAD_INITIALIZER(commit);
	struct wal_fifo rollback = STAILQ_HEAD_INITIALIZER(rollback);

	(void) tt_pthread_mutex_lock(&writer->mutex);
	while (! writer->is_shutdown) {
		wal_writer_pop(writer, &input);
		(void) tt_pthread_mutex_unlock(&writer->mutex);

		wal_write_to_disk(r, writer, &input, &commit, &rollback);

		(void) tt_pthread_mutex_lock(&writer->mutex);
		STAILQ_CONCAT(&writer->commit, &commit);
		if (! STAILQ_EMPTY(&rollback)) {
			/*
			 * Begin rollback: create a rollback queue
			 * from all requests which were not
			 * written to disk and all requests in the
			 * input queue.
			 */
			writer->is_rollback = true;
			STAILQ_CONCAT(&rollback, &writer->input);
			STAILQ_CONCAT(&writer->input, &rollback);
		}
		ev_async_send(writer->txn_loop, &writer->write_event);
	}
	(void) tt_pthread_mutex_unlock(&writer->mutex);
	if (r->current_wal != NULL) {
		wal_write_setlsn(r->current_wal, writer->batch, writer->cluster);
		log_io_close(&r->current_wal);
	}
	return NULL;
}

/**
 * WAL writer main entry point: queue a single request
 * to be written to disk and wait until this task is completed.
 */
int
wal_write(struct recovery_state *r, struct iproto_packet *packet)
{
	struct node *node = fill_lsn(r, packet);
	if (r->wal_mode == WAL_NONE)
		return 0;

	assert(packet != NULL);
	assert(r->wal_mode != WAL_NONE);
	ERROR_INJECT_RETURN(ERRINJ_WAL_IO);

	struct wal_writer *writer = r->writer;

	struct wal_write_request *req = (struct wal_write_request *)
		region_alloc(&fiber()->gc, sizeof(struct wal_write_request));

	req->fiber = fiber();
	req->prev_lsn = -1;
	req->packet = packet;
	packet->tm = ev_now(loop());
	packet->sync = 0;

	(void) tt_pthread_mutex_lock(&writer->mutex);

	bool input_was_empty = STAILQ_EMPTY(&writer->input);
	STAILQ_INSERT_TAIL(&writer->input, req, wal_fifo_entry);

	if (input_was_empty)
		(void) tt_pthread_cond_signal(&writer->cond);

	(void) tt_pthread_mutex_unlock(&writer->mutex);

	int64_t lsn = node->current_lsn; /* save current lsn on the stack */
	fiber_yield(); /* Request was inserted. */

	/* req->res is -1 on error and prev_lsn on success */
	if (req->prev_lsn < 0)
		return -1; /* error */

	if (req->prev_lsn >= lsn) {
		/*
		 * There can be holes in
		 * confirmed_lsn, in case of disk write failure, but
		 * wal_writer never confirms LSNs out order.
		 */
		panic("LSN for %s is used twice or COMMIT order is broken: "
		      "confirmed: %lld, new: %lld", tt_uuid_str(&node->uuid),
		      (long long) req->prev_lsn, (long long) lsn);
	}

	return 0; /* success */
}

/* }}} */

/* {{{ box.snapshot() */

void
snapshot_write_row(struct log_io *l, struct iproto_packet *packet)
{
	static int rows;
	static uint64_t bytes;
	ev_tstamp elapsed;
	static ev_tstamp last = 0;
	ev_loop *loop = loop();

	packet->tm = last;
	packet->node_id = 0;
	if (iproto_request_is_dml(packet->code))
		packet->lsn = ++rows;
	packet->sync = 0; /* don't write sync to wal */

	char fixheader[XLOG_FIXHEADER_SIZE];
	struct iovec iov[XLOG_ROW_IOVMAX];
	int iovcnt = xlog_encode_row(packet, iov, fixheader);

	/* TODO: use writev here */
	for (int i = 0; i < iovcnt; i++) {
		if (fwrite(iov[i].iov_base, iov[i].iov_len, 1, l->f) != 1) {
			say_error("Can't write row (%zu bytes)",
				  iov[i].iov_len);
			panic_syserror("snapshot_write_row");
		}
		bytes += iov[i].iov_len;
	}

	if (rows % 100000 == 0)
		say_crit("%.1fM rows written", rows / 1000000.);

	region_free_after(&fiber()->gc, 128 * 1024);

	if (recovery_state->snap_io_rate_limit != UINT64_MAX) {
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
		if (bytes > recovery_state->snap_io_rate_limit)
			fdatasync(fileno(l->f));
	}
	while (bytes > recovery_state->snap_io_rate_limit) {
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
		bytes -= recovery_state->snap_io_rate_limit;
	}
}

void
snapshot_save(struct recovery_state *r)
{
	assert(r->snapshot_handler != NULL);
	struct log_io *snap;
	int64_t lsnsum = mh_cluster_current_sum(r->cluster);
	snap = log_io_open_for_write(&r->snap_dir, lsnsum, &r->node_uuid,
				     INPROGRESS);
	if (snap == NULL)
		panic_status(errno, "Failed to save snapshot: failed to open file in write mode.");
	/*
	 * While saving a snapshot, snapshot name is set to
	 * <lsn>.snap.inprogress. When done, the snapshot is
	 * renamed to <lsn>.snap.
	 */
	say_info("saving snapshot `%s'", snap->filename);

	/* Write starting SETLSN (always empty table for snapshot) */
	struct iproto_packet setlsn;
	log_encode_setlsn(&setlsn, NULL);
	snapshot_write_row(snap, &setlsn);

	r->snapshot_handler(snap);

	/* Write finishing SETLSN */
	log_encode_setlsn(&setlsn, r->cluster);
	snapshot_write_row(snap, &setlsn);

	log_io_close(&snap);

	say_info("done");
}

/* }}} */

