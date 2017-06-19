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
#include "vinyl.h"

#include "vy_mem.h"
#include "vy_run.h"
#include "vy_cache.h"
#include "vy_log.h"
#include "vy_upsert.h"
#include "vy_write_iterator.h"
#include "vy_stat.h"

#define RB_COMPACT 1
#include <small/rb.h>

#include <small/lsregion.h>
#include <coio_file.h>

#include "assoc.h"
#include "cfg.h"
#include "coio_task.h"
#include "cbus.h"
#include "histogram.h"

#include "tuple_update.h"
#include "txn.h" /* box_txn_alloc() */
#include "replication.h" /* INSTANCE_UUID */
#include "schema.h"
#include "xrow.h"
#include "xlog.h"
#include "space.h"
#include "xstream.h"
#include "info.h"
#include "request.h"
#include "tuple_hash.h" /* for bloom filter */
#include "column_mask.h"

#define HEAP_FORWARD_DECLARATION
#include "salad/heap.h"

/**
 * Yield after iterating over this many objects (e.g. ranges).
 * Yield more often in debug mode.
 */
#if defined(NDEBUG)
enum { VY_YIELD_LOOPS = 128 };
#else
enum { VY_YIELD_LOOPS = 2 };
#endif

struct tx_manager;
struct vy_scheduler;
struct vy_task;
struct vy_stat;
struct vy_squash_queue;

enum vy_status {
	VINYL_OFFLINE,
	VINYL_INITIAL_RECOVERY_LOCAL,
	VINYL_INITIAL_RECOVERY_REMOTE,
	VINYL_FINAL_RECOVERY_LOCAL,
	VINYL_FINAL_RECOVERY_REMOTE,
	VINYL_ONLINE,
};

static const int64_t MAX_LSN = INT64_MAX / 2;

/**
 * Global configuration of an entire vinyl instance (env object).
 */
struct vy_conf {
	/* path to vinyl_dir */
	char *path;
	/* memory */
	uint64_t memory_limit;
	/* read cache quota */
	uint64_t cache;
	/* quota timeout */
	double timeout;
};

struct vy_env {
	/** Recovery status */
	enum vy_status status;
	/** Configuration */
	struct vy_conf      *conf;
	/** TX manager */
	struct tx_manager   *xm;
	/** Scheduler */
	struct vy_scheduler *scheduler;
	/** Statistics */
	struct vy_stat      *stat;
	/** Upsert squash queue */
	struct vy_squash_queue *squash_queue;
	/** Tuple format for keys (SELECT) */
	struct tuple_format *key_format;
	/** Mempool for struct vy_cursor */
	struct mempool      cursor_pool;
	/** Allocator for tuples */
	struct lsregion     allocator;
	/** Memory quota */
	struct vy_quota     quota;
	/** Timer for updating quota watermark. */
	ev_timer            quota_timer;
	/** Environment for cache subsystem */
	struct vy_cache_env cache_env;
	/** Environment for run subsystem */
	struct vy_run_env run_env;
	/** Local recovery context. */
	struct vy_recovery *recovery;
	/** Local recovery vclock. */
	const struct vclock *recovery_vclock;
};

/** Mask passed to vy_gc(). */
enum {
	/** Delete incomplete runs. */
	VY_GC_INCOMPLETE	= 1 << 0,
	/** Delete dropped runs. */
	VY_GC_DROPPED		= 1 << 1,
};

static void
vy_gc(struct vy_env *env, struct vy_recovery *recovery,
      unsigned int gc_mask, int64_t gc_lsn);

struct vy_latency {
	uint64_t count;
	double total;
	double max;
};

static void
vy_latency_update(struct vy_latency *lat, double v)
{
	lat->count++;
	lat->total += v;
	if (v > lat->max)
		lat->max = v;
}

static int
path_exists(const char *path)
{
	struct stat st;
	int rc = lstat(path, &st);
	return rc == 0;
}

enum vy_stat_name {
	VY_STAT_GET,
	VY_STAT_TX,
	VY_STAT_TX_OPS,
	VY_STAT_TX_WRITE,
	VY_STAT_CURSOR,
	VY_STAT_CURSOR_OPS,
	/* How many upsert chains was squashed */
	VY_STAT_UPSERT_SQUASHED,
	/* How many upserts was applied on read */
	VY_STAT_UPSERT_APPLIED,
	VY_STAT_LAST,
};

static const char *vy_stat_strings[] = {
	"get",
	"tx",
	"tx_ops",
	"tx_write",
	"cursor",
	"cursor_ops",
	"upsert_squashed",
	"upsert_applied"
};

struct vy_stat {
	struct rmean *rmean;
	uint64_t write_count;
	/**
	 * The total count of dumped statemnts.
	 * Doesn't count compactions and splits.
	 * Used to test optimization of UPDATE and UPSERT
	 * optimization.
	 * @sa vy_can_skip_update().
	 */
	uint64_t dumped_statements;
	uint64_t tx_rlb;
	uint64_t tx_conflict;
	struct vy_latency get_latency;
	struct vy_latency tx_latency;
	struct vy_latency cursor_latency;
	/**
	 * Dump bandwidth is needed for calculating the quota watermark.
	 * The higher the bandwidth, the later we can start dumping w/o
	 * suffering from transaction throttling. So we want to be very
	 * conservative about estimating the bandwidth.
	 *
	 * To make sure we don't overestimate it, we maintain a
	 * histogram of all observed measurements and assume the
	 * bandwidth to be equal to the 10th percentile, i.e. the
	 * best result among 10% worst measurements.
	 */
	struct histogram *dump_bw;
	int64_t dump_total;
	/* iterators statistics */
	struct vy_iterator_stat txw_stat;
	struct vy_iterator_stat cache_stat;
	struct vy_iterator_stat mem_stat;
	struct vy_iterator_stat run_stat;
};

static struct vy_stat *
vy_stat_new()
{
	enum { KB = 1000, MB = 1000 * 1000 };
	static int64_t bandwidth_buckets[] = {
		100 * KB, 200 * KB, 300 * KB, 400 * KB, 500 * KB,
		  1 * MB,   2 * MB,   3 * MB,   4 * MB,   5 * MB,
		 10 * MB,  20 * MB,  30 * MB,  40 * MB,  50 * MB,
		 60 * MB,  70 * MB,  80 * MB,  90 * MB, 100 * MB,
		110 * MB, 120 * MB, 130 * MB, 140 * MB, 150 * MB,
		160 * MB, 170 * MB, 180 * MB, 190 * MB, 200 * MB,
		220 * MB, 240 * MB, 260 * MB, 280 * MB, 300 * MB,
		320 * MB, 340 * MB, 360 * MB, 380 * MB, 400 * MB,
		450 * MB, 500 * MB, 550 * MB, 600 * MB, 650 * MB,
		700 * MB, 750 * MB, 800 * MB, 850 * MB, 900 * MB,
		950 * MB, 1000 * MB,
	};

	struct vy_stat *s = calloc(1, sizeof(*s));
	if (s == NULL) {
		diag_set(OutOfMemory, sizeof(*s), "stat", "struct");
		return NULL;
	}
	s->dump_bw = histogram_new(bandwidth_buckets,
				   lengthof(bandwidth_buckets));
	if (s->dump_bw == NULL) {
		free(s);
		return NULL;
	}
	/*
	 * Until we dump anything, assume bandwidth to be 10 MB/s,
	 * which should be fine for initial guess.
	 */
	histogram_collect(s->dump_bw, 10 * MB);

	s->rmean = rmean_new(vy_stat_strings, VY_STAT_LAST);
	if (s->rmean == NULL) {
		histogram_delete(s->dump_bw);
		free(s);
		return NULL;
	}
	return s;
}

static void
vy_stat_delete(struct vy_stat *s)
{
	histogram_delete(s->dump_bw);
	rmean_delete(s->rmean);
	free(s);
}

static void
vy_stat_get(struct vy_stat *s, ev_tstamp start)
{
	ev_tstamp diff = ev_now(loop()) - start;
	rmean_collect(s->rmean, VY_STAT_GET, 1);
	vy_latency_update(&s->get_latency, diff);
}

static void
vy_stat_tx(struct vy_stat *s, ev_tstamp start,
	   int ops, int write_count, size_t write_size)
{
	ev_tstamp diff = ev_now(loop()) - start;
	rmean_collect(s->rmean, VY_STAT_TX, 1);
	rmean_collect(s->rmean, VY_STAT_TX_OPS, ops);
	rmean_collect(s->rmean, VY_STAT_TX_WRITE, write_size);
	s->write_count += write_count;
	vy_latency_update(&s->tx_latency, diff);
}

static void
vy_stat_cursor(struct vy_stat *s, ev_tstamp start, int ops)
{
	ev_tstamp diff = ev_now(loop()) - start;
	rmean_collect(s->rmean, VY_STAT_CURSOR, 1);
	rmean_collect(s->rmean, VY_STAT_CURSOR_OPS, ops);
	vy_latency_update(&s->cursor_latency, diff);
}

static void
vy_stat_dump(struct vy_stat *s, size_t written,
	     uint64_t dumped_statements)
{
	s->dump_total += written;
	s->dumped_statements += dumped_statements;
}

static int64_t
vy_stat_dump_bandwidth(struct vy_stat *s)
{
	/* See comment to vy_stat->dump_bw. */
	return histogram_percentile(s->dump_bw, 10);
}

static int64_t
vy_stat_tx_write_rate(struct vy_stat *s)
{
	return rmean_mean(s->rmean, VY_STAT_TX_WRITE);
}

struct vy_range {
	/** Unique ID of this range. */
	int64_t   id;
	/**
	 * Range lower bound. NULL if range is leftmost.
	 * Both 'begin' and 'end' statements have SELECT type with the full
	 * idexed key.
	 */
	struct tuple *begin;
	/** Range upper bound. NULL if range is rightmost. */
	struct tuple *end;
	/** The index this range belongs to. */
	struct vy_index *index;
	/** An estimate of the number of statements in this range. */
	struct vy_disk_stmt_counter count;
	/**
	 * List of run slices in this range, linked by vy_slice->in_range.
	 * The newer a slice, the closer it to the list head.
	 */
	struct rlist slices;
	/** Number of entries in the ->slices list. */
	int slice_count;
	/**
	 * The goal of compaction is to reduce read amplification.
	 * All ranges for which the LSM tree has more runs per
	 * level than run_count_per_level or run size larger than
	 * one defined by run_size_ratio of this level are candidates
	 * for compaction.
	 * Unlike other LSM implementations, Vinyl can have many
	 * sorted runs in a single level, and is able to compact
	 * runs from any number of adjacent levels. Moreover,
	 * higher levels are always taken in when compacting
	 * a lower level - i.e. L1 is always included when
	 * compacting L2, and both L1 and L2 are always included
	 * when compacting L3.
	 *
	 * This variable contains the number of runs the next
	 * compaction of this range will include.
	 *
	 * The lower the level is scheduled for compaction,
	 * the bigger it tends to be because upper levels are
	 * taken in.
	 * @sa vy_range_update_compact_priority() to see
	 * how we  decide how many runs to compact next time.
	 */
	int compact_priority;
	/** Number of times the range was compacted. */
	int n_compactions;
	/** Link in vy_index->tree. */
	rb_node(struct vy_range) tree_node;
	/** Link in vy_scheduler->compact_heap. */
	struct heap_node in_compact;
	/**
	 * Incremented whenever an in-memory index or on disk
	 * run is added to or deleted from this range. Used to
	 * invalidate iterators.
	 */
	uint32_t version;
};

typedef rb_tree(struct vy_range) vy_range_tree_t;

/**
 * A single operation made by a transaction:
 * a single read or write in a vy_index.
 */
struct txv {
	struct vy_index *index;
	/** A mem in which this statement will be stored. */
	struct vy_mem *mem;
	struct tuple *stmt;
	/** A tuple in mem's region; is set in vy_prepare */
	const struct tuple *region_stmt;
	struct vy_tx *tx;
	/** Next in the transaction log. */
	struct stailq_entry next_in_log;
	/** Member of either read or write set. */
	rb_node(struct txv) in_set;
	/** true for read tx, false for write tx */
	bool is_read;
	/** true if that is a read statement,
	 * and there was no value found for that key */
	bool is_gap;
};

typedef rb_tree(struct txv) read_set_t;

/**
 * A struct for primary and secondary Vinyl indexes.
 *
 * Vinyl primary and secondary indexes work differently:
 *
 * - the primary index is fully covering (also known as
 *   "clustered" in MS SQL circles).
 *   It stores all tuple fields of the tuple coming from
 *   INSERT/REPLACE/UPDATE/DELETE operations. This index is
 *   the only place where the full tuple is stored.
 *
 * - a secondary index only stores parts participating in the
 *   secondary key, coalesced with parts of the primary key.
 *   Duplicate parts, i.e. identical parts of the primary and
 *   secondary key are only stored once. (@sa key_def_merge
 *   function). This reduces the disk and RAM space necessary to
 *   maintain a secondary index, but adds an extra look-up in the
 *   primary key for every fetched tuple.
 *
 * When a search in a secondary index is made, we first look up
 * the secondary index tuple, containing the primary key, and then
 * use this key to find the original tuple in the primary index.

 * While the primary index has only one key_def that is
 * used for validating and comparing tuples, secondary index needs
 * two:
 *
 * - the first one is defined by the user. It contains the key
 *   parts of the secondary key, as present in the original tuple.
 *   This is user_key_def.
 *
 * - the second one is used to fetch key parts of the secondary
 *   key, *augmented* with the parts of the primary key from the
 *   original tuple and compare secondary index tuples. These
 *   parts concatenated together construe the tuple of the
 *   secondary key, i.e. the tuple stored. This is key_def.
 */
struct vy_index {
	struct vy_env *env;
	/* An merge cache for current index. Contains the hotest tuples
	 * with continuation markers.
	 */
	struct vy_cache cache;
	/**
	 * Conflict manager index. Contains all changes
	 * made by transaction before they commit. Is used
	 * to implement read committed isolation level, i.e.
	 * the changes made by a transaction are only present
	 * in this tree, and thus not seen by other transactions.
	 */
	read_set_t read_set;
	/** Active in-memory index, i.e. the one used for insertions. */
	struct vy_mem *mem;
	/**
	 * List of sealed in-memory indexes, i.e. indexes that can't be
	 * inserted into, only read from, linked by vy_mem->in_sealed.
	 * The newer an index, the closer it to the list head.
	 */
	struct rlist sealed;
	/**
	 * Generation of in-memory data stored in this index
	 * (min over mem->generation).
	 */
	int64_t generation;
	/**
	 * LSN of the last dump or -1 if the index has not
	 * been dumped yet.
	 */
	int64_t dump_lsn;
	/**
	 * Tree of all ranges of this index, linked by
	 * vy_range->tree_node, ordered by vy_range->begin.
	 */
	vy_range_tree_t *tree;
	/**
	 * List of all runs created for this index,
	 * linked by vy_run->in_index.
	 */
	struct rlist runs;
	/** Number of ranges in this index. */
	int range_count;
	/** Number of runs in all ranges. */
	int run_count;
	/** Index statistics. */
	struct vy_index_stat stat;
	/** Size of data stored on disk. */
	uint64_t size;
	/** Histogram of number of runs in range. */
	struct histogram *run_hist;
	/**
	 * Reference counter. Used to postpone index drop
	 * until all pending operations have completed.
	 */
	int refs;
	/**
	 * This flag is set if the index was dropped.
	 * It is also set on local recovery if the index
	 * will be dropped when WAL is replayed.
	 */
	bool is_dropped;
	/**
	 * A key definition for this index, used to
	 * compare tuples.
	 */
	struct key_def *key_def;
	/** Index options. */
	struct index_opts opts;
	/** ID of the parent space. */
	uint32_t space_id;
	/** Index id, visible to the user. */
	uint32_t id;
	/**
	 * A key definition that was declared by an user with
	 * space:create_index().
	 */
	struct key_def *user_key_def;
	/** A tuple format for key_def. */
	struct tuple_format *surrogate_format;
	/**
	 * A format for the tuples of type REPLACE or DELETE which
	 * are parts of an UPDATE operation.
	 */
	struct tuple_format *space_format_with_colmask;
	/**
	 * A format of the space of this index. It is need as the
	 * separate member, because there is a case, when the
	 * index is dropped, but we still need the format of the
	 * space.
	 */
	struct tuple_format *space_format;
	/**
	 * Count of indexes in the space. This member is need by
	 * the same reason, as the previous - we need to know the
	 * count of indexes was in the space, that maybe already
	 * was deleted.
	 */
	uint32_t space_index_count;
	/*
	 * Format for UPSERT statements. Note, that UPSERTs can
	 * appear only in spaces with a single primary index.
	 */
	struct tuple_format *upsert_format;
	/**
	 * Incremented for each change of the range list,
	 * to invalidate iterators.
	 */
	uint32_t version;
	/**
	 * Primary index of the same space or NULL if this index
	 * is primary. Referenced by each secondary index.
	 */
	struct vy_index *pk;
	/** Link in vy_scheduler->dump_heap. */
	struct heap_node in_dump;
	/**
	 * If pin_count > 0 the index can't be scheduled for dump.
	 * Used to make sure that the primary index is dumped last.
	 */
	int pin_count;
	/**
	 * The number of times the index was truncated.
	 *
	 * After recovery is complete, it equals space->truncate_count.
	 * On local recovery, it is loaded from the metadata log and may
	 * be greater than space->truncate_count, which indicates that
	 * the space is truncated in WAL.
	 */
	uint64_t truncate_count;
};

/** Return index name. Used for logging. */
static const char *
vy_index_name(struct vy_index *index)
{
	char *buf = tt_static_buf();
	snprintf(buf, TT_STATIC_BUF_LEN, "%u/%u", (unsigned)index->space_id,
		 (unsigned)index->id);
	return buf;
}

/** @sa implementation for details. */
extern struct vy_index *
vy_index(struct Index *index);

/**
 * Get struct vy_index by a space index with the specified
 * identifier. If the index is not found then set the
 * corresponding error in the diagnostics area.
 * @param space Vinyl space.
 * @param iid   Identifier of the index for search.
 *
 * @retval not NULL Pointer to index->db
 * @retval NULL     The index is not found.
 */
static inline struct vy_index *
vy_index_find(struct space *space, uint32_t iid)
{
	struct Index *index = index_find(space, iid);
	if (index == NULL)
		return NULL;
	return vy_index(index);
}

/**
 * Get unique struct vy_index by a space index with the specified
 * identifier. If the index is not found or found not unique then
 * set the corresponding error in the diagnostics area.
 * @param space Vinyl space.
 * @param iid   Identifier of the index for search.
 *
 * @retval not NULL Pointer to index->db
 * @retval NULL     The index is not found, or found not unique.
 */
static inline struct vy_index *
vy_index_find_unique(struct space *space, uint32_t index_id)
{
	struct vy_index *index = vy_index_find(space, index_id);
	if (index != NULL && !index->opts.is_unique) {
		diag_set(ClientError, ER_MORE_THAN_ONE_TUPLE);
		return NULL;
	}
	return index;
}

/** Transaction state. */
enum tx_state {
	/** Initial state. */
	VINYL_TX_READY,
	/**
	 * A transaction is finished and validated in the engine.
	 * It may still be rolled back if there is an error
	 * writing the WAL.
	 */
	VINYL_TX_COMMIT,
	/** A transaction is aborted by a conflict. */
	VINYL_TX_ABORT
};

struct read_set_key {
	struct tuple *stmt;
	struct vy_tx *tx;
};

typedef rb_tree(struct txv) write_set_t;

/**
 * Initialize an instance of a global read view.
 * To be used exclusively by the transaction manager.
 */
static void
vy_global_read_view_create(struct vy_read_view *rv, int64_t lsn)
{
	rlist_create(&rv->in_read_views);
	/*
	 * By default, the transaction is assumed to be
	 * read-write, and it reads the latest changes of all
	 * prepared transactions. This makes it possible to
	 * use the tuple cache in it.
	 */
	rv->vlsn = lsn;
	rv->refs = 0;
	rv->is_aborted = false;
}

struct vy_tx {
	/**
	 * In memory transaction log. Contains both reads
	 * and writes.
	 */
	struct stailq log;
	/**
	 * Writes of the transaction segregated by the changed
	 * vy_index object.
	 */
	write_set_t write_set;
	/**
	 * Version of write_set state; if the state changes (insert/remove),
	 * the version increments.
	 */
	uint32_t write_set_version;
	/**
	 * Total size of memory occupied by statements of
	 * the write set.
	 */
	size_t write_size;
	/** Transaction start time saved in vy_begin() */
	ev_tstamp start;
	/** Current state of the transaction.*/
	enum tx_state state;
	/**
	 * The read view of this transaction. When a transaction
	 * is started, it is set to the "read committed" state,
	 * or actually, "read prepared" state, in other words,
	 * all changes of all prepared transactions are visible
	 * to this transaction. Upon a conflict, the transaction's
	 * read view is changed: it begins to point to the
	 * last state of the database before the conflicting
	 * change.
	 */
	struct vy_read_view *read_view;
	/**
	 * Prepare sequence number. 
	 * Is -1 if the transaction is not prepared.
	 */
	int64_t psn;
	/*
	 * For non-autocommit transactions, the list of open
	 * cursors. When a transaction ends, all open cursors are
	 * forcibly closed.
	 */
	struct rlist cursors;
	struct tx_manager *xm;
};

static int
vy_tx_track(struct vy_tx *tx, struct vy_index *index,
	    struct tuple *key, bool is_gap);

/**
 * Merge iterator takes several iterators as sources and sorts
 * output from them by the given order and LSN DESC. It has no filter,
 * it just sorts output from its sources.
 *
 * All statements from all sources can be traversed via
 * next_key()/next_lsn() like in a simple iterator (run, mem etc).
 * next_key() switches to the youngest statement of
 * the next key (according to the order), and next_lsn()
 * switches to an older statement of the same key.
 *
 * There are several merge optimizations, which expect that:
 *
 * 1) All sources are sorted by age, i.e. the most fresh
 * sources are added first.
 * 2) Mutable sources are added before read-blocking sources.
 *
 * The iterator can merge the write set of the current
 * transaction, that does not belong to any range but to entire
 * index, and mems and runs of some range. For this purpose the
 * iterator has a special flag (range_ended) that signals to the
 * read iterator that it must switch to the next range.
 */
struct vy_merge_iterator {
	/** Array of sources */
	struct vy_merge_src *src;
	/** Number of elements in the src array */
	uint32_t src_count;
	/** Number of elements allocated in the src array */
	uint32_t src_capacity;
	/** Current source offset that merge iterator is positioned on */
	uint32_t curr_src;
	/** Offset of the first source with is_mutable == true */
	uint32_t mutable_start;
	/** Next offset after the last source with is_mutable == true */
	uint32_t mutable_end;
	/** The offset starting with which the sources were skipped */
	uint32_t skipped_start;
	/** Index key definition. */
	const struct key_def *key_def;
	/** Format to allocate REPLACE and DELETE tuples. */
	struct tuple_format *format;
	/** Format to allocate UPSERT tuples. */
	struct tuple_format *upsert_format;
	/** Set if this iterator is for a primary index. */
	bool is_primary;

	/* {{{ Range version checking */
	/* pointer to index->version */
	const uint32_t *p_index_version;
	/* copy of index->version to track range tree changes */
	uint32_t index_version;
	/* pointer to range->version */
	const uint32_t *p_range_version;
	/* copy of curr_range->version to track mem/run lists changes */
	uint32_t range_version;
	/* Range version checking }}} */

	const struct tuple *key;
	/** Iteration type. */
	enum iterator_type iterator_type;
	/** Current stmt that merge iterator is positioned on */
	struct tuple *curr_stmt;
	/**
	 * All sources with this front_id are on the same key of
	 * current iteration (optimization)
	 */
	uint32_t front_id;
	/**
	 * For some optimization the flag is set for unique
	 * index and full key and EQ order - that means that only
	 * one value is to be emitted by the iterator.
	 */
	bool is_one_value;
	/**
	 * If index is unique and full key is given we can
	 * optimize first search in order to avoid unnecessary
	 * reading from disk.  That flag is set to true during
	 * initialization if index is unique and  full key is
	 * given. After first _get or _next_key call is set to
	 * false
	 */
	bool unique_optimization;
	/**
	 * This flag is set to false during initialization and
	 * means that we must do lazy search for first _get or
	 * _next call. After that is set to false
	 */
	bool search_started;
	/**
	 * If all sources marked with belong_range = true comes to
	 * the end of data this flag is automatically set to true;
	 * is false otherwise.  For read iterator range_ended = true
	 * means that it must switch to next range
	 */
	bool range_ended;
};

struct vy_range_iterator {
	struct vy_index *index;
	enum iterator_type iterator_type;
	const struct tuple *key;
	struct vy_range *curr_range;
};

/**
 * Complex read iterator over vinyl index and write_set of current tx
 * Iterates over ranges, creates merge iterator for every range and outputs
 * the result.
 * Can also wor without transaction, just set tx = NULL in _open
 * Applyes upserts and skips deletes, so only one replace stmt for every key
 * is output
 */
struct vy_read_iterator {
	/* index to iterate over */
	struct vy_index *index;
	/* transaction to iterate over */
	struct vy_tx *tx;

	/* search options */
	enum iterator_type iterator_type;
	const struct tuple *key;
	const struct vy_read_view **read_view;

	/* iterator over ranges */
	struct vy_range_iterator range_iterator;
	/* current range */
	struct vy_range *curr_range;
	/* merge iterator over current range */
	struct vy_merge_iterator merge_iterator;

	struct tuple *curr_stmt;
	/* is lazy search started */
	bool search_started;
};

/**
 * Open the read iterator.
 * @param itr           Read iterator.
 * @param index         Vinyl index to iterate.
 * @param tx            Current transaction, if exists.
 * @param iterator_type Type of the iterator that determines order
 *                      of the iteration.
 * @param key           Key for the iteration.
 * @param vlsn          Maximal visible LSN of transactions.
 */
static void
vy_read_iterator_open(struct vy_read_iterator *itr,
		      struct vy_index *index, struct vy_tx *tx,
		      enum iterator_type iterator_type,
		      const struct tuple *key, const struct vy_read_view **rv);

/**
 * Get the next statement with another key, or start the iterator,
 * if it wasn't started.
 * @param itr         Read iterator.
 * @param[out] result Found statement is stored here.
 *
 * @retval  0 Success.
 * @retval -1 Read error.
 */
static NODISCARD int
vy_read_iterator_next(struct vy_read_iterator *itr, struct tuple **result);

/**
 * Close the iterator and free resources.
 */
static void
vy_read_iterator_close(struct vy_read_iterator *itr);

/** Cursor. */
struct vy_cursor {
	/**
	 * A built-in transaction created when a cursor is open
	 * in autocommit mode.
	 */
	struct vy_tx tx_autocommit;
	struct vy_index *index;
	struct vy_env *env;
	struct tuple *key;
	/**
	 * Points either to tx_autocommit for autocommit mode or
	 * to a multi-statement transaction active when the cursor
	 * was created.
	 */
	struct vy_tx *tx;
	enum iterator_type iterator_type;
	/** The number of vy_cursor_next() invocations. */
	int n_reads;
	/** Cursor creation time, used for statistics. */
	ev_tstamp start;
	/**
	 * All open cursors are registered in a transaction
	 * they belong to. When the transaction ends, the cursor
	 * is closed.
	 */
	struct rlist next_in_tx;
	/** Iterator over index */
	struct vy_read_iterator iterator;
	/** Set to true, if need to check statements to match the cursor key. */
	bool need_check_eq;
};

static int
read_set_cmp(struct txv *a, struct txv *b);

static int
read_set_key_cmp(struct read_set_key *a, struct txv *b);

rb_gen_ext_key(MAYBE_UNUSED static inline, read_set_, read_set_t, struct txv,
	       in_set, read_set_cmp, struct read_set_key *, read_set_key_cmp);

static struct txv *
read_set_search_key(read_set_t *rbtree, struct tuple *stmt, struct vy_tx *tx)
{
	struct read_set_key key;
	key.stmt = stmt;
	key.tx = tx;
	return read_set_search(rbtree, &key);
}

static int
read_set_cmp(struct txv *a, struct txv *b)
{
	assert(a->index == b->index);
	int rc = vy_stmt_compare(a->stmt, b->stmt, a->index->key_def);
	/**
	 * While in svindex older value are "bigger" than newer
	 * ones, i.e. the newest value comes first, in
	 * transactional index (read_set), we want to look
	 * at data in chronological order.
	 * @sa vy_mem_tree_cmp
	 */
	if (rc == 0)
		rc = a->tx < b->tx ? -1 : a->tx > b->tx;
	return rc;
}

static int
read_set_key_cmp(struct read_set_key *a, struct txv *b)
{
	int rc = vy_stmt_compare(a->stmt, b->stmt, b->index->key_def);
	if (rc == 0)
		return a->tx < b->tx ? -1 : a->tx > b->tx;
	return rc;
}

struct tx_manager {
	/** The number of active transactions. */
	uint32_t tx_count;
	/**
	 * The last committed log sequence number known to
	 * vinyl. Updated in vy_commit().
	 */
	int64_t lsn;
	/**
	 * The last prepared (but not committed) transaction,
	 * or NULL if there are no prepared transactions.
	 */
	struct vy_tx *last_prepared_tx;
	/**
	 * A global transaction prepare counter: a transaction
	 * is assigned an id at the time of vy_prepare(). Is used
	 * to order statements of prepared but not yet committed
	 * transactions in vy_mem.
	 */
	int64_t psn;
	/**
	 * The list of TXs with a read view in order of vlsn.
	 *
	 */
	struct rlist read_views;
	/**
	 * Global read view - all prepared transactions are
	 * visible in this view. The global read view
	 * LSN is always INT64_MAX and it never changes.
	 */
	const struct vy_read_view global_read_view;
	/**
	 * It is possible to create a cursor without an active
	 * transaction, e.g. a write iterator;
	 * this pointer represents a skeleton
	 * transaction to use in such places.
	 */
	const struct vy_read_view *p_global_read_view;
	/**
	 * Committed read view - all committed transactions are
	 * visible in this view. The global read view
	 * LSN is always (MAX_LSN - 1)  and it never changes.
	 */
	const struct vy_read_view committed_read_view;
	/**
	 * It is possible to create a cursor without an active
	 * transaction, e.g. when squashing upserts;
	 * this pointer represents a skeleton
	 * transaction to use in such places.
	 */
	const struct vy_read_view *p_committed_read_view;
	struct vy_env *env;
	struct mempool tx_mempool;
	struct mempool txv_mempool;
	struct mempool read_view_mempool;
};

static int
write_set_cmp(struct txv *a, struct txv *b)
{
	/* Order by index first, by key in the index second. */
	int rc = a->index < b->index ? -1 : a->index > b->index;
	if (rc == 0)
		return vy_stmt_compare(a->stmt, b->stmt, a->index->key_def);
	return rc;
}

struct write_set_key {
	struct vy_index *index;
	const struct tuple *stmt;
};

static int
write_set_key_cmp(struct write_set_key *a, struct txv *b)
{
	/* Order by index first, by key in the index second. */
	int rc = a->index < b->index ? -1 : a->index > b->index;
	if (rc == 0)
		return vy_stmt_compare(a->stmt, b->stmt, a->index->key_def);
	return rc;
}

rb_gen_ext_key(MAYBE_UNUSED static inline, write_set_, write_set_t, struct txv,
		in_set, write_set_cmp, struct write_set_key *,
		write_set_key_cmp);

static struct txv *
write_set_search_key(write_set_t *tree, struct vy_index *index,
		     const struct tuple *data)
{
	struct write_set_key key = { .index = index, .stmt = data};
	return write_set_search(tree, &key);
}

static struct tx_manager *
tx_manager_new(struct vy_env *env)
{
	struct tx_manager *m = malloc(sizeof(*m));
	if (m == NULL) {
		diag_set(OutOfMemory, sizeof(*m), "tx_manager", "struct");
		return NULL;
	}
	m->tx_count = 0;
	m->lsn = 0;
	m->last_prepared_tx = NULL;
	m->psn = 0;
	m->env = env;
	rlist_create(&m->read_views);
	vy_global_read_view_create((struct vy_read_view *) &m->global_read_view,
				   INT64_MAX);
	m->p_global_read_view = &m->global_read_view;
	vy_global_read_view_create((struct vy_read_view *) &m->committed_read_view,
				   MAX_LSN - 1);
	m->p_committed_read_view = &m->committed_read_view;
	mempool_create(&m->tx_mempool, cord_slab_cache(), sizeof(struct vy_tx));
	mempool_create(&m->txv_mempool, cord_slab_cache(), sizeof(struct txv));
	mempool_create(&m->read_view_mempool, cord_slab_cache(),
		       sizeof(struct vy_read_view));
	return m;
}

/** Create or reuse an instance of a read view. */
struct vy_read_view *
tx_manager_read_view(struct tx_manager *xm)
{
	struct vy_read_view *rv;
	/*
	 * Check if the last read view can be reused. Reference
	 * and return it if it's the case.
	 */
	if (! rlist_empty(&xm->read_views)) {
		rv = rlist_last_entry(&xm->read_views, struct vy_read_view,
				      in_read_views);
		/** Reuse an existing read view */
		if ((xm->last_prepared_tx == NULL && rv->vlsn == xm->lsn) ||
		    (xm->last_prepared_tx != NULL &&
		     rv->vlsn == MAX_LSN + xm->last_prepared_tx->psn)) {

			rv->refs++;
			return  rv;
		}
	}
	rv = mempool_alloc(&xm->read_view_mempool);
	if (rv == NULL) {
		diag_set(OutOfMemory, sizeof(*rv),
			 "mempool", "read view");
		return NULL;
	}
	rv->is_aborted = false;
	if (xm->last_prepared_tx != NULL) {
		rv->vlsn = MAX_LSN + xm->last_prepared_tx->psn;
		xm->last_prepared_tx->read_view = rv;
		rv->refs = 2;
	} else {
		rv->vlsn = xm->lsn;
		rv->refs = 1;
	}
	/*
	 * Add to the tail of the list, so that tx_manager_vlsn()
	 * works correctly.
	 */
	rlist_add_tail_entry(&xm->read_views, rv,
			     in_read_views);
	return rv;
}

/** Dereference and possibly destroy a read view. */
void
tx_manager_destroy_read_view(struct tx_manager *xm,
			     const struct vy_read_view *read_view)
{
	struct vy_read_view *rv = (struct vy_read_view *) read_view;
	if (rv == xm->p_global_read_view)
		return;
	assert(rv->refs);
	if (--rv->refs == 0) {
		rlist_del_entry(rv, in_read_views);
		mempool_free(&xm->read_view_mempool, rv);
	}
}

static int
tx_manager_delete(struct tx_manager *m)
{
	mempool_destroy(&m->read_view_mempool);
	mempool_destroy(&m->txv_mempool);
	mempool_destroy(&m->tx_mempool);
	free(m);
	return 0;
}

/*
 * Determine a lowest possible vlsn - the level below which the
 * history could be compacted.
 * If there are active read views, it is the first's vlsn. If there is
 * no active read view, a read view could be created at any moment
 * with vlsn = m->lsn, so m->lsn must be chosen.
 */
static int64_t
tx_manager_vlsn(struct tx_manager *xm)
{
	if (rlist_empty(&xm->read_views))
		return xm->lsn;
	struct vy_read_view *oldest = rlist_first_entry(&xm->read_views,
							struct vy_read_view,
							in_read_views);
	return oldest->vlsn;
}

static void
vy_index_add_run(struct vy_index *index, struct vy_run *run)
{
	assert(rlist_empty(&run->in_index));
	rlist_add_entry(&index->runs, run, in_index);
	index->run_count++;
	vy_disk_stmt_counter_add(&index->stat.disk.count, &run->count);
}

static void
vy_index_remove_run(struct vy_index *index, struct vy_run *run)
{
	assert(index->run_count > 0);
	assert(!rlist_empty(&run->in_index));
	rlist_del_entry(run, in_index);
	index->run_count--;
	vy_disk_stmt_counter_sub(&index->stat.disk.count, &run->count);
}

static void
vy_index_acct_range(struct vy_index *index, struct vy_range *range)
{
	histogram_collect(index->run_hist, range->slice_count);
}

static void
vy_index_unacct_range(struct vy_index *index, struct vy_range *range)
{
	histogram_discard(index->run_hist, range->slice_count);
}

/** An snprint-style function to print a range's boundaries. */
static int
vy_range_snprint(char *buf, int size, const struct vy_range *range)
{
	int total = 0;
	SNPRINT(total, snprintf, buf, size, "(");
	if (range->begin != NULL)
		SNPRINT(total, vy_key_snprint, buf, size,
			tuple_data(range->begin));
	else
		SNPRINT(total, snprintf, buf, size, "-inf");
	SNPRINT(total, snprintf, buf, size, "..");
	if (range->end != NULL)
		SNPRINT(total, vy_key_snprint, buf, size,
			tuple_data(range->end));
	else
		SNPRINT(total, snprintf, buf, size, "inf");
	SNPRINT(total, snprintf, buf, size, ")");
	return total;
}

/**
 * Helper function returning a human readable representation
 * of a range's boundaries.
 */
static const char *
vy_range_str(struct vy_range *range)
{
	char *buf = tt_static_buf();
	vy_range_snprint(buf, TT_STATIC_BUF_LEN, range);
	return buf;
}

/** Add a run slice to the head of a range's list. */
static void
vy_range_add_slice(struct vy_range *range, struct vy_slice *slice)
{
	rlist_add_entry(&range->slices, slice, in_range);
	range->slice_count++;
	vy_disk_stmt_counter_add(&range->count, &slice->count);
}

/** Add a run slice to a range's list before @next_slice. */
static void
vy_range_add_slice_before(struct vy_range *range, struct vy_slice *slice,
			  struct vy_slice *next_slice)
{
	rlist_add_tail(&next_slice->in_range, &slice->in_range);
	range->slice_count++;
	vy_disk_stmt_counter_add(&range->count, &slice->count);
}

/** Remove a run slice from a range's list. */
static void
vy_range_remove_slice(struct vy_range *range, struct vy_slice *slice)
{
	assert(range->slice_count > 0);
	assert(!rlist_empty(&range->slices));
	rlist_del_entry(slice, in_range);
	range->slice_count--;
	vy_disk_stmt_counter_sub(&range->count, &slice->count);
}

/**
 * Allocate a new run for an index and write the information
 * about it to the metadata log so that we could still find
 * and delete it in case a write error occured. This function
 * is called from dump/compaction task constructor.
 */
static struct vy_run *
vy_run_prepare(struct vy_index *index)
{
	struct vy_run *run = vy_run_new(vy_log_next_id());
	if (run == NULL)
		return NULL;
	vy_log_tx_begin();
	vy_log_prepare_run(index->opts.lsn, run->id);
	if (vy_log_tx_commit() < 0) {
		vy_run_unref(run);
		return NULL;
	}
	return run;
}

/**
 * Free an incomplete run and write a record to the metadata
 * log indicating that the run is not needed any more.
 * This function is called on dump/compaction task abort.
 */
static void
vy_run_discard(struct vy_run *run)
{
	int64_t run_id = run->id;

	vy_run_unref(run);

	ERROR_INJECT(ERRINJ_VY_RUN_DISCARD,
		     {say_error("error injection: run %lld not discarded",
				(long long)run_id); return;});

	vy_log_tx_begin();
	/*
	 * The run hasn't been used and can be deleted right away
	 * so set gc_lsn to minimal possible (0).
	 */
	vy_log_drop_run(run_id, 0);
	if (vy_log_tx_commit() < 0) {
		/*
		 * Failure to log deletion of an incomplete
		 * run means that we won't retry to delete
		 * its files on log rotation. We will do that
		 * after restart though, so just warn and
		 * carry on.
		 */
		struct error *e = diag_last_error(diag_get());
		say_warn("failed to log run %lld deletion: %s",
			 (long long)run_id, e->errmsg);
	}
}

/** Return true if a task was scheduled for a given range. */
static bool
vy_range_is_scheduled(struct vy_range *range)
{
	return range->in_compact.pos == UINT32_MAX;
}

#define HEAP_NAME vy_dump_heap

static bool
vy_dump_heap_less(struct heap_node *a, struct heap_node *b)
{
	struct vy_index *left = container_of(a, struct vy_index, in_dump);
	struct vy_index *right = container_of(b, struct vy_index, in_dump);

	/* Older indexes are dumped first. */
	if (left->generation != right->generation)
		return left->generation < right->generation;
	/*
	 * If a space has more than one index, appending a statement
	 * to it requires reading the primary index to get the old
	 * tuple and delete it from secondary indexes. This means that
	 * on local recovery from WAL, the primary index must not be
	 * ahead of secondary indexes of the same space, i.e. it must
	 * be dumped last.
	 */
	return left->id > right->id;
}

#define HEAP_LESS(h, l, r) vy_dump_heap_less(l, r)

#include "salad/heap.h"

#undef HEAP_LESS
#undef HEAP_NAME

#define HEAP_NAME vy_compact_heap

static bool
vy_compact_heap_less(struct heap_node *a, struct heap_node *b)
{
	struct vy_range *left = container_of(a, struct vy_range, in_compact);
	struct vy_range *right = container_of(b, struct vy_range, in_compact);
	/*
	 * Prefer ranges whose read amplification will be reduced
	 * most as a result of compaction.
	 */
	return left->compact_priority > right->compact_priority;
}

#define HEAP_LESS(h, l, r) vy_compact_heap_less(l, r)

#include "salad/heap.h"

struct vy_scheduler {
	pthread_mutex_t        mutex;
	struct vy_env    *env;
	heap_t dump_heap;
	heap_t compact_heap;

	struct cord *worker_pool;
	struct fiber *scheduler;
	struct ev_loop *loop;
	/** Total number of worker threads. */
	int worker_pool_size;
	/** Number worker threads that are currently idle. */
	int workers_available;
	bool is_worker_pool_running;

	/**
	 * There is a pending task for workers in the pool,
	 * or we want to shutdown workers.
	 */
	pthread_cond_t worker_cond;
	/**
	 * There is no pending tasks for workers, so scheduler
	 * needs to create one, or we want to shutdown the
	 * scheduler. Scheduler is a fiber in TX, so ev_async + ipc_channel
	 * are used here instead of pthread_cond_t.
	 */
	struct ev_async scheduler_async;
	struct ipc_cond scheduler_cond;
	/** Used for throttling tx when quota is full. */
	struct ipc_cond quota_cond;
	/**
	 * A queue with all vy_task objects created by the
	 * scheduler and not yet taken by a worker.
	 */
	struct stailq input_queue;
	/**
	 * A queue of processed vy_tasks objects.
	 */
	struct stailq output_queue;
	/**
	 * A memory pool for vy_tasks.
	 */
	struct mempool task_pool;

	/** Last error seen by the scheduler. */
	struct diag diag;
	/**
	 * Schedule timeout. Grows exponentially with each successive
	 * failure. Reset on successful task completion.
	 */
	ev_tstamp timeout;
	/** Set if the scheduler is throttled due to errors. */
	bool is_throttled;
	/** Set if checkpoint is in progress. */
	bool checkpoint_in_progress;
	/** Last checkpoint vclock. */
	struct vclock last_checkpoint;
	/**
	 * Current generation of in-memory data.
	 *
	 * New in-memory trees inherit the current generation, while
	 * the scheduler dumps all in-memory trees whose generation
	 * is less. The generation is increased either on checkpoint
	 * or on exceeding the memory quota to force dumping all old
	 * in-memory trees.
	 */
	int64_t generation;
	/**
	 * Target generation for checkpoint. The scheduler will force
	 * dumping of all in-memory trees whose generation is less.
	 */
	int64_t checkpoint_generation;
	/** Time of dump start. */
	ev_tstamp dump_start;
	/**
	 * List of all in-memory trees, scheduled for dump.
	 * Older trees are closer to the tail of the list.
	 */
	struct rlist dump_fifo;
	/**
	 * Signaled on dump completion, i.e. as soon as all in-memory
	 * trees whose generation is less than the current generation
	 * have been dumped. Also signaled on any scheduler failure.
	 */
	struct ipc_cond dump_cond;
};

static void
vy_scheduler_add_index(struct vy_scheduler *, struct vy_index *);
static void
vy_scheduler_update_index(struct vy_scheduler *, struct vy_index *);
static void
vy_scheduler_remove_index(struct vy_scheduler *, struct vy_index *);
static void
vy_scheduler_pin_index(struct vy_scheduler *, struct vy_index *);
static void
vy_scheduler_unpin_index(struct vy_scheduler *, struct vy_index *);
static void
vy_scheduler_add_range(struct vy_scheduler *, struct vy_range *);
static void
vy_scheduler_update_range(struct vy_scheduler *, struct vy_range *);
static void
vy_scheduler_remove_range(struct vy_scheduler *, struct vy_range *);
static void
vy_scheduler_add_mem(struct vy_scheduler *scheduler, struct vy_mem *mem);
static void
vy_scheduler_remove_mem(struct vy_scheduler *scheduler, struct vy_mem *mem);
static bool
vy_scheduler_needs_dump(struct vy_scheduler *scheduler);

static int
vy_range_tree_cmp(struct vy_range *a, struct vy_range *b);

static int
vy_range_tree_key_cmp(const struct tuple *a, struct vy_range *b);

rb_gen_ext_key(MAYBE_UNUSED static inline, vy_range_tree_, vy_range_tree_t,
	       struct vy_range, tree_node, vy_range_tree_cmp,
	       const struct tuple *, vy_range_tree_key_cmp);

static int
vy_range_tree_cmp(struct vy_range *range_a, struct vy_range *range_b)
{
	if (range_a == range_b)
		return 0;

	/* Any key > -inf. */
	if (range_a->begin == NULL)
		return -1;
	if (range_b->begin == NULL)
		return 1;

	assert(range_a->index == range_b->index);
	return vy_key_compare(range_a->begin, range_b->begin,
			      range_a->index->key_def);
}

static int
vy_range_tree_key_cmp(const struct tuple *stmt, struct vy_range *range)
{
	/* Any key > -inf. */
	if (range->begin == NULL)
		return 1;
	return vy_stmt_compare_with_key(stmt, range->begin,
					range->index->key_def);
}

static void
vy_range_iterator_open(struct vy_range_iterator *itr, struct vy_index *index,
		       enum iterator_type iterator_type,
		       const struct tuple *key)
{
	itr->index = index;
	itr->iterator_type = iterator_type;
	itr->key = key;
	itr->curr_range = NULL;
}

/*
 * Find the first range in which a given key should be looked up.
 */
static struct vy_range *
vy_range_tree_find_by_key(vy_range_tree_t *tree,
			  enum iterator_type iterator_type,
			  const struct tuple *key,
			  const struct key_def *key_def)
{
	uint32_t key_field_count = tuple_field_count(key);
	if (key_field_count == 0) {
		switch (iterator_type) {
		case ITER_LT:
		case ITER_LE:
			return vy_range_tree_last(tree);
		case ITER_GT:
		case ITER_GE:
		case ITER_EQ:
			return vy_range_tree_first(tree);
		default:
			unreachable();
			return NULL;
		}
	}
	/* route */
	struct vy_range *range;
	if (iterator_type == ITER_GE || iterator_type == ITER_GT ||
	    iterator_type == ITER_EQ) {
		/**
		 * Case 1. part_count == 1, looking for [10]. ranges:
		 * {1, 3, 5} {7, 8, 9} {10, 15 20} {22, 32, 42}
		 *                      ^looking for this
		 * Case 2. part_count == 1, looking for [10]. ranges:
		 * {1, 2, 4} {5, 6, 7, 8} {50, 100, 200}
		 *            ^looking for this
		 * Case 3. part_count == 2, looking for [10]. ranges:
		 * {[1, 2], [2, 3]} {[9, 1], [10, 1], [10 2], [11 3]} {[12,..}
		 *                   ^looking for this
		 * Case 4. part_count == 2, looking for [10]. ranges:
		 * {[1, 2], [10, 1]} {[10, 2] [10 3] [11 3]} {[12, 1]..}
		 *  ^looking for this
		 * Case 5. part_count does not matter, looking for [10].
		 * ranges:
		 * {100, 200}, {300, 400}
		 * ^looking for this
		 */
		/**
		 * vy_range_tree_psearch finds least range with begin == key
		 * or previous if equal was not found
		 */
		range = vy_range_tree_psearch(tree, key);
		/* switch to previous for case (4) */
		if (range != NULL && range->begin != NULL &&
		    key_field_count < key_def->part_count &&
		    vy_stmt_compare_with_key(key, range->begin, key_def) == 0)
			range = vy_range_tree_prev(tree, range);
		/* for case 5 or subcase of case 4 */
		if (range == NULL)
			range = vy_range_tree_first(tree);
	} else {
		assert(iterator_type == ITER_LT || iterator_type == ITER_LE);
		/**
		 * Case 1. part_count == 1, looking for [10]. ranges:
		 * {1, 3, 5} {7, 8, 9} {10, 15 20} {22, 32, 42}
		 *                      ^looking for this
		 * Case 2. part_count == 1, looking for [10]. ranges:
		 * {1, 2, 4} {5, 6, 7, 8} {50, 100, 200}
		 *            ^looking for this
		 * Case 3. part_count == 2, looking for [10]. ranges:
		 * {[1, 2], [2, 3]} {[9, 1], [10, 1], [10 2], [11 3]} {[12,..}
		 *                   ^looking for this
		 * Case 4. part_count == 2, looking for [10]. ranges:
		 * {[1, 2], [10, 1]} {[10, 2] [10 3] [11 3]} {[12, 1]..}
		 *                    ^looking for this
		 * Case 5. part_count does not matter, looking for [10].
		 * ranges:
		 * {1, 2}, {3, 4, ..}
		 *          ^looking for this
		 */
		/**
		 * vy_range_tree_nsearch finds most range with begin == key
		 * or next if equal was not found
		 */
		range = vy_range_tree_nsearch(tree, key);
		if (range != NULL) {
			/* fix curr_range for cases 2 and 3 */
			if (range->begin != NULL &&
			    vy_stmt_compare_with_key(key, range->begin,
						     key_def) != 0) {
				struct vy_range *prev;
				prev = vy_range_tree_prev(tree,
							  range);
				if (prev != NULL)
					range = prev;
			}
		} else {
			/* Case 5 */
			range = vy_range_tree_last(tree);
		}
	}
	/* Range tree must span all possible keys. */
	assert(range != NULL);
	return range;
}

/**
 * Iterate to the next range. The next range is returned in @result.
 */
static void
vy_range_iterator_next(struct vy_range_iterator *itr, struct vy_range **result)
{
	struct vy_range *curr = itr->curr_range;
	struct vy_range *next;
	struct vy_index *index = itr->index;
	const struct key_def *key_def = index->key_def;

	if (curr == NULL) {
		/* First iteration */
		if (unlikely(index->range_count == 1))
			next = vy_range_tree_first(index->tree);
		else
			next = vy_range_tree_find_by_key(index->tree,
							 itr->iterator_type,
							 itr->key, key_def);
		goto out;
	}
	switch (itr->iterator_type) {
	case ITER_LT:
	case ITER_LE:
		next = vy_range_tree_prev(index->tree, curr);
		break;
	case ITER_GT:
	case ITER_GE:
		next = vy_range_tree_next(index->tree, curr);
		break;
	case ITER_EQ:
		if (curr->end != NULL &&
		    vy_stmt_compare_with_key(itr->key, curr->end,
					     key_def) >= 0) {
			/* A partial key can be found in more than one range. */
			next = vy_range_tree_next(index->tree, curr);
		} else {
			next = NULL;
		}
		break;
	default:
		unreachable();
	}
out:
	*result = itr->curr_range = next;
}

/**
 * Position iterator @itr to the range that contains @last_stmt and
 * return the current range in @result. If @last_stmt is NULL, restart
 * the iterator.
 */
static void
vy_range_iterator_restore(struct vy_range_iterator *itr,
			  const struct tuple *last_stmt,
			  struct vy_range **result)
{
	struct vy_index *index = itr->index;
	struct vy_range *curr = vy_range_tree_find_by_key(index->tree,
				itr->iterator_type,
				last_stmt != NULL ? last_stmt : itr->key,
				index->key_def);
	*result = itr->curr_range = curr;
}

static void
vy_index_add_range(struct vy_index *index, struct vy_range *range)
{
	vy_range_tree_insert(index->tree, range);
	index->range_count++;
}

static void
vy_index_remove_range(struct vy_index *index, struct vy_range *range)
{
	vy_range_tree_remove(index->tree, range);
	index->range_count--;
}

/**
 * Allocate and initialize a range (either a new one or for
 * restore from disk). @begin and @end specify the range's
 * boundaries. If @id is >= 0, the range's ID is set to
 * the given value, otherwise a new unique ID is allocated
 * for the range.
 */
static struct vy_range *
vy_range_new(struct vy_index *index, int64_t id,
	     struct tuple *begin, struct tuple *end)
{
	struct vy_range *range = (struct vy_range*) calloc(1, sizeof(*range));
	if (range == NULL) {
		diag_set(OutOfMemory, sizeof(struct vy_range), "malloc",
			 "struct vy_range");
		return NULL;
	}
	/* Allocate a new id unless specified. */
	range->id = (id >= 0 ? id : vy_log_next_id());
	if (begin != NULL) {
		tuple_ref(begin);
		range->begin = begin;
	}
	if (end != NULL) {
		tuple_ref(end);
		range->end = end;
	}
	rlist_create(&range->slices);
	range->index = index;
	range->in_compact.pos = UINT32_MAX;
	return range;
}

/**
 * Allocate a new active in-memory index for an index while moving
 * the old one to the sealed list. Used by the dump task in order
 * not to bother about synchronization with concurrent insertions
 * while an index is being dumped.
 */
static int
vy_index_rotate_mem(struct vy_index *index)
{
	struct lsregion *allocator = &index->env->allocator;
	struct vy_scheduler *scheduler = index->env->scheduler;
	struct vy_mem *mem;

	assert(index->mem != NULL);
	mem = vy_mem_new(allocator, scheduler->generation, index->key_def,
			 index->space_format, index->space_format_with_colmask,
			 index->upsert_format, schema_version);
	if (mem == NULL)
		return -1;

	rlist_add_entry(&index->sealed, index->mem, in_sealed);
	index->mem = mem;
	index->version++;

	vy_scheduler_add_mem(scheduler, mem);
	return 0;
}

static void
vy_range_delete(struct vy_range *range)
{
	/* The range has been deleted from the scheduler queues. */
	assert(range->in_compact.pos == UINT32_MAX);

	if (range->begin != NULL)
		tuple_unref(range->begin);
	if (range->end != NULL)
		tuple_unref(range->end);

	/* Delete all run slices. */
	while (!rlist_empty(&range->slices)) {
		struct vy_slice *slice = rlist_shift_entry(&range->slices,
						struct vy_slice, in_range);
		vy_slice_delete(slice);
	}

	TRASH(range);
	free(range);
}


/**
 * Return true and set split_key accordingly if the range needs to be
 * split in two.
 *
 * - We should never split a range until it was merged at least once
 *   (actually, it should be a function of run_count_per_level/number
 *   of runs used for the merge: with low run_count_per_level it's more
 *   than once, with high run_count_per_level it's once).
 * - We should use the last run size as the size of the range.
 * - We should split around the last run middle key.
 * - We should only split if the last run size is greater than
 *   4/3 * range_size.
 */
static bool
vy_range_needs_split(struct vy_range *range, const char **p_split_key)
{
	struct vy_index *index = range->index;
	struct vy_slice *slice;

	/* The range hasn't been merged yet - too early to split it. */
	if (range->n_compactions < 1)
		return false;

	/* Find the oldest run. */
	assert(!rlist_empty(&range->slices));
	slice = rlist_last_entry(&range->slices, struct vy_slice, in_range);

	/* The range is too small to be split. */
	if (slice->count.bytes_compressed < index->opts.range_size * 4 / 3)
		return false;

	/* Find the median key in the oldest run (approximately). */
	struct vy_page_info *mid_page;
	mid_page = vy_run_page_info(slice->run, slice->first_page_no +
				    (slice->last_page_no -
				     slice->first_page_no) / 2);

	struct vy_page_info *first_page = vy_run_page_info(slice->run,
						slice->first_page_no);

	/* No point in splitting if a new range is going to be empty. */
	if (key_compare(first_page->min_key, mid_page->min_key,
			index->key_def) == 0)
		return false;
	/*
	 * In extreme cases the median key can be < the beginning
	 * of the slice, e.g.
	 *
	 * RUN:
	 * ... |---- page N ----|-- page N + 1 --|-- page N + 2 --
	 *     | min_key = [10] | min_key = [50] | min_key = [100]
	 *
	 * SLICE:
	 * begin = [30], end = [70]
	 * first_page_no = N, last_page_no = N + 1
	 *
	 * which makes mid_page_no = N and mid_page->min_key = [10].
	 *
	 * In such cases there's no point in splitting the range.
	 */
	if (slice->begin != NULL && key_compare(mid_page->min_key,
			tuple_data(slice->begin), index->key_def) <= 0)
		return false;
	/*
	 * The median key can't be >= the end of the slice as we
	 * take the min key of a page for the median key.
	 */
	assert(slice->end == NULL || key_compare(mid_page->min_key,
			tuple_data(slice->end), index->key_def) < 0);

	*p_split_key = mid_page->min_key;
	return true;
}

/**
 * Split a range if it has grown too big, return true if the range
 * was split. Splitting is done by making slices of the runs used
 * by the original range, adding them to new ranges, and reflecting
 * the change in the metadata log, i.e. it doesn't involve heavy
 * operations, like writing a run file, and is done immediately.
 */
static bool
vy_range_maybe_split(struct vy_range *range)
{
	struct vy_index *index = range->index;
	struct tuple_format *key_format = index->env->key_format;
	struct vy_scheduler *scheduler = index->env->scheduler;

	const char *split_key_raw;
	if (!vy_range_needs_split(range, &split_key_raw))
		return false;

	/* Split a range in two parts. */
	const int n_parts = 2;

	/*
	 * Determine new ranges' boundaries.
	 */
	struct tuple *split_key = vy_key_from_msgpack(key_format,
						      split_key_raw);
	if (split_key == NULL)
		goto fail;

	struct tuple *keys[3];
	keys[0] = range->begin;
	keys[1] = split_key;
	keys[2] = range->end;

	/*
	 * Allocate new ranges and create slices of
	 * the old range's runs for them.
	 */
	struct vy_slice *slice, *new_slice;
	struct vy_range *part, *parts[2] = {NULL, };
	for (int i = 0; i < n_parts; i++) {
		part = vy_range_new(index, -1, keys[i], keys[i + 1]);
		if (part == NULL)
			goto fail;
		parts[i] = part;
		/*
		 * vy_range_add_slice() adds a slice to the list head,
		 * so to preserve the order of the slices list, we have
		 * to iterate backward.
		 */
		rlist_foreach_entry_reverse(slice, &range->slices, in_range) {
			if (vy_slice_cut(slice, vy_log_next_id(), part->begin,
					 part->end, index->key_def,
					 &new_slice) != 0)
				goto fail;
			if (new_slice != NULL)
				vy_range_add_slice(part, new_slice);
		}
		part->compact_priority = range->compact_priority;
	}
	tuple_unref(split_key);
	split_key = NULL;

	/*
	 * Log change in metadata.
	 */
	vy_log_tx_begin();
	rlist_foreach_entry(slice, &range->slices, in_range)
		vy_log_delete_slice(slice->id);
	vy_log_delete_range(range->id);
	for (int i = 0; i < n_parts; i++) {
		part = parts[i];
		vy_log_insert_range(index->opts.lsn, part->id,
				    tuple_data_or_null(part->begin),
				    tuple_data_or_null(part->end));
		rlist_foreach_entry(slice, &part->slices, in_range)
			vy_log_insert_slice(part->id, slice->run->id, slice->id,
					    tuple_data_or_null(slice->begin),
					    tuple_data_or_null(slice->end));
	}
	if (vy_log_tx_commit() < 0)
		goto fail;

	/*
	 * Replace the old range in the index.
	 */
	vy_scheduler_remove_range(scheduler, range);
	vy_index_unacct_range(index, range);
	vy_index_remove_range(index, range);

	for (int i = 0; i < n_parts; i++) {
		part = parts[i];
		vy_index_add_range(index, part);
		vy_index_acct_range(index, part);
		vy_scheduler_add_range(scheduler, part);
	}
	index->version++;

	say_info("%s: split range %s by key %s", vy_index_name(index),
		 vy_range_str(range), vy_key_str(split_key_raw));

	rlist_foreach_entry(slice, &range->slices, in_range)
		vy_slice_wait_pinned(slice);
	vy_range_delete(range);
	return true;
fail:
	for (int i = 0; i < n_parts; i++) {
		if (parts[i] != NULL)
			vy_range_delete(parts[i]);
	}
	if (split_key != NULL)
		tuple_unref(split_key);
	assert(!diag_is_empty(diag_get()));
	say_error("%s: failed to split range %s: %s",
		  vy_index_name(index), vy_range_str(range),
		  diag_last_error(diag_get())->errmsg);
	return false;
}

/**
 * To reduce write amplification caused by compaction, we follow
 * the LSM tree design. Runs in each range are divided into groups
 * called levels:
 *
 *   level 1: runs 1 .. L_1
 *   level 2: runs L_1 + 1 .. L_2
 *   ...
 *   level N: runs L_{N-1} .. L_N
 *
 * where L_N is the total number of runs, N is the total number of
 * levels, older runs have greater numbers. Runs at each subsequent
 * are run_size_ratio times larger than on the previous one. When
 * the number of runs at a level exceeds run_count_per_level, we
 * compact all its runs along with all runs from the upper levels
 * and in-memory indexes.  Including  previous levels into
 * compaction is relatively cheap, because of the level size
 * ratio.
 *
 * Given a range, this function computes the maximal level that needs
 * to be compacted and sets @compact_priority to the number of runs in
 * this level and all preceding levels.
 */
static void
vy_range_update_compact_priority(struct vy_range *range)
{
	struct index_opts *opts = &range->index->opts;

	assert(opts->run_count_per_level > 0);
	assert(opts->run_size_ratio > 1);

	range->compact_priority = 0;

	/* Total number of checked runs. */
	uint32_t total_run_count = 0;
	/* The total size of runs checked so far. */
	uint64_t total_size = 0;
	/* Estimated size of a compacted run, if compaction is scheduled. */
	uint64_t est_new_run_size = 0;
	/* The number of runs at the current level. */
	uint32_t level_run_count = 0;
	/*
	 * The target (perfect) size of a run at the current level.
	 * For the first level, it's the size of the newest run.
	 * For lower levels it's computed as first level run size
	 * times run_size_ratio.
	 */
	uint64_t target_run_size = 0;

	struct vy_slice *slice;
	rlist_foreach_entry(slice, &range->slices, in_range) {
		uint64_t size = slice->count.bytes_compressed;
		/*
		 * The size of the first level is defined by
		 * the size of the most recent run.
		 */
		if (target_run_size == 0)
			target_run_size = size;
		total_size += size;
		level_run_count++;
		total_run_count++;
		while (size > target_run_size) {
			/*
			 * The run size exceeds the threshold
			 * set for the current level. Move this
			 * run down to a lower level. Switch the
			 * current level and reset the level run
			 * count.
			 */
			level_run_count = 1;
			/*
			 * If we have already scheduled
			 * a compaction of an upper level, and
			 * estimated compacted run will end up at
			 * this level, include the new run into
			 * this level right away to avoid
			 * a cascading compaction.
			 */
			if (est_new_run_size > target_run_size)
				level_run_count++;
			/*
			 * Calculate the target run size for this
			 * level.
			 */
			target_run_size *= opts->run_size_ratio;
			/*
			 * Keep pushing the run down until
			 * we find an appropriate level for it.
			 */
		}
		if (level_run_count > opts->run_count_per_level) {
			/*
			 * The number of runs at the current level
			 * exceeds the configured maximum. Arrange
			 * for compaction. We compact all runs at
			 * this level and upper levels.
			 */
			range->compact_priority = total_run_count;
			est_new_run_size = total_size;
		}
	}
}

/**
 * Check if a range should be coalesced with one or more its neighbors.
 * If it should, return true and set @p_first and @p_last to the first
 * and last ranges to coalesce, otherwise return false.
 *
 * We coalesce ranges together when they become too small, less than
 * half the target range size to avoid split-coalesce oscillations.
 */
static bool
vy_range_needs_coalesce(struct vy_range *range,
			struct vy_range **p_first, struct vy_range **p_last)
{
	struct vy_index *index = range->index;
	struct vy_range *it;

	/* Size of the coalesced range. */
	uint64_t total_size = range->count.bytes_compressed;
	/* Coalesce ranges until total_size > max_size. */
	uint64_t max_size = index->opts.range_size / 2;

	/*
	 * We can't coalesce a range that was scheduled for dump
	 * or compaction, because it is about to be processed by
	 * a worker thread.
	 */
	assert(!vy_range_is_scheduled(range));

	*p_first = *p_last = range;
	for (it = vy_range_tree_next(index->tree, range);
	     it != NULL && !vy_range_is_scheduled(it);
	     it = vy_range_tree_next(index->tree, it)) {
		uint64_t size = it->count.bytes_compressed;
		if (total_size + size > max_size)
			break;
		total_size += size;
		*p_last = it;
	}
	for (it = vy_range_tree_prev(index->tree, range);
	     it != NULL && !vy_range_is_scheduled(it);
	     it = vy_range_tree_prev(index->tree, it)) {
		uint64_t size = it->count.bytes_compressed;
		if (total_size + size > max_size)
			break;
		total_size += size;
		*p_first = it;
	}
	return *p_first != *p_last;
}

/**
 * Coalesce a range with one or more its neighbors if it is too small,
 * return true if the range was coalesced. We coalesce ranges by
 * splicing their lists of run slices and reflecting the change in the
 * log. No long-term operation involving a worker thread, like writing
 * a new run file, is necessary, because the merge iterator can deal
 * with runs that intersect by LSN coexisting in the same range as long
 * as they do not intersect for each particular key, which is true in
 * case of merging key ranges.
 */
static bool
vy_range_maybe_coalesce(struct vy_range *range)
{
	struct vy_index *index = range->index;
	struct vy_scheduler *scheduler = index->env->scheduler;
	struct error *e;

	struct vy_range *first, *last;
	if (!vy_range_needs_coalesce(range, &first, &last))
		return false;

	struct vy_range *result = vy_range_new(index, -1,
					       first->begin, last->end);
	if (result == NULL)
		goto fail_range;

	struct vy_range *it;
	struct vy_range *end = vy_range_tree_next(index->tree, last);

	/*
	 * Log change in metadata.
	 */
	vy_log_tx_begin();
	vy_log_insert_range(index->opts.lsn, result->id,
			    tuple_data_or_null(result->begin),
			    tuple_data_or_null(result->end));
	for (it = first; it != end; it = vy_range_tree_next(index->tree, it)) {
		struct vy_slice *slice;
		rlist_foreach_entry(slice, &it->slices, in_range)
			vy_log_delete_slice(slice->id);
		vy_log_delete_range(it->id);
		rlist_foreach_entry(slice, &it->slices, in_range) {
			vy_log_insert_slice(result->id, slice->run->id, slice->id,
					    tuple_data_or_null(slice->begin),
					    tuple_data_or_null(slice->end));
		}
	}
	if (vy_log_tx_commit() < 0)
		goto fail_commit;

	/*
	 * Move run slices of the coalesced ranges to the
	 * resulting range and delete the former.
	 */
	it = first;
	while (it != end) {
		struct vy_range *next = vy_range_tree_next(index->tree, it);
		vy_scheduler_remove_range(scheduler, it);
		vy_index_unacct_range(index, it);
		vy_index_remove_range(index, it);
		rlist_splice(&result->slices, &it->slices);
		result->slice_count += it->slice_count;
		vy_disk_stmt_counter_add(&result->count, &it->count);
		vy_range_delete(it);
		it = next;
	}
	/*
	 * Coalescing increases read amplification and breaks the log
	 * structured layout of the run list, so, although we could
	 * leave the resulting range as it is, we'd better compact it
	 * as soon as we can.
	 */
	result->compact_priority = result->slice_count;
	vy_index_acct_range(index, result);
	vy_index_add_range(index, result);
	index->version++;
	vy_scheduler_add_range(scheduler, result);

	say_info("%s: coalesced ranges %s",
		 vy_index_name(index), vy_range_str(result));
	return true;

fail_commit:
	vy_range_delete(result);
fail_range:
	assert(!diag_is_empty(diag_get()));
	e = diag_last_error(diag_get());
	say_error("%s: failed to coalesce range %s: %s",
		  vy_index_name(index), vy_range_str(range), e->errmsg);
	return false;
}

/**
 * Create an index directory for a new index.
 */
static int
vy_index_create(struct vy_index *index)
{
	/* create directory */
	int rc;
	char path[PATH_MAX];
	vy_index_snprint_path(path, sizeof(path), index->env->conf->path,
			      index->space_id, index->id);
	char *path_sep = path;
	while (*path_sep == '/') {
		/* Don't create root */
		++path_sep;
	}
	while ((path_sep = strchr(path_sep, '/'))) {
		/* Recursively create path hierarchy */
		*path_sep = '\0';
		rc = mkdir(path, 0777);
		if (rc == -1 && errno != EEXIST) {
			diag_set(SystemError, "failed to create directory '%s'",
		                 path);
			*path_sep = '/';
			return -1;
		}
		*path_sep = '/';
		++path_sep;
	}
	rc = mkdir(path, 0777);
	if (rc == -1 && errno != EEXIST) {
		diag_set(SystemError, "failed to create directory '%s'",
			 path);
		return -1;
	}

	/* create initial range */
	struct vy_range *range = vy_range_new(index, -1, NULL, NULL);
	if (unlikely(range == NULL))
		return -1;

	assert(index->range_count == 0);
	vy_index_add_range(index, range);
	vy_index_acct_range(index, range);
	return 0;
}

/**
 * A quick intro into Vinyl cosmology and file format
 * --------------------------------------------------
 * A single vinyl index on disk consists of a set of "range"
 * objects. A range contains a sorted set of index keys;
 * keys in different ranges do not overlap and all ranges of the
 * same index together span the whole key space, for example:
 * (-inf..100), [100..114), [114..304), [304..inf)
 *
 * A sorted set of keys in a range is called a run. A single
 * range may contain multiple runs, each run contains changes of
 * keys in the range over a certain period of time. The periods do
 * not overlap, while, of course, two runs of the same range may
 * contain changes of the same key.
 * All keys in a run are sorted and split between pages of
 * approximately equal size. The purpose of putting keys into
 * pages is a quicker key lookup, since (min,max) key of every
 * page is put into the page index, stored at the beginning of each
 * run. The page index of an active run is fully cached in RAM.
 *
 * All files of an index have the following name pattern:
 * <run_id>.{run,index}
 * and are stored together in the index directory.
 *
 * Files that end with '.index' store page index (see vy_run_info)
 * while '.run' files store vinyl statements.
 *
 * <run_id> is the unique id of this run. Newer runs have greater ids.
 *
 * Information about which run id belongs to which range is stored
 * in vinyl.meta file.
 */

/** vy_index_recovery_cb() argument. */
struct vy_index_recovery_cb_arg {
	/** Index being recovered. */
	struct vy_index *index;
	/** Last recovered range. */
	struct vy_range *range;
	/**
	 * All recovered runs hashed by ID.
	 * It is needed in order not to load the same
	 * run each time a slice is created for it.
	 */
	struct mh_i64ptr_t *run_hash;
};

/** Index recovery callback, passed to vy_recovery_iterate_index(). */
static int
vy_index_recovery_cb(const struct vy_log_record *record, void *cb_arg)
{
	struct vy_index_recovery_cb_arg *arg = cb_arg;
	struct vy_index *index = arg->index;
	struct vy_range *range = arg->range;
	struct mh_i64ptr_t *run_hash = arg->run_hash;
	struct tuple_format *key_format = index->env->key_format;
	struct tuple *begin = NULL, *end = NULL;
	struct vy_run *run;
	struct vy_slice *slice;
	bool success = false;

	if (record->type == VY_LOG_INSERT_RANGE ||
	    record->type == VY_LOG_INSERT_SLICE) {
		if (record->begin != NULL) {
			begin = vy_key_from_msgpack(key_format, record->begin);
			if (begin == NULL)
				goto out;
		}
		if (record->end != NULL) {
			end = vy_key_from_msgpack(key_format, record->end);
			if (end == NULL)
				goto out;
		}
	}

	switch (record->type) {
	case VY_LOG_CREATE_INDEX:
		assert(record->index_lsn == index->opts.lsn);
		break;
	case VY_LOG_DUMP_INDEX:
		assert(record->index_lsn == index->opts.lsn);
		index->dump_lsn = record->dump_lsn;
		break;
	case VY_LOG_TRUNCATE_INDEX:
		assert(record->index_lsn == index->opts.lsn);
		index->truncate_count = record->truncate_count;
		break;
	case VY_LOG_DROP_INDEX:
		assert(record->index_lsn == index->opts.lsn);
		index->is_dropped = true;
		break;
	case VY_LOG_CREATE_RUN:
		assert(record->index_lsn == index->opts.lsn);
		run = vy_run_new(record->run_id);
		if (run == NULL)
			goto out;
		run->dump_lsn = record->dump_lsn;
		if (vy_run_recover(run, index->env->conf->path,
				   index->space_id, index->id) != 0) {
			vy_run_unref(run);
			goto out;
		}
		struct mh_i64ptr_node_t node = { run->id, run };
		if (mh_i64ptr_put(run_hash, &node,
				  NULL, NULL) == mh_end(run_hash)) {
			diag_set(OutOfMemory, 0,
				 "mh_i64ptr_put", "mh_i64ptr_node_t");
			vy_run_unref(run);
			goto out;
		}
		break;
	case VY_LOG_INSERT_RANGE:
		assert(record->index_lsn == index->opts.lsn);
		range = vy_range_new(index, record->range_id, begin, end);
		if (range == NULL)
			goto out;
		if (range->begin != NULL && range->end != NULL &&
		    vy_key_compare(range->begin, range->end,
				   index->key_def) >= 0) {
			diag_set(ClientError, ER_INVALID_VYLOG_FILE,
				 tt_sprintf("begin >= end for range id %lld",
					    (long long)range->id));
			vy_range_delete(range);
			goto out;
		}
		vy_index_add_range(index, range);
		arg->range = range;
		break;
	case VY_LOG_INSERT_SLICE:
		assert(range != NULL);
		assert(range->id == record->range_id);
		mh_int_t k = mh_i64ptr_find(run_hash, record->run_id, NULL);
		assert(k != mh_end(run_hash));
		run = mh_i64ptr_node(run_hash, k)->val;
		slice = vy_slice_new(record->slice_id, run, begin, end,
				     index->key_def);
		if (slice == NULL)
			goto out;
		vy_range_add_slice(range, slice);
		break;
	default:
		unreachable();
	}
	success = true;
out:
	if (begin != NULL)
		tuple_unref(begin);
	if (end != NULL)
		tuple_unref(end);
	return success ? 0 : -1;
}

/**
 * Load a vinyl index from disk. Called on local recovery.
 *
 * This function retrieves the index structure from the
 * metadata log, rebuilds the range tree, and opens run
 * files.
 */
static int
vy_index_recover(struct vy_index *index)
{
	struct vy_env *env = index->env;

	assert(env->recovery != NULL);
	assert(index->range_count == 0);

	struct vy_index_recovery_info *ri;
	ri = vy_recovery_lookup_index(env->recovery, index->opts.lsn);
	if (ri == NULL) {
		if (env->status == VINYL_INITIAL_RECOVERY_LOCAL) {
			diag_set(ClientError, ER_INVALID_VYLOG_FILE,
				 tt_sprintf("Index %lld not found",
					    (long long)index->opts.lsn));
			return -1;
		}
		/*
		 * If we failed to log index creation before restart,
		 * we won't find it in the log on recovery. This is
		 * OK as the index doesn't have any runs in this case.
		 * We will retry to log index in vy_index_commit_create().
		 * For now, just create the initial range.
		 */
		assert(env->status == VINYL_FINAL_RECOVERY_LOCAL);
		return vy_index_create(index);
	}

	struct vy_index_recovery_cb_arg arg = { .index = index };
	arg.run_hash = mh_i64ptr_new();
	if (arg.run_hash == NULL) {
		diag_set(OutOfMemory, 0, "mh_i64ptr_new", "mh_i64ptr_t");
		return -1;
	}

	int rc = vy_recovery_iterate_index(ri, false,
			vy_index_recovery_cb, &arg);

	mh_int_t k;
	mh_foreach(arg.run_hash, k) {
		struct vy_run *run = mh_i64ptr_node(arg.run_hash, k)->val;
		if (run->refs == 1) {
			diag_set(ClientError, ER_INVALID_VYLOG_FILE,
				 tt_sprintf("Unused run %lld in index %lld",
					    (long long)run->id,
					    (long long)index->opts.lsn));
			rc = -1;
		} else
			vy_index_add_run(index, run);
		/* Drop the reference held by the hash. */
		vy_run_unref(run);
	}
	mh_i64ptr_delete(arg.run_hash);

	if (rc != 0)
		return -1;

	/*
	 * Account ranges to the index and check that the range tree
	 * does not have holes or overlaps.
	 */
	struct vy_range *range, *prev = NULL;
	for (range = vy_range_tree_first(index->tree); range != NULL;
	     prev = range, range = vy_range_tree_next(index->tree, range)) {
		if (prev == NULL && range->begin != NULL) {
			diag_set(ClientError, ER_INVALID_VYLOG_FILE,
				 tt_sprintf("Range %lld is leftmost but "
					    "starts with a finite key",
					    (long long)range->id));
			return -1;
		}
		int cmp = 0;
		if (prev != NULL &&
		    (prev->end == NULL || range->begin == NULL ||
		     (cmp = vy_key_compare(prev->end, range->begin,
					   index->key_def)) != 0)) {
			const char *errmsg = cmp > 0 ?
				"Nearby ranges %lld and %lld overlap" :
				"Keys between ranges %lld and %lld not spanned";
			diag_set(ClientError, ER_INVALID_VYLOG_FILE,
				 tt_sprintf(errmsg,
					    (long long)prev->id,
					    (long long)range->id));
			return -1;
		}
		vy_index_acct_range(index, range);
		vy_scheduler_add_range(env->scheduler, range);
	}
	if (prev == NULL) {
		diag_set(ClientError, ER_INVALID_VYLOG_FILE,
			 tt_sprintf("Index %lld has empty range tree",
				    (long long)index->opts.lsn));
		return -1;
	}
	if (prev->end != NULL) {
		diag_set(ClientError, ER_INVALID_VYLOG_FILE,
			 tt_sprintf("Range %lld is rightmost but "
				    "ends with a finite key",
				    (long long)prev->id));
		return -1;
	}
	vy_scheduler_add_index(env->scheduler, index);
	return 0;
}

/*
 * Insert a statement into the index's in-memory tree. If the
 * region_stmt is NULL and the statement is successfully inserted
 * then the new lsregion statement is returned via @a region_stmt.
 * Either vy_index_commit_stmt() or vy_index_rollback_stmt() must
 * be called on success.
 *
 * @param index Index the statement is for.
 * @param mem In-memory tree to insert the statement into.
 * @param stmt Statement, allocated on malloc().
 * @param region_stmt NULL or the same statement, allocated on
 *                    lsregion.
 * @retval  0 Success.
 * @retval -1 Memory error.
 */
static int
vy_index_set(struct vy_index *index, struct vy_mem *mem,
	     const struct tuple *stmt, const struct tuple **region_stmt)
{
	assert(!vy_stmt_is_region_allocated(stmt));
	assert(*region_stmt == NULL ||
	       vy_stmt_is_region_allocated(*region_stmt));
	struct lsregion *allocator = &index->env->allocator;

	/* Allocate region_stmt on demand. */
	if (*region_stmt == NULL) {
		*region_stmt = vy_stmt_dup_lsregion(stmt, allocator,
						    mem->generation);
		if (*region_stmt == NULL)
			return -1;
	}

	/* We can't free region_stmt below, so let's add it to the stats */
	index->stat.memory.count.bytes += tuple_size(stmt);

	if (vy_stmt_type(*region_stmt) != IPROTO_UPSERT)
		return vy_mem_insert(mem, *region_stmt);
	else
		return vy_mem_insert_upsert(mem, *region_stmt);
}

static void
vy_index_squash_upserts(struct vy_index *index, struct tuple *stmt);

static void
vy_index_commit_upsert(struct vy_index *index, struct vy_mem *mem,
		       const struct tuple *stmt);

/*
 * Confirm that the statement stays in the index's in-memory tree.
 * @param index Index the statement is for.
 * @param mem In-memory tree where the statement was saved.
 * @param stmt Statement allocated from lsregion.
 */
static void
vy_index_commit_stmt(struct vy_index *index, struct vy_mem *mem,
		     const struct tuple *stmt)
{
	vy_mem_commit_stmt(mem, stmt);

	index->stat.memory.count.rows++;

	if (vy_stmt_type(stmt) == IPROTO_UPSERT)
		vy_index_commit_upsert(index, mem, stmt);

	/* Invalidate cache element. */
	vy_cache_on_write(&index->cache, stmt, NULL);
}

/*
 * Erase a statement from the index's in-memory tree.
 * @param index Index to erase from.
 * @param mem In-memory tree where the statement was saved.
 * @param stmt Statement allocated from lsregion.
 */
static void
vy_index_rollback_stmt(struct vy_index *index, struct vy_mem *mem,
		       const struct tuple *stmt)
{
	vy_mem_rollback_stmt(mem, stmt);

	/* Invalidate cache element. */
	vy_cache_on_write(&index->cache, stmt, NULL);
}

/**
 * Calculate and record the number of seqential upserts, squash
 * immediatelly or schedule upsert process if needed.
 * Additional handler used in vy_index_commit_stmt() for UPSERT
 * statements.
 *
 * @param index - index that the statement was committed to.
 * @param mem - mem that the statement was committed to.
 * @param stmt - mem UPSERT statement.
 */
static void
vy_index_commit_upsert(struct vy_index *index, struct vy_mem *mem,
		       const struct tuple *stmt)
{
	assert(vy_stmt_type(stmt) == IPROTO_UPSERT);
	assert(vy_stmt_lsn(stmt) < MAX_LSN);
	/*
	 * UPSERT is enabled only for the spaces with the single
	 * index.
	 */
	assert(index->id == 0);

	struct vy_stat *stat = index->env->stat;
	const struct tuple *older;
	int64_t lsn = vy_stmt_lsn(stmt);
	uint8_t n_upserts = vy_stmt_n_upserts(stmt);
	/*
	 * If there are a lot of successive upserts for the same key,
	 * select might take too long to squash them all. So once the
	 * number of upserts exceeds a certain threshold, we schedule
	 * a fiber to merge them and insert the resulting statement
	 * after the latest upsert.
	 */
	if (n_upserts == VY_UPSERT_INF) {
		/*
		 * If UPSERT has n_upserts > VY_UPSERT_THRESHOLD,
		 * it means the mem has older UPSERTs for the same
		 * key which already are beeing processed in the
		 * squashing task. At the end, the squashing task
		 * will merge its result with this UPSERT
		 * automatically.
		 */
		return;
	}
	if (n_upserts == VY_UPSERT_THRESHOLD) {
		/*
		 * Start single squashing task per one-mem and
		 * one-key continous UPSERTs sequence.
		 */
#ifndef NDEBUG
		older = vy_mem_older_lsn(mem, stmt);
		assert(older != NULL && vy_stmt_type(older) == IPROTO_UPSERT &&
		       vy_stmt_n_upserts(older) == VY_UPSERT_THRESHOLD - 1);
#endif
		struct tuple *dup = vy_stmt_dup(stmt, index->upsert_format);
		if (dup != NULL) {
			vy_index_squash_upserts(index, dup);
			tuple_unref(dup);
		}
		/*
		 * Ignore dup == NULL, because the optimization is
		 * good, but is not necessary.
		 */
		return;
	}

	/*
	 * If there are no other mems and runs and n_upserts == 0,
	 * then we can turn the UPSERT into the REPLACE.
	 */
	if (n_upserts == 0 &&
	    index->stat.memory.count.rows == index->mem->count.rows &&
	    index->run_count == 0) {
		older = vy_mem_older_lsn(mem, stmt);
		assert(older == NULL || vy_stmt_type(older) != IPROTO_UPSERT);
		struct tuple *upserted =
			vy_apply_upsert(stmt, older, index->key_def,
					index->space_format,
					index->upsert_format, false);
		rmean_collect(stat->rmean, VY_STAT_UPSERT_APPLIED, 1);

		if (upserted == NULL) {
			/* OOM */
			diag_clear(diag_get());
			return;
		}
		int64_t upserted_lsn = vy_stmt_lsn(upserted);
		if (upserted_lsn != lsn) {
			/**
			 * This could only happen if the upsert completely
			 * failed and the old tuple was returned.
			 * In this case we shouldn't insert the same replace
			 * again.
			 */
			assert(older == NULL ||
			       upserted_lsn == vy_stmt_lsn(older));
			tuple_unref(upserted);
			return;
		}
		assert(older == NULL || upserted_lsn != vy_stmt_lsn(older));
		assert(vy_stmt_type(upserted) == IPROTO_REPLACE);
		struct lsregion *allocator = &index->env->allocator;

		size_t mem_used_before = lsregion_used(allocator);

		const struct tuple *region_stmt =
			vy_stmt_dup_lsregion(upserted, allocator, mem->generation);
		if (region_stmt == NULL) {
			/* OOM */
			tuple_unref(upserted);
			diag_clear(diag_get());
			return;
		}

		size_t mem_used_after = lsregion_used(allocator);
		assert(mem_used_after >= mem_used_before);
		size_t mem_increment = mem_used_after - mem_used_before;
		vy_quota_force_use(&index->env->quota, mem_increment);

		int rc = vy_index_set(index, mem, upserted, &region_stmt);
		/**
		 * Since we have already allocated mem statement and
		 * now we replacing one statement with another, the
		 * vy_index_set() cannot fail.
		 */
		assert(rc == 0); (void)rc;
		tuple_unref(upserted);
		vy_mem_commit_stmt(mem, region_stmt);
		rmean_collect(stat->rmean, VY_STAT_UPSERT_SQUASHED, 1);
	}
}

/**
 * Rotate the active in-memory tree if necessary and pin it to make
 * sure it is not dumped until the transaction is complete.
 */
static int
vy_tx_write_prepare(struct txv *v)
{
	struct vy_index *index = v->index;
	struct vy_scheduler *scheduler = index->env->scheduler;

	/*
	 * Allocate a new in-memory tree if either of the following
	 * conditions is true:
	 *
	 * - Generation has increased after the tree was created.
	 *   In this case we need to dump the tree as is in order to
	 *   guarantee dump consistency.
	 *
	 * - Schema version has increased after the tree was created.
	 *   We have to seal the tree, because we don't support mixing
	 *   statements of different formats in the same tree.
	 */
	if (unlikely(index->mem->schema_version != schema_version ||
		     index->mem->generation != scheduler->generation)) {
		if (vy_index_rotate_mem(index) != 0)
			return -1;
	}
	vy_mem_pin(index->mem);
	v->mem = index->mem;
	return 0;
}

/**
 * Write a single statement into an index. If the statement has an
 * lsregion copy then use it, else create it.
 * @param index Index to write to.
 * @param mem In-memory tree to write to.
 * @param stmt Statement allocated from malloc().
 * @param region_stmt NULL or the same statement as stmt,
 *                    but allocated on lsregion.
 * @param status Vinyl engine status.
 *
 * @retval  0 Success.
 * @retval -1 Memory error.
 */
static int
vy_tx_write(struct vy_index *index, struct vy_mem *mem,
	    struct tuple *stmt, const struct tuple **region_stmt)
{
	assert(!vy_stmt_is_region_allocated(stmt));
	assert(*region_stmt == NULL ||
	       vy_stmt_is_region_allocated(*region_stmt));

	/*
	 * The UPSERT statement can be applied to the cached
	 * statement, because the cache always contains only
	 * newest REPLACE statements. In such a case the UPSERT,
	 * applied to the cached statement, can be inserted
	 * instead of the original UPSERT.
	 */
	if (vy_stmt_type(stmt) == IPROTO_UPSERT) {
		struct tuple *deleted = NULL;
		/* Invalidate cache element. */
		vy_cache_on_write(&index->cache, stmt, &deleted);
		if (deleted != NULL) {
			struct tuple *applied =
				vy_apply_upsert(stmt, deleted, mem->key_def,
						mem->format, mem->upsert_format,
						false);
			tuple_unref(deleted);
			if (applied != NULL) {
				assert(vy_stmt_type(applied) == IPROTO_REPLACE);
				int rc = vy_index_set(index, mem, applied,
						      region_stmt);
				tuple_unref(applied);
				return rc;
			}
			/*
			 * Ignore a memory error, because it is
			 * not critical to apply the optimization.
			 */
		}
	} else {
		/* Invalidate cache element. */
		vy_cache_on_write(&index->cache, stmt, NULL);
	}
	return vy_index_set(index, mem, stmt, region_stmt);
}

/* {{{ Scheduler Task */

struct vy_task_ops {
	/**
	 * This function is called from a worker. It is supposed to do work
	 * which is too heavy for the tx thread (like IO or compression).
	 * Returns 0 on success. On failure returns -1 and sets diag.
	 */
	int (*execute)(struct vy_task *task);
	/**
	 * This function is called by the scheduler upon task completion.
	 * It may be used to finish the task from the tx thread context.
	 *
	 * Returns 0 on success. On failure returns -1 and sets diag.
	 */
	int (*complete)(struct vy_task *task);
	/**
	 * This function is called by the scheduler if either ->execute
	 * or ->complete failed. It may be used to undo changes done to
	 * the index when preparing the task.
	 *
	 * If @in_shutdown is set, the callback is invoked from the
	 * engine destructor.
	 */
	void (*abort)(struct vy_task *task, bool in_shutdown);
};

struct vy_task {
	const struct vy_task_ops *ops;
	/** Return code of ->execute. */
	int status;
	/** If ->execute fails, the error is stored here. */
	struct diag diag;
	/** Index this task is for. */
	struct vy_index *index;
	/** Number of bytes written to disk by this task. */
	size_t dump_size;
	/** Number of statements dumped to the disk. */
	uint64_t dumped_statements;
	/** Range to compact. */
	struct vy_range *range;
	/** Run written by this task. */
	struct vy_run *new_run;
	/** Write iterator producing statements for the new run. */
	struct vy_stmt_stream *wi;
	/**
	 * The current generation at the time of task start.
	 * On success a dump task dumps all in-memory trees
	 * whose generation is less.
	 */
	int64_t generation;
	/**
	 * First (newest) and last (oldest) slices to compact.
	 *
	 * While a compaction task is in progress, a new slice
	 * can be added to a range by concurrent dump, so we
	 * need to remember the slices we are compacting.
	 */
	struct vy_slice *first_slice, *last_slice;
	/**
	 * A link in the list of all pending tasks, generated by
	 * task scheduler.
	 */
	struct stailq_entry link;
	/** For run-writing tasks: maximum possible number of tuple to write */
	size_t max_output_count;
	/**
	 * Save the snapshot of some index_opts attributes since
	 * they can be modified in the index->opts by
	 * an index:alter() call.
	 */
	double bloom_fpr;
	int64_t page_size;
};

/**
 * Allocate a new task to be executed by a worker thread.
 * When preparing an asynchronous task, this function must
 * be called before yielding the current fiber in order to
 * pin the index the task is for so that a concurrent fiber
 * does not free it from under us.
 */
static struct vy_task *
vy_task_new(struct mempool *pool, struct vy_index *index,
	    const struct vy_task_ops *ops)
{
	struct vy_task *task = mempool_alloc(pool);
	if (task == NULL) {
		diag_set(OutOfMemory, sizeof(*task), "scheduler", "task");
		return NULL;
	}
	memset(task, 0, sizeof(*task));
	task->ops = ops;
	task->index = index;
	vy_index_ref(index);
	diag_create(&task->diag);
	return task;
}

/** Free a task allocated with vy_task_new(). */
static void
vy_task_delete(struct mempool *pool, struct vy_task *task)
{
	vy_index_unref(task->index);
	diag_destroy(&task->diag);
	TRASH(task);
	mempool_free(pool, task);
}

static int
vy_task_dump_execute(struct vy_task *task)
{
	struct vy_index *index = task->index;
	/* The index has been deleted from the scheduler queues. */
	assert(index->in_dump.pos == UINT32_MAX);

	return vy_run_write(task->new_run, index->env->conf->path,
			    index->space_id, index->id, task->wi,
			    task->page_size, index->key_def,
			    index->user_key_def, task->max_output_count,
			    task->bloom_fpr, &task->dump_size,
			    &task->dumped_statements);
}

static int
vy_task_dump_complete(struct vy_task *task)
{
	struct vy_index *index = task->index;
	struct vy_run *new_run = task->new_run;
	int64_t dump_lsn = new_run->dump_lsn;
	struct vy_scheduler *scheduler = index->env->scheduler;
	struct tuple_format *key_format = index->env->key_format;
	struct vy_mem *mem, *next_mem;
	struct vy_slice **new_slices, *slice;
	struct vy_range *range, *begin_range, *end_range;
	struct tuple *min_key, *max_key;
	int i, loops = 0;

	if (vy_run_is_empty(new_run)) {
		/*
		 * In case the run is empty, we can discard the run
		 * and delete dumped in-memory trees right away w/o
		 * inserting slices into ranges. However, we need
		 * to log index dump anyway.
		 */
		vy_log_tx_begin();
		vy_log_dump_index(index->opts.lsn, dump_lsn);
		if (vy_log_tx_commit() < 0)
			goto fail;
		vy_run_discard(new_run);
		goto delete_mems;
	}

	assert(new_run->info.min_lsn > index->dump_lsn);
	assert(new_run->info.max_lsn <= dump_lsn);

	/*
	 * Figure out which ranges intersect the new run.
	 * @begin_range is the first range intersecting the run.
	 * @end_range is the range following the last range
	 * intersecting the run or NULL if the run itersects all
	 * ranges.
	 */
	min_key = vy_key_from_msgpack(key_format, new_run->info.min_key);
	if (min_key == NULL)
		goto fail;
	max_key = vy_key_from_msgpack(key_format, new_run->info.max_key);
	if (max_key == NULL) {
		tuple_unref(min_key);
		goto fail;
	}
	begin_range = vy_range_tree_psearch(index->tree, min_key);
	end_range = vy_range_tree_nsearch(index->tree, max_key);
	tuple_unref(min_key);
	tuple_unref(max_key);

	/*
	 * For each intersected range allocate a slice of the new run.
	 */
	new_slices = calloc(index->range_count, sizeof(*new_slices));
	if (new_slices == NULL) {
		diag_set(OutOfMemory, index->range_count * sizeof(*new_slices),
			 "malloc", "struct vy_slice *");
		goto fail;
	}
	for (range = begin_range, i = 0; range != end_range;
	     range = vy_range_tree_next(index->tree, range), i++) {
		slice = vy_slice_new(vy_log_next_id(), new_run,
				     range->begin, range->end, index->key_def);
		if (slice == NULL)
			goto fail_free_slices;

		assert(i < index->range_count);
		new_slices[i] = slice;
		/*
		 * It's OK to yield here for the range tree can only
		 * be changed from the scheduler fiber.
		 */
		if (++loops % VY_YIELD_LOOPS == 0)
			fiber_sleep(0);
	}

	/*
	 * Log change in metadata.
	 */
	vy_log_tx_begin();
	vy_log_create_run(index->opts.lsn, new_run->id, dump_lsn);
	for (range = begin_range, i = 0; range != end_range;
	     range = vy_range_tree_next(index->tree, range), i++) {
		assert(i < index->range_count);
		slice = new_slices[i];
		vy_log_insert_slice(range->id, new_run->id, slice->id,
				    tuple_data_or_null(slice->begin),
				    tuple_data_or_null(slice->end));

		if (++loops % VY_YIELD_LOOPS == 0)
			fiber_sleep(0); /* see comment above */
	}
	vy_log_dump_index(index->opts.lsn, dump_lsn);
	if (vy_log_tx_commit() < 0)
		goto fail_free_slices;

	/*
	 * Account the new run.
	 */
	vy_index_add_run(index, new_run);

	/* Drop the reference held by the task. */
	vy_run_unref(new_run);

	/*
	 * Add new slices to ranges.
	 */
	for (range = begin_range, i = 0; range != end_range;
	     range = vy_range_tree_next(index->tree, range), i++) {
		assert(i < index->range_count);
		slice = new_slices[i];
		vy_index_unacct_range(index, range);
		vy_range_add_slice(range, slice);
		vy_index_acct_range(index, range);
		vy_range_update_compact_priority(range);
		if (!vy_range_is_scheduled(range))
			vy_scheduler_update_range(scheduler, range);
		range->version++;
		/*
		 * If we yield here, a concurrent fiber will see
		 * a range with a run slice containing statements
		 * present in the in-memory trees of the index.
		 * This is OK, because read iterator won't use the
		 * new run slice until index->dump_lsn is bumped,
		 * which is only done after in-memory trees are
		 * removed (see vy_read_iterator_add_disk()).
		 */
		if (++loops % VY_YIELD_LOOPS == 0)
			fiber_sleep(0);
	}
	free(new_slices);

delete_mems:
	/*
	 * Delete dumped in-memory trees.
	 */
	rlist_foreach_entry_safe(mem, &index->sealed, in_sealed, next_mem) {
		if (mem->generation >= task->generation)
			continue;
		rlist_del_entry(mem, in_sealed);
		vy_stmt_counter_sub(&index->stat.memory.count, &mem->count);
		vy_scheduler_remove_mem(scheduler, mem);
		vy_mem_delete(mem);
	}
	index->version++;
	index->dump_lsn = dump_lsn;
	index->generation = task->generation;

	/* The iterator has been cleaned up in a worker thread. */
	task->wi->iface->close(task->wi);

	vy_scheduler_add_index(scheduler, index);
	if (index->id != 0)
		vy_scheduler_unpin_index(scheduler, index->pk);

	say_info("%s: dump completed", vy_index_name(index));
	return 0;

fail_free_slices:
	for (i = 0; i < index->range_count; i++) {
		slice = new_slices[i];
		if (slice != NULL)
			vy_slice_delete(slice);
		if (++loops % VY_YIELD_LOOPS == 0)
			fiber_sleep(0);
	}
	free(new_slices);
fail:
	return -1;
}

static void
vy_task_dump_abort(struct vy_task *task, bool in_shutdown)
{
	struct vy_index *index = task->index;
	struct vy_scheduler *scheduler = index->env->scheduler;

	/* The iterator has been cleaned up in a worker thread. */
	task->wi->iface->close(task->wi);

	if (!in_shutdown && !index->is_dropped) {
		say_error("%s: dump failed: %s", vy_index_name(index),
			  diag_last_error(&task->diag)->errmsg);
		vy_run_discard(task->new_run);
	} else
		vy_run_unref(task->new_run);

	vy_scheduler_add_index(scheduler, index);
	if (index->id != 0)
		vy_scheduler_unpin_index(scheduler, index->pk);
}

/**
 * Create a task to dump an index.
 *
 * On success the task is supposed to dump all in-memory
 * trees older than @scheduler->generation.
 */
static int
vy_task_dump_new(struct vy_index *index, struct vy_task **p_task)
{
	static struct vy_task_ops dump_ops = {
		.execute = vy_task_dump_execute,
		.complete = vy_task_dump_complete,
		.abort = vy_task_dump_abort,
	};

	struct tx_manager *xm = index->env->xm;
	struct vy_scheduler *scheduler = index->env->scheduler;
	int64_t generation = scheduler->generation;

	assert(index->pin_count == 0);
	assert(index->generation < generation);

	if (index->is_dropped) {
		vy_scheduler_remove_index(scheduler, index);
		return 0;
	}

	struct errinj *inj = errinj(ERRINJ_VY_INDEX_DUMP, ERRINJ_INT);
	if (inj != NULL && inj->iparam == (int)index->id) {
		diag_set(ClientError, ER_INJECTION, "vinyl index dump");
		goto err;
	}

	/* Rotate the active tree if it needs to be dumped. */
	if (index->mem->generation < generation &&
	    vy_index_rotate_mem(index) != 0)
		goto err;

	/*
	 * Wait until all active writes to in-memory trees
	 * eligible for dump are over.
	 */
	int64_t dump_lsn = -1;
	size_t max_output_count = 0;
	struct vy_mem *mem, *next_mem;
	rlist_foreach_entry_safe(mem, &index->sealed, in_sealed, next_mem) {
		if (mem->generation >= generation)
			continue;
		vy_mem_wait_pinned(mem);
		if (mem->tree.size == 0) {
			/*
			 * The tree is empty so we can delete it
			 * right away, without involving a worker.
			 */
			vy_stmt_counter_sub(&index->stat.memory.count,
					    &mem->count);
			rlist_del_entry(mem, in_sealed);
			vy_scheduler_remove_mem(scheduler, mem);
			vy_mem_delete(mem);
			continue;
		}
		dump_lsn = MAX(dump_lsn, mem->max_lsn);
		max_output_count += mem->tree.size;
	}

	if (max_output_count == 0) {
		/* Nothing to do, pick another index. */
		index->generation = generation;
		vy_scheduler_update_index(scheduler, index);
		return 0;
	}

	struct vy_task *task = vy_task_new(&scheduler->task_pool,
					   index, &dump_ops);
	if (task == NULL)
		goto err;

	struct vy_run *new_run = vy_run_prepare(index);
	if (new_run == NULL)
		goto err_run;

	assert(dump_lsn >= 0);
	new_run->dump_lsn = dump_lsn;

	struct vy_stmt_stream *wi;
	bool is_last_level = (index->run_count == 0);
	wi = vy_write_iterator_new(index->key_def, index->surrogate_format,
				   index->upsert_format, index->id == 0,
				   is_last_level, tx_manager_vlsn(xm));
	if (wi == NULL)
		goto err_wi;
	rlist_foreach_entry(mem, &index->sealed, in_sealed) {
		if (mem->generation >= generation)
			continue;
		if (vy_write_iterator_add_mem(wi, mem) != 0)
			goto err_wi_sub;
	}

	task->new_run = new_run;
	task->wi = wi;
	task->generation = generation;
	task->max_output_count = max_output_count;
	task->bloom_fpr = index->opts.bloom_fpr;
	task->page_size = index->opts.page_size;

	vy_scheduler_remove_index(scheduler, index);
	if (index->id != 0) {
		/*
		 * The primary index must be dumped after all
		 * secondary indexes of the same space - see
		 * vy_dump_heap_less(). To make sure it isn't
		 * picked by the scheduler while all secondary
		 * indexes are being dumped, temporarily remove
		 * it from the dump heap.
		 */
		vy_scheduler_pin_index(scheduler, index->pk);
	}

	say_info("%s: dump started", vy_index_name(index));
	*p_task = task;
	return 0;

err_wi_sub:
	task->wi->iface->close(wi);
err_wi:
	vy_run_discard(new_run);
err_run:
	vy_task_delete(&scheduler->task_pool, task);
err:
	say_error("%s: could not start dump: %s", vy_index_name(index),
		  diag_last_error(diag_get())->errmsg);
	return -1;
}

static int
vy_task_compact_execute(struct vy_task *task)
{
	struct vy_index *index = task->index;
	/* The range has been deleted from the scheduler queues. */
	assert(task->range->in_compact.pos == UINT32_MAX);

	return vy_run_write(task->new_run, index->env->conf->path,
			    index->space_id, index->id, task->wi,
			    task->page_size, index->key_def,
			    index->user_key_def, task->max_output_count,
			    task->bloom_fpr, &task->dump_size,
			    &task->dumped_statements);
}

static int
vy_task_compact_complete(struct vy_task *task)
{
	struct vy_index *index = task->index;
	struct vy_range *range = task->range;
	struct vy_run *new_run = task->new_run;
	struct vy_slice *first_slice = task->first_slice;
	struct vy_slice *last_slice = task->last_slice;
	struct vy_scheduler *scheduler = index->env->scheduler;
	struct vy_slice *slice, *next_slice, *new_slice = NULL;
	struct vy_run *run;

	/*
	 * Allocate a slice of the new run.
	 *
	 * If the run is empty, we don't need to allocate a new slice
	 * and insert it into the range, but we still need to delete
	 * compacted runs.
	 */
	if (!vy_run_is_empty(new_run)) {
		new_slice = vy_slice_new(vy_log_next_id(), new_run, NULL, NULL,
					 index->key_def);
		if (new_slice == NULL)
			return -1;
	}

	/*
	 * Build the list of runs that became unused
	 * as a result of compaction.
	 */
	RLIST_HEAD(unused_runs);
	for (slice = first_slice; ; slice = rlist_next_entry(slice, in_range)) {
		slice->run->compacted_slice_count++;
		if (slice == last_slice)
			break;
	}
	for (slice = first_slice; ; slice = rlist_next_entry(slice, in_range)) {
		run = slice->run;
		if (run->compacted_slice_count == run->refs)
			rlist_add_entry(&unused_runs, run, in_unused);
		slice->run->compacted_slice_count = 0;
		if (slice == last_slice)
			break;
	}

	/*
	 * Log change in metadata.
	 */
	vy_log_tx_begin();
	for (slice = first_slice; ; slice = rlist_next_entry(slice, in_range)) {
		vy_log_delete_slice(slice->id);
		if (slice == last_slice)
			break;
	}
	int64_t gc_lsn = vclock_sum(&scheduler->last_checkpoint);
	rlist_foreach_entry(run, &unused_runs, in_unused)
		vy_log_drop_run(run->id, gc_lsn);
	if (new_slice != NULL) {
		vy_log_create_run(index->opts.lsn, new_run->id,
				  new_run->dump_lsn);
		vy_log_insert_slice(range->id, new_run->id, new_slice->id,
				    tuple_data_or_null(new_slice->begin),
				    tuple_data_or_null(new_slice->end));
	}
	if (vy_log_tx_commit() < 0) {
		if (new_slice != NULL)
			vy_slice_delete(new_slice);
		return -1;
	}

	/*
	 * Account the new run if it is not empty,
	 * otherwise discard it.
	 */
	if (new_slice != NULL) {
		vy_index_add_run(index, new_run);
		/* Drop the reference held by the task. */
		vy_run_unref(new_run);
	} else
		vy_run_discard(new_run);

	/*
	 * Replace compacted slices with the resulting slice.
	 *
	 * Note, since a slice might have been added to the range
	 * by a concurrent dump while compaction was in progress,
	 * we must insert the new slice at the same position where
	 * the compacted slices were.
	 */
	RLIST_HEAD(compacted_slices);
	vy_index_unacct_range(index, range);
	if (new_slice != NULL)
		vy_range_add_slice_before(range, new_slice, first_slice);
	for (slice = first_slice; ; slice = next_slice) {
		next_slice = rlist_next_entry(slice, in_range);
		vy_range_remove_slice(range, slice);
		rlist_add_entry(&compacted_slices, slice, in_range);
		if (slice == last_slice)
			break;
	}
	range->n_compactions++;
	range->version++;
	vy_index_acct_range(index, range);
	vy_range_update_compact_priority(range);

	/*
	 * Unaccount unused runs and delete compacted slices.
	 */
	rlist_foreach_entry(run, &unused_runs, in_unused)
		vy_index_remove_run(index, run);
	rlist_foreach_entry_safe(slice, &compacted_slices,
				 in_range, next_slice) {
		vy_slice_wait_pinned(slice);
		vy_slice_delete(slice);
	}

	/* The iterator has been cleaned up in worker. */
	task->wi->iface->close(task->wi);

	vy_scheduler_add_range(scheduler, range);

	say_info("%s: completed compacting range %s",
		 vy_index_name(index), vy_range_str(range));
	return 0;
}

static void
vy_task_compact_abort(struct vy_task *task, bool in_shutdown)
{
	struct vy_index *index = task->index;
	struct vy_range *range = task->range;
	struct vy_scheduler *scheduler = index->env->scheduler;

	/* The iterator has been cleaned up in worker. */
	task->wi->iface->close(task->wi);

	if (!in_shutdown && !index->is_dropped) {
		say_error("%s: failed to compact range %s: %s",
			  vy_index_name(index), vy_range_str(range),
			  diag_last_error(&task->diag)->errmsg);
		vy_run_discard(task->new_run);
	} else
		vy_run_unref(task->new_run);

	vy_scheduler_add_range(scheduler, range);
}

static int
vy_task_compact_new(struct vy_range *range, struct vy_task **p_task)
{
	assert(range->compact_priority > 1);

	static struct vy_task_ops compact_ops = {
		.execute = vy_task_compact_execute,
		.complete = vy_task_compact_complete,
		.abort = vy_task_compact_abort,
	};

	struct vy_index *index = range->index;
	struct tx_manager *xm = index->env->xm;
	struct vy_scheduler *scheduler = index->env->scheduler;

	if (index->is_dropped) {
		vy_scheduler_remove_range(scheduler, range);
		return 0;
	}

	if (vy_range_maybe_split(range))
		return 0;

	if (vy_range_maybe_coalesce(range))
		return 0;

	struct vy_task *task = vy_task_new(&scheduler->task_pool,
					   index, &compact_ops);
	if (task == NULL)
		goto err_task;

	struct vy_run *new_run = vy_run_prepare(index);
	if (new_run == NULL)
		goto err_run;

	struct vy_stmt_stream *wi;
	bool is_last_level = (range->compact_priority == range->slice_count);
	wi = vy_write_iterator_new(index->key_def, index->surrogate_format,
				   index->upsert_format, index->id == 0,
				   is_last_level, tx_manager_vlsn(xm));
	if (wi == NULL)
		goto err_wi;

	struct vy_slice *slice;
	int n = range->compact_priority;
	rlist_foreach_entry(slice, &range->slices, in_range) {
		if (vy_write_iterator_add_slice(wi, slice,
						&index->env->run_env) != 0)
			goto err_wi_sub;

		task->max_output_count += slice->count.rows;
		new_run->dump_lsn = MAX(new_run->dump_lsn,
					slice->run->dump_lsn);

		/* Remember the slices we are compacting. */
		if (task->first_slice == NULL)
			task->first_slice = slice;
		task->last_slice = slice;

		if (--n == 0)
			break;
	}
	assert(n == 0);
	assert(new_run->dump_lsn >= 0);

	task->range = range;
	task->new_run = new_run;
	task->wi = wi;
	task->bloom_fpr = index->opts.bloom_fpr;
	task->page_size = index->opts.page_size;

	vy_scheduler_remove_range(scheduler, range);

	say_info("%s: started compacting range %s, runs %d/%d",
		 vy_index_name(index), vy_range_str(range),
                 range->compact_priority, range->slice_count);
	*p_task = task;
	return 0;

err_wi_sub:
	task->wi->iface->close(wi);
err_wi:
	vy_run_discard(new_run);
err_run:
	vy_task_delete(&scheduler->task_pool, task);
err_task:
	say_error("%s: could not start compacting range %s: %s",
		  vy_index_name(index), vy_range_str(range),
		  diag_last_error(diag_get())->errmsg);
	return -1;
}

/* Scheduler Task }}} */

/* {{{ Scheduler */

/* Min and max values for vy_scheduler->timeout. */
#define VY_SCHEDULER_TIMEOUT_MIN		1
#define VY_SCHEDULER_TIMEOUT_MAX		60

static void
vy_scheduler_start_workers(struct vy_scheduler *scheduler);
static void
vy_scheduler_stop_workers(struct vy_scheduler *scheduler);
static int
vy_scheduler_f(va_list va);

static void
vy_scheduler_quota_exceeded_cb(struct vy_quota *quota)
{
	struct vy_env *env = container_of(quota, struct vy_env, quota);
	ipc_cond_signal(&env->scheduler->scheduler_cond);
}

static ev_tstamp
vy_scheduler_quota_throttled_cb(struct vy_quota *quota, ev_tstamp timeout)
{
	struct vy_env *env = container_of(quota, struct vy_env, quota);
	ev_tstamp wait_start = ev_now(loop());
	if (ipc_cond_wait_timeout(&env->scheduler->quota_cond, timeout) != 0)
		return 0; /* timed out */
	ev_tstamp wait_end = ev_now(loop());
	return timeout - (wait_end - wait_start);
}

static void
vy_scheduler_quota_released_cb(struct vy_quota *quota)
{
	struct vy_env *env = container_of(quota, struct vy_env, quota);
	ipc_cond_broadcast(&env->scheduler->quota_cond);
}

static void
vy_scheduler_async_cb(ev_loop *loop, struct ev_async *watcher, int events)
{
	(void) loop;
	(void) events;
	struct vy_scheduler *scheduler =
		container_of(watcher, struct vy_scheduler, scheduler_async);
	ipc_cond_signal(&scheduler->scheduler_cond);
}

static struct vy_scheduler *
vy_scheduler_new(struct vy_env *env)
{
	struct vy_scheduler *scheduler = calloc(1, sizeof(*scheduler));
	if (scheduler == NULL) {
		diag_set(OutOfMemory, sizeof(*scheduler), "scheduler",
			 "struct");
		return NULL;
	}
	tt_pthread_mutex_init(&scheduler->mutex, NULL);
	diag_create(&scheduler->diag);
	rlist_create(&scheduler->dump_fifo);
	ipc_cond_create(&scheduler->dump_cond);
	vclock_create(&scheduler->last_checkpoint);
	scheduler->env = env;
	vy_compact_heap_create(&scheduler->compact_heap);
	vy_dump_heap_create(&scheduler->dump_heap);
	tt_pthread_cond_init(&scheduler->worker_cond, NULL);
	scheduler->loop = loop();
	ev_async_init(&scheduler->scheduler_async, vy_scheduler_async_cb);
	ipc_cond_create(&scheduler->scheduler_cond);
	ipc_cond_create(&scheduler->quota_cond);
	mempool_create(&scheduler->task_pool, cord_slab_cache(),
			sizeof(struct vy_task));
	/* Start scheduler fiber. */
	scheduler->scheduler = fiber_new("vinyl.scheduler", vy_scheduler_f);
	if (scheduler->scheduler == NULL)
		panic("failed to start vinyl scheduler fiber");
	fiber_start(scheduler->scheduler, scheduler);
	return scheduler;
}

static void
vy_scheduler_delete(struct vy_scheduler *scheduler)
{
	/* Stop scheduler fiber. */
	scheduler->scheduler = NULL;
	/* Sic: fiber_cancel() can't be used here. */
	ipc_cond_signal(&scheduler->scheduler_cond);

	if (scheduler->is_worker_pool_running)
		vy_scheduler_stop_workers(scheduler);

	mempool_destroy(&scheduler->task_pool);
	diag_destroy(&scheduler->diag);
	vy_compact_heap_destroy(&scheduler->compact_heap);
	vy_dump_heap_destroy(&scheduler->dump_heap);
	tt_pthread_cond_destroy(&scheduler->worker_cond);
	TRASH(&scheduler->scheduler_async);
	ipc_cond_destroy(&scheduler->scheduler_cond);
	ipc_cond_destroy(&scheduler->quota_cond);
	tt_pthread_mutex_destroy(&scheduler->mutex);
	free(scheduler);
}

static void
vy_scheduler_add_index(struct vy_scheduler *scheduler,
		       struct vy_index *index)
{
	vy_dump_heap_insert(&scheduler->dump_heap, &index->in_dump);
	assert(index->in_dump.pos != UINT32_MAX);
}

static void
vy_scheduler_update_index(struct vy_scheduler *scheduler,
			  struct vy_index *index)
{
	vy_dump_heap_update(&scheduler->dump_heap, &index->in_dump);
	assert(index->in_dump.pos != UINT32_MAX);
}

static void
vy_scheduler_remove_index(struct vy_scheduler *scheduler,
			  struct vy_index *index)
{
	vy_dump_heap_delete(&scheduler->dump_heap, &index->in_dump);
	index->in_dump.pos = UINT32_MAX;
}

static void
vy_scheduler_pin_index(struct vy_scheduler *scheduler, struct vy_index *index)
{
	if (index->pin_count == 0)
		vy_scheduler_remove_index(scheduler, index);
	index->pin_count++;
}

static void
vy_scheduler_unpin_index(struct vy_scheduler *scheduler, struct vy_index *index)
{
	assert(index->pin_count > 0);
	index->pin_count--;
	if (index->pin_count == 0)
		vy_scheduler_add_index(scheduler, index);
}

static void
vy_scheduler_add_range(struct vy_scheduler *scheduler,
		       struct vy_range *range)
{
	vy_compact_heap_insert(&scheduler->compact_heap, &range->in_compact);
	assert(range->in_compact.pos != UINT32_MAX);
}

static void
vy_scheduler_update_range(struct vy_scheduler *scheduler,
			  struct vy_range *range)
{
	vy_compact_heap_update(&scheduler->compact_heap, &range->in_compact);
	assert(range->in_compact.pos != UINT32_MAX);
}

static void
vy_scheduler_remove_range(struct vy_scheduler *scheduler,
			  struct vy_range *range)
{
	vy_compact_heap_delete(&scheduler->compact_heap, &range->in_compact);
	range->in_compact.pos = UINT32_MAX;
}

/**
 * Create a task for dumping an index. The new task is returned
 * in @ptask. If there's no index that needs to be dumped @ptask
 * is set to NULL.
 *
 * We only dump an index if it needs to be snapshotted or the quota
 * on memory usage is exceeded. In either case, the oldest index
 * is selected, because dumping it will free the maximal amount of
 * memory due to log structured design of the memory allocator.
 *
 * Returns 0 on success, -1 on failure.
 */
static int
vy_scheduler_peek_dump(struct vy_scheduler *scheduler, struct vy_task **ptask)
{
retry:
	*ptask = NULL;
	if (!vy_scheduler_needs_dump(scheduler))
		return 0;
	struct heap_node *pn = vy_dump_heap_top(&scheduler->dump_heap);
	if (pn == NULL)
		return 0; /* nothing to do */
	struct vy_index *index = container_of(pn, struct vy_index, in_dump);
	if (index->generation == scheduler->generation)
		return 0; /* nothing to do */
	if (vy_task_dump_new(index, ptask) != 0)
		return -1;
	if (*ptask == NULL)
		goto retry; /* index dropped or all mems empty */
	return 0; /* new task */
}

/**
 * Create a task for compacting a range. The new task is returned
 * in @ptask. If there's no range that needs to be compacted @ptask
 * is set to NULL.
 *
 * We compact ranges that have more runs in a level than specified
 * by run_count_per_level configuration option. Among those runs we
 * give preference to those ranges whose compaction will reduce
 * read amplification most.
 *
 * Returns 0 on success, -1 on failure.
 */
static int
vy_scheduler_peek_compact(struct vy_scheduler *scheduler,
			  struct vy_task **ptask)
{
retry:
	*ptask = NULL;
	struct heap_node *pn = vy_compact_heap_top(&scheduler->compact_heap);
	if (pn == NULL)
		return 0; /* nothing to do */
	struct vy_range *range = container_of(pn, struct vy_range, in_compact);
	if (range->compact_priority <= 1)
		return 0; /* nothing to do */
	if (vy_task_compact_new(range, ptask) != 0)
		return -1;
	if (*ptask == NULL)
		goto retry; /* index dropped or range split/coalesced */
	return 0; /* new task */
}

static int
vy_schedule(struct vy_scheduler *scheduler, struct vy_task **ptask)
{
	*ptask = NULL;

	if (vy_scheduler_peek_dump(scheduler, ptask) != 0)
		goto fail;
	if (*ptask != NULL)
		return 0;

	if (scheduler->workers_available <= 1) {
		/*
		 * If all worker threads are busy doing compaction
		 * when we run out of quota, ongoing transactions will
		 * hang until one of the threads has finished, which
		 * may take quite a while. To avoid unpredictably long
		 * stalls, always keep one worker thread reserved for
		 * dumps.
		 */
		return 0;
	}

	if (vy_scheduler_peek_compact(scheduler, ptask) != 0)
		goto fail;
	if (*ptask != NULL)
		return 0;

	/* no task to run */
	return 0;
fail:
	assert(!diag_is_empty(diag_get()));
	diag_move(diag_get(), &scheduler->diag);
	return -1;

}

static int
vy_scheduler_complete_task(struct vy_scheduler *scheduler,
			   struct vy_task *task)
{
	if (task->index->is_dropped) {
		if (task->ops->abort)
			task->ops->abort(task, false);
		return 0;
	}

	struct diag *diag = &task->diag;
	if (task->status != 0) {
		assert(!diag_is_empty(diag));
		goto fail; /* ->execute fialed */
	}
	ERROR_INJECT(ERRINJ_VY_TASK_COMPLETE, {
			diag_set(ClientError, ER_INJECTION,
			       "vinyl task completion");
			diag_move(diag_get(), diag);
			goto fail; });
	if (task->ops->complete &&
	    task->ops->complete(task) != 0) {
		assert(!diag_is_empty(diag_get()));
		diag_move(diag_get(), diag);
		goto fail;
	}
	return 0;
fail:
	if (task->ops->abort)
		task->ops->abort(task, false);
	diag_move(diag, &scheduler->diag);
	return -1;
}

static int
vy_scheduler_f(va_list va)
{
	struct vy_scheduler *scheduler = va_arg(va, struct vy_scheduler *);
	struct vy_env *env = scheduler->env;

	/*
	 * Yield immediately, until the quota watermark is reached
	 * for the first time or a checkpoint is made.
	 * Then start the worker threads: we know they will be
	 * needed. If quota watermark is never reached, workers
	 * are not started and the scheduler is idle until
	 * shutdown or checkpoint.
	 */
	ipc_cond_wait(&scheduler->scheduler_cond);
	if (scheduler->scheduler == NULL)
		return 0; /* destroyed */

	/*
	 * The scheduler must be disabled during local recovery so as
	 * not to distort data stored on disk. Not that we really need
	 * it anyway, because the memory footprint is limited by the
	 * memory limit from the previous run.
	 *
	 * On the contrary, remote recovery does require the scheduler
	 * to be up and running, because the amount of data received
	 * when bootstrapping from a remote master is only limited by
	 * its disk size, which can exceed the size of available
	 * memory by orders of magnitude.
	 */
	assert(env->status != VINYL_INITIAL_RECOVERY_LOCAL &&
	       env->status != VINYL_FINAL_RECOVERY_LOCAL);

	vy_scheduler_start_workers(scheduler);

	while (scheduler->scheduler != NULL) {
		struct stailq output_queue;
		struct vy_task *task, *next;
		int tasks_failed = 0, tasks_done = 0;
		bool was_empty;

		/* Get the list of processed tasks. */
		stailq_create(&output_queue);
		tt_pthread_mutex_lock(&scheduler->mutex);
		stailq_concat(&output_queue, &scheduler->output_queue);
		tt_pthread_mutex_unlock(&scheduler->mutex);

		/* Complete and delete all processed tasks. */
		stailq_foreach_entry_safe(task, next, &output_queue, link) {
			if (vy_scheduler_complete_task(scheduler, task) != 0)
				tasks_failed++;
			else
				tasks_done++;
			if (task->dump_size > 0)
				vy_stat_dump(env->stat, task->dump_size,
					     task->dumped_statements);
			vy_task_delete(&scheduler->task_pool, task);
			scheduler->workers_available++;
			assert(scheduler->workers_available <=
			       scheduler->worker_pool_size);
		}
		/*
		 * Reset the timeout if we managed to successfully
		 * complete at least one task.
		 */
		if (tasks_done > 0) {
			scheduler->timeout = 0;
			/*
			 * Task completion callback may yield, which
			 * opens a time window for a worker to submit
			 * a processed task and wake up the scheduler
			 * (via scheduler_async). Hence we should go
			 * and recheck the output_queue in order not
			 * to lose a wakeup event and hang for good.
			 */
			continue;
		}
		/* Throttle for a while if a task failed. */
		if (tasks_failed > 0)
			goto error;
		/* All worker threads are busy. */
		if (scheduler->workers_available == 0)
			goto wait;
		/* Get a task to schedule. */
		if (vy_schedule(scheduler, &task) != 0)
			goto error;
		/* Nothing to do. */
		if (task == NULL)
			goto wait;

		/* Queue the task and notify workers if necessary. */
		tt_pthread_mutex_lock(&scheduler->mutex);
		was_empty = stailq_empty(&scheduler->input_queue);
		stailq_add_tail_entry(&scheduler->input_queue, task, link);
		if (was_empty)
			tt_pthread_cond_signal(&scheduler->worker_cond);
		tt_pthread_mutex_unlock(&scheduler->mutex);

		scheduler->workers_available--;
		fiber_reschedule();
		continue;
error:
		/* Abort pending checkpoint. */
		ipc_cond_signal(&scheduler->dump_cond);
		/*
		 * A task can fail either due to lack of memory or IO
		 * error. In either case it is pointless to schedule
		 * another task right away, because it is likely to fail
		 * too. So we throttle the scheduler for a while after
		 * each failure.
		 */
		scheduler->timeout *= 2;
		if (scheduler->timeout < VY_SCHEDULER_TIMEOUT_MIN)
			scheduler->timeout = VY_SCHEDULER_TIMEOUT_MIN;
		if (scheduler->timeout > VY_SCHEDULER_TIMEOUT_MAX)
			scheduler->timeout = VY_SCHEDULER_TIMEOUT_MAX;
		struct errinj *inj;
		inj = errinj(ERRINJ_VY_SCHED_TIMEOUT, ERRINJ_DOUBLE);
		if (inj != NULL && inj->dparam != 0)
			scheduler->timeout = inj->dparam;
		say_warn("throttling scheduler for %.0f second(s)",
			 scheduler->timeout);
		scheduler->is_throttled = true;
		fiber_sleep(scheduler->timeout);
		scheduler->is_throttled = false;
		continue;
wait:
		/* Wait for changes */
		ipc_cond_wait(&scheduler->scheduler_cond);
	}

	return 0;
}

static int
vy_worker_f(va_list va)
{
	struct vy_scheduler *scheduler = va_arg(va, struct vy_scheduler *);
	coio_enable();
	struct vy_task *task = NULL;

	tt_pthread_mutex_lock(&scheduler->mutex);
	while (scheduler->is_worker_pool_running) {
		/* Wait for a task */
		if (stailq_empty(&scheduler->input_queue)) {
			/* Wake scheduler up if there are no more tasks */
			ev_async_send(scheduler->loop,
				      &scheduler->scheduler_async);
			tt_pthread_cond_wait(&scheduler->worker_cond,
					     &scheduler->mutex);
			continue;
		}
		task = stailq_shift_entry(&scheduler->input_queue,
					  struct vy_task, link);
		tt_pthread_mutex_unlock(&scheduler->mutex);
		assert(task != NULL);

		/* Execute task */
		task->status = task->ops->execute(task);
		if (task->status != 0) {
			struct diag *diag = diag_get();
			assert(!diag_is_empty(diag));
			diag_move(diag, &task->diag);
		}

		/* Return processed task to scheduler */
		tt_pthread_mutex_lock(&scheduler->mutex);
		stailq_add_tail_entry(&scheduler->output_queue, task, link);
	}
	tt_pthread_mutex_unlock(&scheduler->mutex);
	return 0;
}

static void
vy_scheduler_start_workers(struct vy_scheduler *scheduler)
{
	assert(!scheduler->is_worker_pool_running);

	/* Start worker threads */
	scheduler->is_worker_pool_running = true;
	scheduler->worker_pool_size = cfg_geti("vinyl_write_threads");
	/* One thread is reserved for dumps, see vy_schedule(). */
	assert(scheduler->worker_pool_size >= 2);
	scheduler->workers_available = scheduler->worker_pool_size;
	stailq_create(&scheduler->input_queue);
	stailq_create(&scheduler->output_queue);
	scheduler->worker_pool = (struct cord *)
		calloc(scheduler->worker_pool_size, sizeof(struct cord));
	if (scheduler->worker_pool == NULL)
		panic("failed to allocate vinyl worker pool");
	ev_async_start(scheduler->loop, &scheduler->scheduler_async);
	for (int i = 0; i < scheduler->worker_pool_size; i++) {
		cord_costart(&scheduler->worker_pool[i], "vinyl.worker",
			     vy_worker_f, scheduler);
	}
}

static void
vy_scheduler_stop_workers(struct vy_scheduler *scheduler)
{
	struct stailq task_queue;
	stailq_create(&task_queue);

	assert(scheduler->is_worker_pool_running);
	scheduler->is_worker_pool_running = false;

	/* Clear the input queue and wake up worker threads. */
	tt_pthread_mutex_lock(&scheduler->mutex);
	stailq_concat(&task_queue, &scheduler->input_queue);
	pthread_cond_broadcast(&scheduler->worker_cond);
	tt_pthread_mutex_unlock(&scheduler->mutex);

	/* Wait for worker threads to exit. */
	for (int i = 0; i < scheduler->worker_pool_size; i++)
		cord_join(&scheduler->worker_pool[i]);
	ev_async_stop(scheduler->loop, &scheduler->scheduler_async);
	free(scheduler->worker_pool);
	scheduler->worker_pool = NULL;
	scheduler->worker_pool_size = 0;

	/* Abort all pending tasks. */
	struct vy_task *task, *next;
	stailq_concat(&task_queue, &scheduler->output_queue);
	stailq_foreach_entry_safe(task, next, &task_queue, link) {
		if (task->ops->abort != NULL)
			task->ops->abort(task, true);
		vy_task_delete(&scheduler->task_pool, task);
	}
}

/**
 * Return true if there are in-memory trees that need to
 * be dumped (are older than the current generation).
 */
static bool
vy_scheduler_dump_in_progress(struct vy_scheduler *scheduler)
{
	if (rlist_empty(&scheduler->dump_fifo))
		return false;

	struct vy_mem *mem = rlist_last_entry(&scheduler->dump_fifo,
					      struct vy_mem, in_dump_fifo);
	if (mem->generation == scheduler->generation)
		return false;

	assert(mem->generation < scheduler->generation);
	return true;

}

/** Called to trigger memory dump. */
static void
vy_scheduler_trigger_dump(struct vy_scheduler *scheduler)
{
	/*
	 * Increment the generation to trigger dump of
	 * all in-memory trees.
	 */
	scheduler->generation++;
	scheduler->dump_start = ev_now(loop());
}

/** Called on memory dump completion. */
static void
vy_scheduler_complete_dump(struct vy_scheduler *scheduler)
{
	/*
	 * All old in-memory trees have been dumped.
	 * Free memory, release quota, and signal
	 * dump completion.
	 */
	struct lsregion *allocator = &scheduler->env->allocator;
	struct vy_quota *quota = &scheduler->env->quota;
	size_t mem_used_before = lsregion_used(allocator);
	lsregion_gc(allocator, scheduler->generation - 1);
	size_t mem_used_after = lsregion_used(allocator);
	assert(mem_used_after <= mem_used_before);
	size_t mem_dumped = mem_used_before - mem_used_after;
	vy_quota_release(quota, mem_dumped);
	ipc_cond_signal(&scheduler->dump_cond);

	/* Account dump bandwidth. */
	struct vy_stat *stat = scheduler->env->stat;
	ev_tstamp dump_duration = ev_now(loop()) - scheduler->dump_start;
	if (dump_duration > 0)
		histogram_collect(stat->dump_bw, mem_dumped / dump_duration);
}

/** Check if memory dump is required. */
static bool
vy_scheduler_needs_dump(struct vy_scheduler *scheduler)
{
	if (vy_scheduler_dump_in_progress(scheduler)) {
		/*
		 * There are old in-memory trees to be dumped.
		 * Do not increase the generation until all of
		 * them are dumped to guarantee dump consistency.
		 */
		return true;
	}

	if (scheduler->checkpoint_in_progress) {
		/*
		 * If checkpoint is in progress, force dumping
		 * all in-memory data that need to be included
		 * in the snapshot.
		 */
		if (scheduler->generation < scheduler->checkpoint_generation)
			goto trigger_dump;
		/*
		 * Do not trigger another dump until checkpoint
		 * is complete so as to make sure no statements
		 * inserted after WAL rotation are written to
		 * the snapshot.
		 */
		return false;
	}

	if (!vy_quota_is_exceeded(&scheduler->env->quota)) {
		/*
		 * Memory consumption is below the watermark,
		 * nothing to do.
		 */
		return false;
	}

	if (lsregion_used(&scheduler->env->allocator) == 0) {
		/*
		 * Quota must be exceeded by a pending transaction,
		 * there's nothing we can do about that.
		 */
		return false;
	}

trigger_dump:
	vy_scheduler_trigger_dump(scheduler);
	return true;
}

static void
vy_scheduler_add_mem(struct vy_scheduler *scheduler, struct vy_mem *mem)
{
	assert(mem->generation <= scheduler->generation);
	assert(rlist_empty(&mem->in_dump_fifo));
	rlist_add_entry(&scheduler->dump_fifo, mem, in_dump_fifo);
}

static void
vy_scheduler_remove_mem(struct vy_scheduler *scheduler, struct vy_mem *mem)
{
	assert(mem->generation <= scheduler->generation);
	assert(!rlist_empty(&mem->in_dump_fifo));
	rlist_del_entry(mem, in_dump_fifo);

	if (mem->generation < scheduler->generation &&
	    !vy_scheduler_dump_in_progress(scheduler)) {
		/*
		 * The last in-memory tree left from the previous
		 * generation has just been deleted, complete dump.
		 */
		vy_scheduler_complete_dump(scheduler);
	}
}

/*
 * Schedule checkpoint. Please call vy_wait_checkpoint() after that.
 */
int
vy_begin_checkpoint(struct vy_env *env)
{
	struct vy_scheduler *scheduler = env->scheduler;

	assert(env->status == VINYL_ONLINE);
	assert(!scheduler->checkpoint_in_progress);

	/*
	 * If the scheduler is throttled due to errors, do not wait
	 * until it wakes up as it may take quite a while. Instead
	 * fail checkpoint immediately with the last error seen by
	 * the scheduler.
	 */
	if (scheduler->is_throttled) {
		assert(!diag_is_empty(&scheduler->diag));
		diag_add_error(diag_get(), diag_last_error(&scheduler->diag));
		say_error("Can't checkpoint, scheduler is throttled with: %s",
			  diag_last_error(diag_get())->errmsg);
		return -1;
	}

	scheduler->checkpoint_in_progress = true;
	scheduler->checkpoint_generation = scheduler->generation + 1;
	ipc_cond_signal(&scheduler->scheduler_cond);
	return 0;
}

/*
 * Wait for checkpoint. Please call vy_end_checkpoint() after that.
 */
int
vy_wait_checkpoint(struct vy_env *env, struct vclock *vclock)
{
	struct vy_scheduler *scheduler = env->scheduler;

	assert(scheduler->checkpoint_in_progress);

	/*
	 * Wait until all in-memory trees whose generation is
	 * less than checkpoint_generation have been dumped.
	 */
	while (scheduler->generation < scheduler->checkpoint_generation ||
	       vy_scheduler_dump_in_progress(scheduler)) {
		if (scheduler->is_throttled) {
			/* A dump error occurred, abort checkpoint. */
			assert(!diag_is_empty(&scheduler->diag));
			diag_add_error(diag_get(),
				       diag_last_error(&scheduler->diag));
			goto error;
		}
		ipc_cond_wait(&scheduler->dump_cond);
	}

	if (vy_log_rotate(vclock) != 0)
		goto error;

	vclock_copy(&scheduler->last_checkpoint, vclock);

	say_info("vinyl checkpoint done");
	return 0;
error:
	say_error("vinyl checkpoint error: %s",
		  diag_last_error(diag_get())->errmsg);
	return -1;
}

/*
 * End checkpoint. Called on both checkpoint commit and abort.
 */
void
vy_end_checkpoint(struct vy_env *env)
{
	struct vy_scheduler *scheduler = env->scheduler;

	/*
	 * Checkpoint blocks dumping of in-memory trees created after
	 * checkpoint started, so wake up the scheduler after we are
	 * done so that it can catch up.
	 */
	scheduler->checkpoint_in_progress = false;
	ipc_cond_signal(&scheduler->scheduler_cond);
}

/* Scheduler }}} */

/* {{{ Configuration */

static struct vy_conf *
vy_conf_new()
{
	struct vy_conf *conf = calloc(1, sizeof(*conf));
	if (conf == NULL) {
		diag_set(OutOfMemory, sizeof(*conf), "conf", "struct");
		return NULL;
	}
	conf->memory_limit = cfg_getd("vinyl_memory");
	conf->cache = cfg_getd("vinyl_cache");
	conf->timeout = cfg_getd("vinyl_timeout");

	conf->path = strdup(cfg_gets("vinyl_dir"));
	if (conf->path == NULL) {
		diag_set(OutOfMemory, sizeof(*conf), "conf", "path");
		goto error_1;
	}
	/* Ensure vinyl data directory exists. */
	if (!path_exists(conf->path)) {
		diag_set(ClientError, ER_CFG, "vinyl_dir",
			 "directory does not exist");
		goto error_2;
	}
	return conf;

error_2:
	free(conf->path);
error_1:
	free(conf);
	return NULL;
}

static void vy_conf_delete(struct vy_conf *c)
{
	free(c->path);
	free(c);
}

int
vy_update_options(struct vy_env *env)
{
	struct vy_conf *conf = env->conf;
	conf->timeout = cfg_getd("vinyl_timeout");
	return 0;
}

/* Configuration }}} */

/** {{{ Introspection */

static void
vy_info_append_memory(struct vy_env *env, struct info_handler *h)
{
	char buf[16];
	struct vy_quota *q = &env->quota;
	info_table_begin(h, "memory");
	info_append_int(h, "used", q->used);
	info_append_int(h, "limit", q->limit);
	info_append_int(h, "watermark", q->watermark);
	snprintf(buf, sizeof(buf), "%d%%", (int)(100 * q->used / q->limit));
	info_append_str(h, "ratio", buf);
	info_table_end(h);
}

static int
vy_info_append_stat_rmean(const char *name, int rps, int64_t total, void *ctx)
{
	struct info_handler *h = ctx;
	info_table_begin(h, name);
	info_append_int(h, "rps", rps);
	info_append_int(h, "total", total);
	info_table_end(h);
	return 0;
}

static void
vy_info_append_stat_latency(struct info_handler *h,
			    const char *name, struct vy_latency *lat)
{
	info_table_begin(h, name);
	info_append_int(h, "max", lat->max * 1000000000);
	info_append_int(h, "avg", lat->count == 0 ? 0 :
			   lat->total / lat->count * 1000000000);
	info_table_end(h);
}

static void
vy_info_append_iterator_stat(struct info_handler *h, const char *name,
			     struct vy_iterator_stat *stat)
{
	info_table_begin(h, name);
	info_append_int(h, "lookup_count", stat->lookup_count);
	info_append_int(h, "step_count", stat->step_count);
	info_append_int(h, "bloom_reflect_count", stat->bloom_reflections);
	info_table_end(h);
}

static void
vy_info_append_performance(struct vy_env *env, struct info_handler *h)
{
	struct vy_stat *stat = env->stat;

	info_table_begin(h, "performance");

	rmean_foreach(stat->rmean, vy_info_append_stat_rmean, h);

	info_append_int(h, "write_count", stat->write_count);

	vy_info_append_stat_latency(h, "tx_latency", &stat->tx_latency);
	vy_info_append_stat_latency(h, "get_latency", &stat->get_latency);
	vy_info_append_stat_latency(h, "cursor_latency", &stat->cursor_latency);

	info_append_int(h, "tx_rollback", stat->tx_rlb);
	info_append_int(h, "tx_conflict", stat->tx_conflict);
	info_append_int(h, "tx_active", env->xm->tx_count);

	struct mempool_stats mstats;
	mempool_stats(&env->xm->tx_mempool, &mstats);
	info_append_int(h, "tx_allocated", mstats.objcount);
	mempool_stats(&env->xm->txv_mempool, &mstats);
	info_append_int(h, "txv_allocated", mstats.objcount);
	mempool_stats(&env->xm->read_view_mempool, &mstats);
	info_append_int(h, "read_view", mstats.objcount);

	info_append_int(h, "dump_bandwidth", vy_stat_dump_bandwidth(stat));
	info_append_int(h, "dump_total", stat->dump_total);
	info_append_int(h, "dumped_statements", stat->dumped_statements);

	struct vy_cache_env *ce = &env->cache_env;
	info_table_begin(h, "cache");
	info_append_int(h, "count", ce->cached_count);
	info_append_int(h, "used", ce->quota.used);
	info_table_end(h);

	info_table_begin(h, "iterator");
	vy_info_append_iterator_stat(h, "txw", &stat->txw_stat);
	vy_info_append_iterator_stat(h, "cache", &stat->cache_stat);
	vy_info_append_iterator_stat(h, "mem", &stat->mem_stat);
	vy_info_append_iterator_stat(h, "run", &stat->run_stat);
	info_table_end(h);

	info_table_end(h);
}

void
vy_info(struct vy_env *env, struct info_handler *h)
{
	info_begin(h);
	vy_info_append_memory(env, h);
	vy_info_append_performance(env, h);
	info_append_int(h, "lsn", env->xm->lsn);
	info_end(h);
}

static void
vy_info_append_stmt_counter(struct info_handler *h, const char *name,
			    const struct vy_stmt_counter *count)
{
	info_table_begin(h, name);
	info_append_int(h, "rows", count->rows);
	info_append_int(h, "bytes", count->bytes);
	info_table_end(h);
}

static void
vy_info_append_disk_stmt_counter(struct info_handler *h, const char *name,
				 const struct vy_disk_stmt_counter *count)
{
	info_table_begin(h, name);
	info_append_int(h, "rows", count->rows);
	info_append_int(h, "bytes", count->bytes);
	info_append_int(h, "bytes_compressed", count->bytes_compressed);
	info_append_int(h, "pages", count->pages);
	info_table_end(h);
}

void
vy_index_info(struct vy_index *index, struct info_handler *h)
{
	char buf[1024];
	struct vy_index_stat *stat = &index->stat;

	info_begin(h);
	info_append_int(h, "rows", stat->disk.count.rows +
				   stat->memory.count.rows);
	info_append_int(h, "bytes", stat->disk.count.bytes +
				    stat->memory.count.bytes);
	vy_info_append_stmt_counter(h, "memory", &stat->memory.count);
	vy_info_append_disk_stmt_counter(h, "disk", &stat->disk.count);
	info_append_int(h, "range_size", index->opts.range_size);
	info_append_int(h, "page_size", index->opts.page_size);
	info_append_int(h, "range_count", index->range_count);
	info_append_int(h, "run_count", index->run_count);
	info_append_int(h, "run_avg", index->run_count / index->range_count);
	histogram_snprint(buf, sizeof(buf), index->run_hist);
	info_append_str(h, "run_histogram", buf);
	info_append_double(h, "bloom_fpr", index->opts.bloom_fpr);
	info_end(h);
}

/** }}} Introspection */

/**
 * Detect whether we already have non-garbage index files,
 * and open an existing index if that's the case. Otherwise,
 * create a new index. Take the current recovery status into
 * account.
 */
int
vy_index_open(struct vy_index *index)
{
	switch (index->env->status) {
	case VINYL_ONLINE:
		/*
		 * The recovery is complete, simply
		 * create a new index.
		 */
		return vy_index_create(index);
	case VINYL_INITIAL_RECOVERY_REMOTE:
	case VINYL_FINAL_RECOVERY_REMOTE:
		/*
		 * Remote recovery. The index files do not
		 * exist locally, and we should create the
		 * index directory from scratch.
		 */
		return vy_index_create(index);
	case VINYL_INITIAL_RECOVERY_LOCAL:
	case VINYL_FINAL_RECOVERY_LOCAL:
		/*
		 * Local WAL replay or recovery from snapshot.
		 * In either case the index directory should
		 * have already been created, so try to load
		 * the index files from it.
		 */
		return vy_index_recover(index);
	default:
		unreachable();
		return -1;
	}
}

void
vy_index_ref(struct vy_index *index)
{
	assert(index->refs >= 0);
	index->refs++;
}

void
vy_index_unref(struct vy_index *index)
{
	assert(index->refs > 0);
	if (--index->refs == 0)
		vy_index_delete(index);
}

void
vy_index_commit_create(struct vy_index *index)
{
	struct vy_env *env = index->env;

	if (env->status == VINYL_INITIAL_RECOVERY_LOCAL ||
	    env->status == VINYL_FINAL_RECOVERY_LOCAL) {
		/*
		 * Normally, if this is local recovery, the index
		 * should have been logged before restart and added
		 * to the scheduler by vy_index_recover(). There's
		 * one exception though - we could've failed to log
		 * index due to a vylog write error, in which case
		 * the index isn't in the recovery context and we
		 * need to retry to log it now.
		 */
		if (vy_recovery_lookup_index(env->recovery,
					     index->opts.lsn) != NULL)
			return;
	}

	assert(index->range_count == 1);
	struct vy_range *range = vy_range_tree_first(index->tree);

	/*
	 * Since it's too late to fail now, in case of vylog write
	 * failure we leave the records we attempted to write in
	 * the log buffer so that they are flushed along with the
	 * next write request. If they don't get flushed before
	 * the instance is shut down, we will replay them on local
	 * recovery.
	 */
	vy_log_tx_begin();
	vy_log_create_index(index->opts.lsn, index->id,
			    index->space_id, index->user_key_def);
	vy_log_insert_range(index->opts.lsn, range->id, NULL, NULL);
	if (vy_log_tx_try_commit() != 0)
		say_warn("failed to log index creation: %s",
			 diag_last_error(diag_get())->errmsg);
	/*
	 * After we committed the index in the log, we can schedule
	 * a task for it.
	 */
	vy_scheduler_add_range(env->scheduler, range);
	vy_scheduler_add_index(env->scheduler, index);
}

/*
 * Delete all runs, ranges, and slices of a given index
 * from the metadata log.
 */
static void
vy_log_index_prune(struct vy_index *index)
{
	int loops = 0;
	for (struct vy_range *range = vy_range_tree_first(index->tree);
	     range != NULL; range = vy_range_tree_next(index->tree, range)) {
		struct vy_slice *slice;
		rlist_foreach_entry(slice, &range->slices, in_range)
			vy_log_delete_slice(slice->id);
		vy_log_delete_range(range->id);
		if (++loops % VY_YIELD_LOOPS == 0)
			fiber_sleep(0);
	}
	struct vy_run *run;
	int64_t gc_lsn = vclock_sum(&index->env->scheduler->last_checkpoint);
	rlist_foreach_entry(run, &index->runs, in_index) {
		vy_log_drop_run(run->id, gc_lsn);
		if (++loops % VY_YIELD_LOOPS == 0)
			fiber_sleep(0);
	}
}

void
vy_index_commit_drop(struct vy_index *index)
{
	struct vy_env *env = index->env;

	/*
	 * We can't abort here, because the index drop request has
	 * already been written to WAL. So if we fail to write the
	 * change to the metadata log, we leave it in the log buffer,
	 * to be flushed along with the next transaction. If it is
	 * not flushed before the instance is shut down, we replay it
	 * on local recovery from WAL.
	 */
	if (env->status == VINYL_FINAL_RECOVERY_LOCAL && index->is_dropped)
		return;

	index->is_dropped = true;

	vy_log_tx_begin();
	vy_log_index_prune(index);
	vy_log_drop_index(index->opts.lsn);
	if (vy_log_tx_try_commit() < 0)
		say_warn("failed to log drop index: %s",
			 diag_last_error(diag_get())->errmsg);
}

/**
 * Swap disk contents (ranges, runs, and corresponding stats)
 * between two indexes. Used only on recovery, to skip reloading
 * indexes of a truncated space. The in-memory tree of the index
 * can't be populated - see vy_is_committed_one().
 */
static void
vy_index_swap(struct vy_index *old_index, struct vy_index *new_index)
{
	assert(old_index->stat.memory.count.rows == 0);
	assert(new_index->stat.memory.count.rows == 0);

	SWAP(old_index->dump_lsn, new_index->dump_lsn);
	SWAP(old_index->range_count, new_index->range_count);
	SWAP(old_index->run_count, new_index->run_count);
	SWAP(old_index->stat, new_index->stat);
	SWAP(old_index->run_hist, new_index->run_hist);
	SWAP(old_index->tree, new_index->tree);
	rlist_swap(&old_index->runs, &new_index->runs);
}

int
vy_prepare_truncate_space(struct space *old_space, struct space *new_space)
{
	assert(old_space->index_count == new_space->index_count);
	uint32_t index_count = new_space->index_count;
	if (index_count == 0)
		return 0;

	struct vy_index *pk = vy_index(old_space->index[0]);
	struct vy_env *env = pk->env;

	/*
	 * On local recovery, we need to handle the following
	 * scenarios:
	 *
	 * - Space truncation was successfully logged before restart.
	 *   In this case indexes of the old space contain data added
	 *   after truncation (recovered by vy_index_recover()) and
	 *   hence we just need to swap contents between old and new
	 *   spaces.
	 *
	 * - We failed to log space truncation before restart.
	 *   In this case we have to replay space truncation the
	 *   same way we handle it during normal operation.
	 *
	 * See also vy_commit_truncate_space().
	 */
	bool truncate_done = (env->status == VINYL_FINAL_RECOVERY_LOCAL &&
			      pk->truncate_count > old_space->truncate_count);

	for (uint32_t i = 0; i < index_count; i++) {
		struct vy_index *old_index = vy_index(old_space->index[i]);
		struct vy_index *new_index = vy_index(new_space->index[i]);

		if (truncate_done) {
			/*
			 * We are replaying truncate from WAL and the
			 * old space already contains data added after
			 * truncate (recovered from vylog). Avoid
			 * reloading the space content from vylog,
			 * simply swap the contents of old and new
			 * spaces instead.
			 */
			vy_index_swap(old_index, new_index);
			new_index->is_dropped = old_index->is_dropped;
			new_index->truncate_count = old_index->truncate_count;
			vy_scheduler_add_index(env->scheduler, new_index);
			continue;
		}

		if (vy_index_create(new_index) != 0)
			return -1;

		new_index->truncate_count = new_space->truncate_count;
	}
	return 0;
}

void
vy_commit_truncate_space(struct space *old_space, struct space *new_space)
{
	assert(old_space->index_count == new_space->index_count);
	uint32_t index_count = new_space->index_count;
	if (index_count == 0)
		return;

	struct vy_index *pk = vy_index(old_space->index[0]);
	struct vy_env *env = pk->env;

	/*
	 * See the comment in vy_prepare_truncate_space().
	 */
	if (env->status == VINYL_FINAL_RECOVERY_LOCAL &&
	    pk->truncate_count > old_space->truncate_count)
		return;

	/*
	 * Mark old indexes as dropped. After this point no task can
	 * be scheduled or completed for any of them (only aborted).
	 */
	for (uint32_t i = 0; i < index_count; i++) {
		struct vy_index *index = vy_index(old_space->index[i]);
		index->is_dropped = true;
	}

	/*
	 * Log change in metadata.
	 *
	 * Since we can't fail here, in case of vylog write failure
	 * we leave records we failed to write in vylog buffer so
	 * that they get flushed along with the next write. If they
	 * don't, we will replay them during WAL recovery.
	 */
	vy_log_tx_begin();
	for (uint32_t i = 0; i < index_count; i++) {
		struct vy_index *old_index = vy_index(old_space->index[i]);
		struct vy_index *new_index = vy_index(new_space->index[i]);
		struct vy_range *range = vy_range_tree_first(new_index->tree);

		assert(!new_index->is_dropped);
		assert(new_index->truncate_count == new_space->truncate_count);
		assert(new_index->range_count == 1);

		vy_log_index_prune(old_index);
		vy_log_insert_range(new_index->opts.lsn, range->id, NULL, NULL);
		vy_log_truncate_index(new_index->opts.lsn,
				      new_index->truncate_count);
	}
	if (vy_log_tx_try_commit() < 0)
		say_warn("failed to log index truncation: %s",
			 diag_last_error(diag_get())->errmsg);

	/*
	 * After we committed space truncation in the metadata log,
	 * we can make new indexes eligible for dump and compaction.
	 */
	for (uint32_t i = 0; i < index_count; i++) {
		struct vy_index *index = vy_index(new_space->index[i]);
		struct vy_range *range = vy_range_tree_first(index->tree);

		vy_scheduler_add_range(env->scheduler, range);
		vy_scheduler_add_index(env->scheduler, index);
	}
}

extern struct tuple_format_vtab vy_tuple_format_vtab;

struct vy_index *
vy_index_new(struct vy_env *e, struct index_def *user_index_def,
	     struct space *space)
{
	assert(space != NULL);
	static int64_t run_buckets[] = {
		0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 15, 20, 25, 50, 100,
	};

	assert(user_index_def->key_def.part_count > 0);
	struct vy_index *pk = NULL;
	if (user_index_def->iid > 0) {
		pk = vy_index_find(space, 0);
		assert(pk != NULL);
	}

	struct vy_index *index = calloc(1, sizeof(struct vy_index));
	if (index == NULL) {
		diag_set(OutOfMemory, sizeof(struct vy_index),
			 "calloc", "struct vy_index");
		goto fail;
	}
	index->env = e;

	index->tree = malloc(sizeof(*index->tree));
	if (index->tree == NULL) {
		diag_set(OutOfMemory, sizeof(*index->tree),
			 "malloc", "vy_range_tree_t");
		goto fail_tree;
	}

	struct key_def *user_key_def = key_def_dup(&user_index_def->key_def);
	if (user_key_def == NULL)
		goto fail_user_key_def;

	struct key_def *key_def = NULL;
	if (user_index_def->iid == 0) {
		key_def = user_key_def;
	} else {
		key_def = key_def_merge(user_key_def, pk->key_def);
		if (key_def == NULL)
			goto fail_key_def;
	}
	index->key_def = key_def;
	index->user_key_def = user_key_def;
	index->surrogate_format = tuple_format_new(&vy_tuple_format_vtab,
						   &key_def, 1, 0);
	if (index->surrogate_format == NULL)
		goto fail_format;
	tuple_format_ref(index->surrogate_format, 1);

	if (user_index_def->iid == 0) {
		index->upsert_format =
			vy_tuple_format_new_upsert(space->format);
		if (index->upsert_format == NULL)
			goto fail_upsert_format;
		tuple_format_ref(index->upsert_format, 1);

		index->space_format_with_colmask =
			vy_tuple_format_new_with_colmask(space->format);
		if (index->space_format_with_colmask == NULL)
			goto fail_space_format_with_colmask;
		tuple_format_ref(index->space_format_with_colmask, 1);
	} else {
		index->space_format_with_colmask =
			pk->space_format_with_colmask;
		index->upsert_format = pk->upsert_format;
		tuple_format_ref(index->space_format_with_colmask, 1);
		tuple_format_ref(index->upsert_format, 1);
	}

	index->run_hist = histogram_new(run_buckets, lengthof(run_buckets));
	if (index->run_hist == NULL)
		goto fail_run_hist;

	struct lsregion *allocator = &index->env->allocator;
	struct vy_scheduler *scheduler = index->env->scheduler;
	index->mem = vy_mem_new(allocator, scheduler->generation, key_def,
				space->format, index->space_format_with_colmask,
				index->upsert_format, schema_version);
	if (index->mem == NULL)
		goto fail_mem;

	index->generation = scheduler->generation;
	index->dump_lsn = -1;
	vy_cache_create(&index->cache, &e->cache_env, key_def);
	rlist_create(&index->sealed);
	vy_range_tree_new(index->tree);
	rlist_create(&index->runs);
	read_set_new(&index->read_set);
	index->pk = pk;
	if (pk != NULL)
		vy_index_ref(pk);
	index->space_format = space->format;
	tuple_format_ref(index->space_format, 1);
	index->space_index_count = space->index_count;
	index->in_dump.pos = UINT32_MAX;
	index->space_id = user_index_def->space_id;
	index->id = user_index_def->iid;
	index->opts = user_index_def->opts;

	vy_scheduler_add_mem(scheduler, index->mem);
	return index;

fail_mem:
	histogram_delete(index->run_hist);
fail_run_hist:
	tuple_format_ref(index->space_format_with_colmask, -1);
fail_space_format_with_colmask:
	tuple_format_ref(index->upsert_format, -1);
fail_upsert_format:
	tuple_format_ref(index->surrogate_format, -1);
fail_format:
	if (user_index_def->iid > 0)
		free(key_def);
fail_key_def:
	free(user_key_def);
fail_user_key_def:
	free(index->tree);
fail_tree:
	free(index);
fail:
	return NULL;
}

int
vy_prepare_alter_space(struct space *old_space, struct space *new_space)
{
	/*
	 * The space with no indexes can contain no rows.
	 * Allow alter.
	 */
	if (old_space->index_count == 0)
		return 0;
	struct vy_index *pk = vy_index(old_space->index[0]);
	/*
	 * During WAL recovery, the space may be not empty. But we
	 * open existing indexes, not creating new ones. Allow
	 * alter.
	 */
	if (pk->env->status != VINYL_ONLINE)
		return 0;
	/* The space is empty. Allow alter. */
	if (pk->stat.disk.count.rows == 0 &&
	    pk->stat.memory.count.rows == 0)
		return 0;
	if (old_space->index_count < new_space->index_count) {
		diag_set(ClientError, ER_UNSUPPORTED, "Vinyl",
			 "adding an index to a non-empty space");
		return -1;
	}

	if (old_space->index_count == new_space->index_count) {
		/* Check index_defs to be unchanged. */
		for (uint32_t i = 0; i < old_space->index_count; ++i) {
			struct index_def *old_def, *new_def;
			old_def = space_index_def(old_space, i);
			new_def = space_index_def(new_space, i);
			/*
			 * We do not support a full rebuild in
			 * vinyl yet.
			 */
			if (index_def_change_requires_rebuild(old_def,
							      new_def)) {
				diag_set(ClientError, ER_UNSUPPORTED, "Vinyl",
					 "changing the definition of a non-empty "\
					 "index");
				return -1;
			}
		}
	}
	/* Drop index or a change in index options. */
	return 0;
}

int
vy_commit_alter_space(struct space *old_space, struct space *new_space)
{
	(void) old_space;
	struct vy_index *pk = vy_index(new_space->index[0]);
	struct index_def *new_user_def = space_index_def(new_space, 0);

	assert(pk->pk == NULL);

	/* Update the format with column mask. */
	struct tuple_format *format =
		vy_tuple_format_new_with_colmask(new_space->format);
	if (format == NULL)
		return -1;
	tuple_format_ref(format, 1);

	/* Update the upsert format. */
	struct tuple_format *upsert_format =
		vy_tuple_format_new_upsert(new_space->format);
	if (upsert_format == NULL) {
		tuple_format_ref(format, -1);
		return -1;
	}
	tuple_format_ref(upsert_format, 1);

	/* Set possibly changed opts. */
	pk->opts = new_user_def->opts;

	/* Set new formats. */
	tuple_format_ref(pk->space_format, -1);
	tuple_format_ref(pk->upsert_format, -1);
	tuple_format_ref(pk->space_format_with_colmask, -1);
	pk->upsert_format = upsert_format;
	pk->space_format_with_colmask = format;
	pk->space_format = new_space->format;
	pk->space_index_count = new_space->index_count;
	tuple_format_ref(pk->space_format, 1);

	for (uint32_t i = 1; i < new_space->index_count; ++i) {
		struct vy_index *index = vy_index(new_space->index[i]);
		vy_index_unref(index->pk);
		vy_index_ref(pk);
		index->pk = pk;
		new_user_def = space_index_def(new_space, i);
		index->opts = new_user_def->opts;
		tuple_format_ref(index->space_format_with_colmask, -1);
		tuple_format_ref(index->space_format, -1);
		index->space_format_with_colmask =
			pk->space_format_with_colmask;
		index->space_format = new_space->format;
		tuple_format_ref(index->space_format_with_colmask, 1);
		tuple_format_ref(index->space_format, 1);
		index->space_index_count = new_space->index_count;
	}
	return 0;
}

static struct txv *
txv_new(struct vy_index *index, struct tuple *stmt, struct vy_tx *tx)
{
	struct txv *v = mempool_alloc(&tx->xm->txv_mempool);
	if (unlikely(v == NULL)) {
		diag_set(OutOfMemory, sizeof(struct txv), "mempool_alloc",
			 "struct txv");
		return NULL;
	}
	v->index = index;
	v->mem = NULL;
	v->stmt = stmt;
	tuple_ref(stmt);
	v->region_stmt = NULL;
	v->tx = tx;
	return v;
}

static void
txv_delete(struct txv *v)
{
	tuple_unref(v->stmt);
	mempool_free(&v->tx->xm->txv_mempool, v);
}

static struct txv *
read_set_delete_cb(read_set_t *t, struct txv *v, void *arg)
{
	(void) t;
	(void) arg;
	txv_delete(v);
	return NULL;
}

static struct vy_range *
vy_range_tree_free_cb(vy_range_tree_t *t, struct vy_range *range, void *arg)
{
	(void)t;
	struct vy_scheduler *scheduler = arg;

	if (range->in_compact.pos != UINT32_MAX) {
		/*
		 * The range could have already been removed
		 * by vy_schedule().
		 */
		vy_scheduler_remove_range(scheduler, range);
	}

	struct vy_slice *slice;
	rlist_foreach_entry(slice, &range->slices, in_range)
		vy_slice_wait_pinned(slice);
	vy_range_delete(range);
	return NULL;
}

void
vy_index_delete(struct vy_index *index)
{
	struct vy_scheduler *scheduler = index->env->scheduler;

	assert(index->refs == 0);

	if (index->pk != NULL)
		vy_index_unref(index->pk);

	if (index->in_dump.pos != UINT32_MAX) {
		/*
		 * The index could have already been removed
		 * by vy_schedule().
		 */
		vy_scheduler_remove_index(scheduler, index);
	}

	/* Delete all in-memory trees. */
	assert(index->mem != NULL);
	vy_scheduler_remove_mem(scheduler, index->mem);
	vy_mem_delete(index->mem);
	while (!rlist_empty(&index->sealed)) {
		struct vy_mem *mem = rlist_shift_entry(&index->sealed,
						struct vy_mem, in_sealed);
		vy_scheduler_remove_mem(scheduler, mem);
		vy_mem_delete(mem);
	}

	read_set_iter(&index->read_set, NULL, read_set_delete_cb, NULL);
	vy_range_tree_iter(index->tree, NULL,
			   vy_range_tree_free_cb, scheduler);
	tuple_format_ref(index->surrogate_format, -1);
	tuple_format_ref(index->space_format_with_colmask, -1);
	tuple_format_ref(index->upsert_format, -1);
	if (index->id > 0)
		free(index->key_def);
	free(index->user_key_def);
	histogram_delete(index->run_hist);
	vy_cache_destroy(&index->cache);
	tuple_format_ref(index->space_format, -1);
	free(index->tree);
	TRASH(index);
	free(index);
}

size_t
vy_index_bsize(struct vy_index *index)
{
	return index->stat.memory.count.bytes;
}

/** True if the transaction is in a read view. */
bool
vy_tx_is_in_read_view(struct vy_tx *tx)
{
	return tx->read_view->vlsn != INT64_MAX;
}

/**
 * Add the statement to the current transaction.
 * @param tx    Current transaction.
 * @param index Index in whose write_set insert the statement.
 * @param stmt  Statement to set.
 */
static int
vy_tx_set(struct vy_tx *tx, struct vy_index *index, struct tuple *stmt)
{
	assert(vy_stmt_type(stmt) != 0);
	struct vy_stat *stat = index->env->stat;
	/**
	 * A statement in write set must have and unique lsn
	 * in order to differ it from cachable statements in mem and run.
	 */
	vy_stmt_set_lsn(stmt, INT64_MAX);

	/* Update concurrent index */
	struct txv *old = write_set_search_key(&tx->write_set, index, stmt);
	/* Found a match of the previous action of this transaction */
	if (old != NULL) {
		if (vy_stmt_type(stmt) == IPROTO_UPSERT) {
			assert(index->id == 0);
			uint8_t old_type = vy_stmt_type(old->stmt);
			assert(old_type == IPROTO_UPSERT ||
			       old_type == IPROTO_REPLACE ||
			       old_type == IPROTO_DELETE);
			(void) old_type;

			stmt = vy_apply_upsert(stmt, old->stmt, index->key_def,
					       index->space_format,
					       index->upsert_format, true);
			rmean_collect(stat->rmean, VY_STAT_UPSERT_APPLIED, 1);
			if (stmt == NULL)
				return -1;
			assert(vy_stmt_type(stmt) != 0);
			rmean_collect(stat->rmean, VY_STAT_UPSERT_SQUASHED, 1);
		}
		assert(tx->write_size >= tuple_size(old->stmt));
		tx->write_size -= tuple_size(old->stmt);
		tx->write_size += tuple_size(stmt);
		tuple_unref(old->stmt);
		tuple_ref(stmt);
		old->stmt = stmt;
	} else {
		/* Allocate a MVCC container. */
		struct txv *v = txv_new(index, stmt, tx);
		v->is_read = false;
		v->is_gap = false;
		write_set_insert(&tx->write_set, v);
		tx->write_set_version++;
		tx->write_size += tuple_size(stmt);
		stailq_add_tail_entry(&tx->log, v, next_in_log);
	}
	return 0;
}

/* {{{ Public API of transaction control: start/end transaction,
 * read, write data in the context of a transaction.
 */

/**
 * Check if a request has already been committed to an index.
 *
 * If we're recovering the WAL, it may happen so that this
 * particular run was dumped after the checkpoint, and we're
 * replaying records already present in the database. In this
 * case avoid overwriting a newer version with an older one.
 *
 * If the index is going to be dropped or truncated on WAL
 * recovery, there's no point in replaying statements for it,
 * either.
 */
static inline bool
vy_is_committed_one(struct vy_tx *tx, struct space *space,
		    struct vy_index *index)
{
	struct vy_env *env = tx->xm->env;
	if (likely(env->status != VINYL_FINAL_RECOVERY_LOCAL))
		return false;
	if (index->is_dropped)
		return true;
	if (index->truncate_count > space->truncate_count)
		return true;
	if (vclock_sum(env->recovery_vclock) <= index->dump_lsn)
		return true;
	return false;
}

/**
 * Check if a request has already been committed to a space.
 * See also vy_is_committed_one().
 */
static inline bool
vy_is_committed(struct vy_tx *tx, struct space *space)
{
	struct vy_env *env = tx->xm->env;
	if (likely(env->status != VINYL_FINAL_RECOVERY_LOCAL))
		return false;
	for (uint32_t iid = 0; iid < space->index_count; iid++) {
		struct vy_index *index = vy_index(space->index[iid]);
		if (!vy_is_committed_one(tx, space, index))
			return false;
	}
	return true;
}

/**
 * Get a vinyl tuple from the index by the key.
 * @param tx          Current transaction.
 * @param index       Index in which search.
 * @param key         MessagePack'ed data, the array without a
 *                    header.
 * @param part_count  Part count of the key.
 * @param[out] result The found tuple is stored here. Must be
 *                    unreferenced after usage.
 *
 * @param  0 Success.
 * @param -1 Memory error or read error.
 */
static inline int
vy_index_get(struct vy_tx *tx, struct vy_index *index, const char *key,
	     uint32_t part_count, struct tuple **result)
{
	struct vy_env *e = index->env;
	/*
	 * tx can be NULL, for example, if an user calls
	 * space.index.get({key}).
	 */
	assert(tx == NULL || tx->state == VINYL_TX_READY);
	struct tuple *vykey;
	assert(part_count <= index->key_def->part_count);
	vykey = vy_stmt_new_select(e->key_format, key, part_count);
	if (vykey == NULL)
		return -1;
	ev_tstamp start  = ev_now(loop());
	const struct vy_read_view **p_read_view;
	if (tx != NULL) {
		p_read_view = (const struct vy_read_view **) &tx->read_view;
	} else {
		p_read_view = &e->xm->p_global_read_view;
	}

	struct vy_read_iterator itr;
	vy_read_iterator_open(&itr, index, tx, ITER_EQ, vykey, p_read_view);
	if (vy_read_iterator_next(&itr, result) != 0)
		goto error;
	if (tx != NULL && vy_tx_track(tx, index, vykey, *result == NULL) != 0) {
		vy_read_iterator_close(&itr);
		goto error;
	}
	tuple_unref(vykey);
	if (*result != NULL)
		tuple_ref(*result);
	vy_read_iterator_close(&itr);
	vy_stat_get(e->stat, start);
	return 0;
error:
	tuple_unref(vykey);
	return -1;
}

/**
 * Check if the index contains the key. If true, then set
 * a duplicate key error in the diagnostics area.
 * @param tx         Current transaction.
 * @param space      Target space.
 * @param index      Index in which to search.
 * @param key        MessagePack'ed data, the array without a
 *                   header.
 * @param part_count Part count of the key.
 *
 * @retval  0 Success, the key isn't found.
 * @retval -1 Memory error or the key is found.
 */
static inline int
vy_check_dup_key(struct vy_tx *tx, struct space *space,
		 struct vy_index *idx, const char *key, uint32_t part_count)
{
	struct tuple *found;
	(void) part_count;
	/*
	 * Expect a full tuple as input (secondary key || primary key)
	 * but use only  the secondary key fields (partial key look
	 * up) to check for duplicates.
         */
	assert(part_count == idx->key_def->part_count);
	if (vy_index_get(tx, idx, key, idx->user_key_def->part_count, &found))
		return -1;

	if (found) {
		tuple_unref(found);
		diag_set(ClientError, ER_TUPLE_FOUND,
			 index_name_by_id(space, idx->id), space_name(space));
		return -1;
	}
	return 0;
}

/**
 * Insert a tuple in a primary index.
 * @param tx    Current transaction.
 * @param space Target space.
 * @param pk    Primary vinyl index.
 * @param stmt  Tuple to insert.
 *
 * @retval  0 Success.
 * @retval -1 Memory error or duplicate key error.
 */
static inline int
vy_insert_primary(struct vy_tx *tx, struct space *space,
		  struct vy_index *pk, struct tuple *stmt)
{
	assert(vy_stmt_type(stmt) == IPROTO_REPLACE);
	assert(tx != NULL && tx->state == VINYL_TX_READY);
	const char *key;
	assert(pk->id == 0);
	key = tuple_extract_key(stmt, pk->key_def, NULL);
	if (key == NULL)
		return -1;
	/*
	 * A primary index is always unique and the new tuple must not
	 * conflict with existing tuples.
	 */
	uint32_t part_count = mp_decode_array(&key);
	if (vy_check_dup_key(tx, space, pk, key, part_count))
		return -1;
	return vy_tx_set(tx, pk, stmt);
}

/**
 * Insert a tuple in a secondary index.
 * @param tx        Current transaction.
 * @param space     Target space.
 * @param index     Secondary index.
 * @param stmt      Tuple to replace.
 *
 * @retval  0 Success.
 * @retval -1 Memory error or duplicate key error.
 */
static int
vy_insert_secondary(struct vy_tx *tx, struct space *space,
		    struct vy_index *index, struct tuple *stmt)
{
	assert(vy_stmt_type(stmt) == IPROTO_REPLACE);
	assert(tx != NULL && tx->state == VINYL_TX_READY);
	assert(index->id > 0);
	/*
	 * If the index is unique then the new tuple must not
	 * conflict with existing tuples. If the index is not
	 * unique a conflict is impossible.
	 */
	if (index->opts.is_unique) {
		uint32_t key_len;
		const char *key = tuple_extract_key(stmt, index->key_def,
						    &key_len);
		if (key == NULL)
			return -1;
		uint32_t part_count = mp_decode_array(&key);
		if (vy_check_dup_key(tx, space, index, key, part_count))
			return -1;
	}
	return vy_tx_set(tx, index, stmt);
}

/**
 * Execute REPLACE in a space with a single index, possibly with
 * lookup for an old tuple if the space has at least one
 * on_replace trigger.
 * @param tx      Current transaction.
 * @param space   Space in which replace.
 * @param request Request with the tuple data.
 * @param stmt    Statement for triggers is filled with old
 *                statement.
 *
 * @retval  0 Success.
 * @retval -1 Memory error OR duplicate key error OR the primary
 *            index is not found OR a tuple reference increment
 *            error.
 */
static inline int
vy_replace_one(struct vy_tx *tx, struct space *space,
	       struct request *request, struct txn_stmt *stmt)
{
	assert(tx != NULL && tx->state == VINYL_TX_READY);
	assert(space->index_count == 1);
	struct vy_index *pk = vy_index(space->index[0]);
	assert(pk->id == 0);
	struct tuple *new_tuple =
		vy_stmt_new_replace(space->format, request->tuple,
				    request->tuple_end);
	if (new_tuple == NULL)
		return -1;
	/**
	 * If the space has triggers, then we need to fetch the
	 * old tuple to pass it to the trigger. Use vy_get to
	 * fetch it.
	 */
	if (stmt != NULL && !rlist_empty(&space->on_replace)) {
		const char *key;
		key = tuple_extract_key(new_tuple, pk->key_def, NULL);
		if (key == NULL)
			goto error_unref;
		uint32_t part_count = mp_decode_array(&key);
		if (vy_get(tx, pk, key, part_count, &stmt->old_tuple) != 0)
			goto error_unref;
	}
	if (vy_tx_set(tx, pk, new_tuple))
		goto error_unref;

	if (stmt != NULL)
		stmt->new_tuple = new_tuple;
	else
		tuple_unref(new_tuple);
	return 0;

error_unref:
	tuple_unref(new_tuple);
	return -1;
}

/**
 * Execute REPLACE in a space with multiple indexes and lookup for
 * an old tuple, that should has been set in \p stmt->old_tuple if
 * the space has at least one on_replace trigger.
 * @param tx      Current transaction.
 * @param space   Vinyl space.
 * @param request Request with the tuple data.
 * @param stmt    Statement for triggers filled with old
 *                statement.
 *
 * @retval  0 Success
 * @retval -1 Memory error OR duplicate key error OR the primary
 *            index is not found OR a tuple reference increment
 *            error.
 */
static inline int
vy_replace_impl(struct vy_tx *tx, struct space *space, struct request *request,
		struct txn_stmt *stmt)
{
	assert(tx != NULL && tx->state == VINYL_TX_READY);
	struct tuple *old_stmt = NULL;
	struct tuple *new_stmt = NULL;
	struct tuple *delete = NULL;
	struct vy_index *pk = vy_index_find(space, 0);
	if (pk == NULL) /* space has no primary key */
		return -1;
	/* Primary key is dumped last. */
	assert(!vy_is_committed_one(tx, space, pk));
	assert(pk->id == 0);
	new_stmt = vy_stmt_new_replace(space->format, request->tuple,
				       request->tuple_end);
	if (new_stmt == NULL)
		return -1;
	const char *key = tuple_extract_key(new_stmt, pk->key_def, NULL);
	if (key == NULL) /* out of memory */
		goto error;
	uint32_t part_count = mp_decode_array(&key);

	/* Get full tuple from the primary index. */
	if (vy_index_get(tx, pk, key, part_count, &old_stmt) != 0)
		goto error;

	/*
	 * Replace in the primary index without explicit deletion
	 * of the old tuple.
	 */
	if (vy_tx_set(tx, pk, new_stmt) != 0)
		goto error;

	if (space->index_count > 1 && old_stmt != NULL) {
		delete = vy_stmt_new_surrogate_delete(space->format, old_stmt);
		if (delete == NULL)
			goto error;
	}

	/* Update secondary keys, avoid duplicates. */
	for (uint32_t iid = 1; iid < space->index_count; ++iid) {
		struct vy_index *index;
		index = vy_index(space->index[iid]);
		if (vy_is_committed_one(tx, space, index))
			continue;
		/*
		 * Delete goes first, so if old and new keys
		 * fully match, there is no look up beyond the
		 * transaction index.
		 */
		if (old_stmt != NULL) {
			if (vy_tx_set(tx, index, delete) != 0)
				goto error;
		}
		if (vy_insert_secondary(tx, space, index, new_stmt) != 0)
			goto error;
	}
	if (delete != NULL)
		tuple_unref(delete);
	/*
	 * The old tuple is used if there is an on_replace
	 * trigger.
	 */
	if (stmt != NULL) {
		stmt->new_tuple = new_stmt;
		stmt->old_tuple = old_stmt;
	}
	return 0;
error:
	if (delete != NULL)
		tuple_unref(delete);
	if (old_stmt != NULL)
		tuple_unref(old_stmt);
	if (new_stmt != NULL)
		tuple_unref(new_stmt);
	return -1;
}

/**
 * Check that the key can be used for search in a unique index.
 * @param  index      Index for checking.
 * @param  key        MessagePack'ed data, the array without a
 *                    header.
 * @param  part_count Part count of the key.
 *
 * @retval  0 The key is valid.
 * @retval -1 The key is not valid, the appropriate error is set
 *            in the diagnostics area.
 */
static inline int
vy_unique_key_validate(struct vy_index *index, const char *key,
		       uint32_t part_count)
{
	assert(index->opts.is_unique);
	assert(key != NULL || part_count == 0);
	/*
	 * The index contains tuples with concatenation of
	 * secondary and primary key fields, while the key
	 * supplied by the user only contains the secondary key
	 * fields. Use the correct key def to validate the key.
	 * The key can be used to look up in the index since the
	 * supplied key parts uniquely identify the tuple, as long
	 * as the index is unique.
	 */
	uint32_t original_part_count = index->user_key_def->part_count;
	if (original_part_count != part_count) {
		diag_set(ClientError, ER_EXACT_MATCH,
			 original_part_count, part_count);
		return -1;
	}
	return key_validate_parts(index->key_def, key, part_count);
}

/**
 * Get a tuple from the primary index by the partial tuple from
 * the secondary index.
 * @param tx        Current transaction.
 * @param index     Secondary index.
 * @param partial   Partial tuple from the secondary \p index.
 * @param[out] full The full tuple is stored here. Must be
 *                  unreferenced after usage.
 *
 * @retval  0 Success.
 * @retval -1 Memory error.
 */
static inline int
vy_index_full_by_stmt(struct vy_tx *tx, struct vy_index *index,
		      const struct tuple *partial, struct tuple **full)
{
	assert(index->id > 0);
	/*
	 * Fetch the primary key from the secondary index tuple.
	 */
	struct vy_index *pk = index->pk;
	assert(pk != NULL);
	uint32_t size;
	const char *tuple = tuple_data_range(partial, &size);
	const char *tuple_end = tuple + size;
	const char *pkey = tuple_extract_key_raw(tuple, tuple_end, pk->key_def,
						 NULL);
	if (pkey == NULL)
		return -1;
	/* Fetch the tuple from the primary index. */
	uint32_t part_count = mp_decode_array(&pkey);
	assert(part_count == pk->key_def->part_count);
	return vy_index_get(tx, pk, pkey, part_count, full);
}

/**
 * Find a tuple in the primary index by the key of the specified
 * index.
 * @param tx          Current transaction.
 * @param index       Index for which the key is specified. Can be
 *                    both primary and secondary.
 * @param key         MessagePack'ed data, the array without a
 *                    header.
 * @param part_count  Count of parts in the key.
 * @param[out] result The found statement is stored here. Must be
 *                    unreferenced after usage.
 *
 * @retval  0 Success.
 * @retval -1 Memory error.
 */
static inline int
vy_index_full_by_key(struct vy_tx *tx, struct vy_index *index, const char *key,
		     uint32_t part_count, struct tuple **result)
{
	struct tuple *found;
	if (vy_index_get(tx, index, key, part_count, &found))
		return -1;
	if (index->id == 0 || found == NULL) {
		*result = found;
		return 0;
	}
	int rc = vy_index_full_by_stmt(tx, index, found, result);
	tuple_unref(found);
	return rc;
}

/**
 * Delete the tuple from all indexes of the vinyl space.
 * @param tx         Current transaction.
 * @param space      Vinyl space.
 * @param tuple      Tuple to delete.
 *
 * @retval  0 Success
 * @retval -1 Memory error or the index is not found.
 */
static inline int
vy_delete_impl(struct vy_tx *tx, struct space *space,
	       const struct tuple *tuple)
{
	struct vy_index *pk = vy_index_find(space, 0);
	if (pk == NULL)
		return -1;
	/* Primary key is dumped last. */
	assert(!vy_is_committed_one(tx, space, pk));
	struct tuple *delete =
		vy_stmt_new_surrogate_delete(space->format, tuple);
	if (delete == NULL)
		return -1;
	if (vy_tx_set(tx, pk, delete) != 0)
		goto error;

	/* At second, delete from seconary indexes. */
	struct vy_index *index;
	for (uint32_t i = 1; i < space->index_count; ++i) {
		index = vy_index(space->index[i]);
		if (vy_is_committed_one(tx, space, index))
			continue;
		if (vy_tx_set(tx, index, delete) != 0)
			goto error;
	}
	tuple_unref(delete);
	return 0;
error:
	tuple_unref(delete);
	return -1;
}

int
vy_delete(struct vy_tx *tx, struct txn_stmt *stmt, struct space *space,
	  struct request *request)
{
	if (vy_is_committed(tx, space))
		return 0;
	struct vy_index *pk = vy_index_find(space, 0);
	if (pk == NULL)
		return -1;
	struct vy_index *index = vy_index_find_unique(space, request->index_id);
	if (index == NULL)
		return -1;
	bool has_secondary = space->index_count > 1;
	const char *key = request->key;
	uint32_t part_count = mp_decode_array(&key);
	if (vy_unique_key_validate(index, key, part_count))
		return -1;
	/*
	 * There are two cases when need to get the full tuple
	 * before deletion.
	 * - if the space has on_replace triggers and need to pass
	 *   to them the old tuple.
	 *
	 * - if the space has one or more secondary indexes, then
	 *   we need to extract secondary keys from the old tuple
	 *   and pass them to indexes for deletion.
	 */
	if (has_secondary || !rlist_empty(&space->on_replace)) {
		if (vy_index_full_by_key(tx, index, key, part_count,
					 &stmt->old_tuple))
			return -1;
		if (stmt->old_tuple == NULL)
			return 0;
	}
	if (has_secondary) {
		assert(stmt->old_tuple != NULL);
		return vy_delete_impl(tx, space, stmt->old_tuple);
	} else { /* Primary is the single index in the space. */
		assert(index->id == 0);
		struct tuple *delete =
			vy_stmt_new_surrogate_delete_from_key(request->key,
							      pk->key_def,
							      space->format);
		if (delete == NULL)
			return -1;
		int rc = vy_tx_set(tx, pk, delete);
		tuple_unref(delete);
		return rc;
	}
}

/**
 * We do not allow changes of the primary key during update.
 *
 * The syntax of update operation allows the user to update the
 * primary key of a tuple, which is prohibited, to avoid funny
 * effects during replication.
 *
 * @param pk         Primary index.
 * @param index_name Name of the index which was updated - it may
 *                   be not the primary index.
 * @param old_tuple  The tuple before update.
 * @param new_tuple  The tuple after update.
 * @param column_mask Bitmask of the update operation.
 *
 * @retval  0 Success, the primary key is not modified in the new
 *            tuple.
 * @retval -1 Attempt to modify the primary key.
 */
static inline int
vy_check_update(struct space *space, const struct vy_index *pk,
		const struct tuple *old_tuple, const struct tuple *new_tuple,
		uint64_t column_mask)
{
	if (!key_update_can_be_skipped(pk->key_def->column_mask, column_mask) &&
	    vy_tuple_compare(old_tuple, new_tuple, pk->key_def) != 0) {
		diag_set(ClientError, ER_CANT_UPDATE_PRIMARY_KEY,
			 index_name_by_id(space, pk->id), space_name(space));
		return -1;
	}
	return 0;
}

/**
 * Check if an UPDATE operation with the specified column mask
 * changes all indexes. In that case we don't need to store
 * column mask in a tuple.
 * @param space Space to update.
 * @param column_mask Bitmask of update operations.
 */
static inline bool
vy_update_changes_all_indexes(const struct space *space, uint64_t column_mask)
{
	for (uint32_t i = 1; i < space->index_count; ++i) {
		struct vy_index *index = vy_index(space->index[i]);
		if (key_update_can_be_skipped(index->key_def->column_mask,
					      column_mask))
			return false;
	}
	return true;
}

int
vy_update(struct vy_tx *tx, struct txn_stmt *stmt, struct space *space,
	  struct request *request)
{
	assert(tx != NULL && tx->state == VINYL_TX_READY);
	if (vy_is_committed(tx, space))
		return 0;
	struct vy_index *index = vy_index_find_unique(space, request->index_id);
	if (index == NULL)
		return -1;
	const char *key = request->key;
	uint32_t part_count = mp_decode_array(&key);
	if (vy_unique_key_validate(index, key, part_count))
		return -1;

	if (vy_index_full_by_key(tx, index, key, part_count, &stmt->old_tuple))
		return -1;
	/* Nothing to update. */
	if (stmt->old_tuple == NULL)
		return 0;

	/* Apply update operations. */
	struct vy_index *pk = vy_index(space->index[0]);
	assert(pk != NULL);
	assert(pk->id == 0);
	/* Primary key is dumped last. */
	assert(!vy_is_committed_one(tx, space, pk));
	uint64_t column_mask = 0;
	const char *new_tuple, *new_tuple_end;
	uint32_t new_size, old_size;
	const char *old_tuple = tuple_data_range(stmt->old_tuple, &old_size);
	const char *old_tuple_end = old_tuple + old_size;
	new_tuple = tuple_update_execute(region_aligned_alloc_cb, &fiber()->gc,
					 request->tuple, request->tuple_end,
					 old_tuple, old_tuple_end, &new_size,
					 request->index_base, &column_mask);
	if (new_tuple == NULL)
		return -1;
	new_tuple_end = new_tuple + new_size;
	/*
	 * Check that the new tuple matches the space format and
	 * the primary key was not modified.
	 */
	if (tuple_validate_raw(space->format, new_tuple))
		return -1;

	bool update_changes_all =
		vy_update_changes_all_indexes(space, column_mask);
	struct tuple_format *mask_format = pk->space_format_with_colmask;
	if (space->index_count == 1 || update_changes_all) {
		stmt->new_tuple = vy_stmt_new_replace(space->format, new_tuple,
						      new_tuple_end);
		if (stmt->new_tuple == NULL)
			return -1;
	} else {
		stmt->new_tuple = vy_stmt_new_replace(mask_format, new_tuple,
						      new_tuple_end);
		if (stmt->new_tuple == NULL)
			return -1;
		vy_stmt_set_column_mask(stmt->new_tuple, column_mask);
	}
	if (vy_check_update(space, pk, stmt->old_tuple, stmt->new_tuple,
			    column_mask) != 0)
		return -1;

	/*
	 * In the primary index the tuple can be replaced without
	 * the old tuple deletion.
	 */
	if (vy_tx_set(tx, pk, stmt->new_tuple) != 0)
		return -1;
	if (space->index_count == 1)
		return 0;

	struct tuple *delete = NULL;
	if (! update_changes_all) {
		delete = vy_stmt_new_surrogate_delete(mask_format,
						      stmt->old_tuple);
		if (delete == NULL)
			return -1;
		vy_stmt_set_column_mask(delete, column_mask);
	} else {
		delete = vy_stmt_new_surrogate_delete(space->format,
						      stmt->old_tuple);
		if (delete == NULL)
			return -1;
	}
	assert(delete != NULL);
	for (uint32_t i = 1; i < space->index_count; ++i) {
		index = vy_index(space->index[i]);
		if (vy_is_committed_one(tx, space, index))
			continue;
		if (vy_tx_set(tx, index, delete) != 0)
			goto error;
		if (vy_insert_secondary(tx, space, index, stmt->new_tuple))
			goto error;
	}
	tuple_unref(delete);
	return 0;
error:
	tuple_unref(delete);
	return -1;
}

/**
 * Insert the tuple in the space without checking duplicates in
 * the primary index.
 * @param tx        Current transaction.
 * @param space     Space in which insert.
 * @param stmt      Tuple to upsert.
 *
 * @retval  0 Success.
 * @retval -1 Memory error or a secondary index duplicate error.
 */
static int
vy_insert_first_upsert(struct vy_tx *tx, struct space *space,
		       struct tuple *stmt)
{
	assert(tx != NULL && tx->state == VINYL_TX_READY);
	assert(space->index_count > 0);
	assert(vy_stmt_type(stmt) == IPROTO_REPLACE);
	struct vy_index *pk = vy_index(space->index[0]);
	assert(pk->id == 0);
	if (vy_tx_set(tx, pk, stmt) != 0)
		return -1;
	struct vy_index *index;
	for (uint32_t i = 1; i < space->index_count; ++i) {
		index = vy_index(space->index[i]);
		if (vy_insert_secondary(tx, space, index, stmt) != 0)
			return -1;
	}
	return 0;
}

/**
 * Insert UPSERT into the write set of the transaction.
 * @param tx        Transaction which deletes.
 * @param index     Index in which \p tx deletes.
 * @param tuple     MessagePack array.
 * @param tuple_end End of the tuple.
 * @param expr      MessagePack array of update operations.
 * @param expr_end  End of the \p expr.
 *
 * @retval  0 Success.
 * @retval -1 Memory error.
 */
static int
vy_index_upsert(struct vy_tx *tx, struct vy_index *index,
	  const char *tuple, const char *tuple_end,
	  const char *expr, const char *expr_end)
{
	assert(tx == NULL || tx->state == VINYL_TX_READY);
	struct tuple *vystmt;
	struct iovec operations[1];
	operations[0].iov_base = (void *)expr;
	operations[0].iov_len = expr_end - expr;
	vystmt = vy_stmt_new_upsert(index->upsert_format, tuple, tuple_end,
				    operations, 1);
	if (vystmt == NULL)
		return -1;
	assert(vy_stmt_type(vystmt) == IPROTO_UPSERT);
	int rc = vy_tx_set(tx, index, vystmt);
	tuple_unref(vystmt);
	return rc;
}

int
vy_upsert(struct vy_tx *tx, struct txn_stmt *stmt, struct space *space,
	  struct request *request)
{
	assert(tx != NULL && tx->state == VINYL_TX_READY);
	if (vy_is_committed(tx, space))
		return 0;
	/* Check update operations. */
	if (tuple_update_check_ops(region_aligned_alloc_cb, &fiber()->gc,
				   request->ops, request->ops_end,
				   request->index_base)) {
		return -1;
	}
	if (request->index_base != 0) {
		if (request_normalize_ops(request))
			return -1;
	}
	assert(request->index_base == 0);
	const char *tuple = request->tuple;
	const char *tuple_end = request->tuple_end;
	const char *ops = request->ops;
	const char *ops_end = request->ops_end;
	struct vy_index *pk = vy_index_find(space, 0);
	if (pk == NULL)
		return -1;
	/* Primary key is dumped last. */
	assert(!vy_is_committed_one(tx, space, pk));
	if (tuple_validate_raw(space->format, tuple))
		return -1;

	if (space->index_count == 1 && rlist_empty(&space->on_replace))
		return vy_index_upsert(tx, pk, tuple, tuple_end, ops, ops_end);

	const char *old_tuple, *old_tuple_end;
	const char *new_tuple, *new_tuple_end;
	uint32_t new_size;
	const char *key;
	uint32_t part_count;
	uint64_t column_mask;
	/*
	 * There are two cases when need to get the old tuple
	 * before upsert:
	 * - if the space has one or more on_repace triggers;
	 *
	 * - if the space has one or more secondary indexes: then
	 *   we need to extract secondary keys from the old tuple
	 *   to delete old tuples from secondary indexes.
	 */
	/* Find the old tuple using the primary key. */
	key = tuple_extract_key_raw(tuple, tuple_end, pk->key_def, NULL);
	if (key == NULL)
		return -1;
	part_count = mp_decode_array(&key);
	if (vy_index_get(tx, pk, key, part_count, &stmt->old_tuple))
		return -1;
	/*
	 * If the old tuple was not found then UPSERT
	 * turns into INSERT.
	 */
	if (stmt->old_tuple == NULL) {
		stmt->new_tuple =
			vy_stmt_new_replace(space->format, tuple, tuple_end);
		if (stmt->new_tuple == NULL)
			return -1;
		return vy_insert_first_upsert(tx, space, stmt->new_tuple);
	}
	uint32_t old_size;
	old_tuple = tuple_data_range(stmt->old_tuple, &old_size);
	old_tuple_end = old_tuple + old_size;

	/* Apply upsert operations to the old tuple. */
	new_tuple = tuple_upsert_execute(region_aligned_alloc_cb,
					 &fiber()->gc, ops, ops_end,
					 old_tuple, old_tuple_end,
					 &new_size, 0, false, &column_mask);
	if (new_tuple == NULL)
		return -1;
	/*
	 * Check that the new tuple matched the space
	 * format and the primary key was not modified.
	 */
	if (tuple_validate_raw(space->format, new_tuple))
		return -1;
	new_tuple_end = new_tuple + new_size;
	bool update_changes_all =
		vy_update_changes_all_indexes(space, column_mask);
	struct tuple_format *mask_format = pk->space_format_with_colmask;
	if (space->index_count == 1 || update_changes_all) {
		stmt->new_tuple = vy_stmt_new_replace(space->format, new_tuple,
						      new_tuple_end);
		if (stmt->new_tuple == NULL)
			return -1;
	} else {
		stmt->new_tuple = vy_stmt_new_replace(mask_format, new_tuple,
						      new_tuple_end);
		if (stmt->new_tuple == NULL)
			return -1;
		vy_stmt_set_column_mask(stmt->new_tuple, column_mask);
	}
	if (vy_check_update(space, pk, stmt->old_tuple, stmt->new_tuple,
			    column_mask) != 0) {
		error_log(diag_last_error(diag_get()));
		/*
		 * Upsert is skipped, to match the semantics of
		 * vy_index_upsert().
		 */
		return 0;
	}
	if (vy_tx_set(tx, pk, stmt->new_tuple))
		return -1;
	if (space->index_count == 1)
		return 0;

	/* Replace in secondary indexes works as delete insert. */
	struct vy_index *index;
	struct tuple *delete = NULL;
	if (! update_changes_all) {
		delete = vy_stmt_new_surrogate_delete(mask_format,
						      stmt->old_tuple);
		if (delete == NULL)
			return -1;
		vy_stmt_set_column_mask(delete, column_mask);
	} else {
		delete = vy_stmt_new_surrogate_delete(space->format,
						      stmt->old_tuple);
		if (delete == NULL)
			return -1;
	}
	assert(delete != NULL);
	for (uint32_t i = 1; i < space->index_count; ++i) {
		index = vy_index(space->index[i]);
		if (vy_is_committed_one(tx, space, index))
			continue;
		if (vy_tx_set(tx, index, delete) != 0)
			goto error;
		if (vy_insert_secondary(tx, space, index, stmt->new_tuple) != 0)
			goto error;
	}
	tuple_unref(delete);
	return 0;
error:
	tuple_unref(delete);
	return -1;
}

/**
 * Execute INSERT in a vinyl space.
 * @param tx      Current transaction.
 * @param stmt    Statement for triggers filled with the new
 *                statement.
 * @param space   Vinyl space.
 * @param request Request with the tuple data and update
 *                operations.
 *
 * @retval  0 Success
 * @retval -1 Memory error OR duplicate error OR the primary
 *            index is not found
 */
static int
vy_insert(struct vy_tx *tx, struct txn_stmt *stmt, struct space *space,
	  struct request *request)
{
	assert(stmt != NULL);
	struct vy_index *pk = vy_index_find(space, 0);
	if (pk == NULL)
		/* The space hasn't the primary index. */
		return -1;
	assert(pk->id == 0);
	/* First insert into the primary index. */
	stmt->new_tuple =
		vy_stmt_new_replace(space->format, request->tuple,
				    request->tuple_end);
	if (stmt->new_tuple == NULL)
		return -1;
	if (vy_insert_primary(tx, space, pk, stmt->new_tuple) != 0)
		return -1;

	for (uint32_t iid = 1; iid < space->index_count; ++iid) {
		struct vy_index *index = vy_index(space->index[iid]);
		if (vy_insert_secondary(tx, space, index, stmt->new_tuple) != 0)
			return -1;
	}
	return 0;
}

int
vy_replace(struct vy_tx *tx, struct txn_stmt *stmt, struct space *space,
	   struct request *request)
{
	struct vy_env *env = tx->xm->env;
	if (vy_is_committed(tx, space))
		return 0;
	/* Check the tuple fields. */
	if (tuple_validate_raw(space->format, request->tuple))
		return -1;
	if (request->type == IPROTO_INSERT && env->status == VINYL_ONLINE)
		return vy_insert(tx, stmt, space, request);

	if (space->index_count == 1) {
		/* Replace in a space with a single index. */
		return vy_replace_one(tx, space, request, stmt);
	} else {
		/* Replace in a space with secondary indexes. */
		return vy_replace_impl(tx, space, request, stmt);
	}
}

static void
vy_tx_create(struct tx_manager *xm, struct vy_tx *tx)
{
	stailq_create(&tx->log);
	write_set_new(&tx->write_set);
	tx->write_set_version = 0;
	tx->write_size = 0;
	tx->start = ev_now(loop());
	tx->xm = xm;
	tx->state = VINYL_TX_READY;
	tx->read_view = (struct vy_read_view *) xm->p_global_read_view;
	tx->psn = 0;
	rlist_create(&tx->cursors);
	xm->tx_count++;
}

static void
vy_tx_abort_cursors(struct vy_tx *tx)
{
	struct vy_cursor *c;
	rlist_foreach_entry(c, &tx->cursors, next_in_tx)
		c->tx = NULL;
}

static void
vy_tx_destroy(struct vy_tx *tx)
{
	/** Abort all open cursors. */
	vy_tx_abort_cursors(tx);

	tx_manager_destroy_read_view(tx->xm, tx->read_view);

	/* Remove from the conflict manager index */
	struct txv *v, *tmp;
	stailq_foreach_entry_safe(v, tmp, &tx->log, next_in_log) {
		if (v->is_read)
			read_set_remove(&v->index->read_set, v);
		txv_delete(v);
	}

	tx->xm->tx_count--;
}

static bool
vy_tx_is_ro(struct vy_tx *tx)
{
	return tx->write_set.rbt_root == &tx->write_set.rbt_nil;
}

/**
 * Remember the read in the conflict manager index.
 */
static int
vy_tx_track(struct vy_tx *tx, struct vy_index *index,
	    struct tuple *key, bool is_gap)
{
	if (vy_tx_is_in_read_view(tx))
		return 0; /* no reason to track reads */
	uint32_t part_count = tuple_field_count(key);
	if (part_count >= index->key_def->part_count) {
		struct txv *v =
			write_set_search_key(&tx->write_set, index, key);
		if (v != NULL && (vy_stmt_type(v->stmt) == IPROTO_REPLACE ||
				  vy_stmt_type(v->stmt) == IPROTO_DELETE)) {
			/** reading from own write set is serializable */
			return 0;
		}
	}
	struct txv *v = read_set_search_key(&index->read_set, key, tx);
	if (v == NULL) {
		if ((v = txv_new(index, key, tx)) == NULL)
			return -1;
		v->is_read = true;
		v->is_gap = is_gap;
		stailq_add_tail_entry(&tx->log, v, next_in_log);
		read_set_insert(&index->read_set, v);
	}
	return 0;
}

/**
 * Send to a read view all transaction which are reading the stmt v
 *  written by tx.
 */
static int
vy_tx_send_to_read_view(struct vy_tx *tx, struct txv *v)
{
	read_set_t *tree = &v->index->read_set;
	struct read_set_key key;
	key.stmt = v->stmt;
	key.tx = NULL;
	/** Find the first value equal to or greater than key */
	for (struct txv *abort = read_set_nsearch(tree, &key);
	     abort != NULL; abort = read_set_next(tree, abort)) {
		/* Check if we're still looking at the matching key. */
		if (vy_stmt_compare(key.stmt, abort->stmt,
				    v->index->key_def) != 0)
			break;
		/* Don't abort self. */
		if (abort->tx == tx)
			continue;
		/* Abort only active TXs */
		if (abort->tx->state != VINYL_TX_READY)
			continue;
		/* Delete of nothing does not cause a conflict */
		if (abort->is_gap && vy_stmt_type(v->stmt) == IPROTO_DELETE)
			continue;
		/* already in (earlier) read view */
		if (vy_tx_is_in_read_view(abort->tx))
			continue;

		struct vy_read_view *rv = tx_manager_read_view(tx->xm);
		if (rv == NULL)
			return -1;
		abort->tx->read_view = rv;
	}
	return 0;
}

/**
 * Abort all transaction which are reading the stmt v written by tx.
 */
static void
vy_tx_abort_readers(struct vy_tx *tx, struct txv *v)
{
	read_set_t *tree = &v->index->read_set;
	struct read_set_key key;
	key.stmt = v->stmt;
	key.tx = NULL;
	/** Find the first value equal to or greater than key */
	for (struct txv *abort = read_set_nsearch(tree, &key);
	     abort != NULL; abort = read_set_next(tree, abort)) {
		/* Check if we're still looking at the matching key. */
		if (vy_stmt_compare(key.stmt, abort->stmt,
				    v->index->key_def) != 0)
			break;
		/* Don't abort self. */
		if (abort->tx == tx)
			continue;
		/* Abort only active TXs */
		if (abort->tx->state != VINYL_TX_READY)
			continue;
		/* Delete of nothing does not cause a conflict */
		if (abort->is_gap && vy_stmt_type(v->stmt) == IPROTO_DELETE)
			continue;
		abort->tx->state = VINYL_TX_ABORT;
	}
}

static int
vy_tx_prepare(struct vy_tx *tx)
{
	struct tx_manager *xm = tx->xm;
	struct vy_env *env = xm->env;

	if (vy_tx_is_ro(tx)) {
		assert(tx->state == VINYL_TX_READY);
		tx->state = VINYL_TX_COMMIT;
		return 0;
	}

	/*
	 * Reserve quota needed by the transaction before allocating
	 * memory. Since this may yield, which opens a time window for
	 * the transaction to be sent to read view or aborted, we call
	 * it before checking for conflicts.
	 */
	if (vy_quota_use(&env->quota, tx->write_size,
			 env->conf->timeout) != 0) {
		diag_set(ClientError, ER_VY_QUOTA_TIMEOUT);
		return -1;
	}

	if (vy_tx_is_in_read_view(tx) || tx->state == VINYL_TX_ABORT) {
		vy_quota_release(&env->quota, tx->write_size);
		env->stat->tx_conflict++;
		diag_set(ClientError, ER_TRANSACTION_CONFLICT);
		return -1;
	}

	assert(tx->state == VINYL_TX_READY);
	tx->state = VINYL_TX_COMMIT;

	assert(tx->read_view == &xm->global_read_view);
	tx->psn = ++xm->psn;

	/** Send to read view read/write intersection */
	for (struct txv *v = write_set_first(&tx->write_set);
	     v != NULL; v = write_set_next(&tx->write_set, v)) {
		if (vy_tx_send_to_read_view(tx, v)) {
			vy_quota_release(&env->quota, tx->write_size);
			return -1;
		}
	}

	/*
	 * Flush transactional changes to the index.
	 * Sic: the loop below must not yield after recovery.
	 */
	size_t mem_used_before = lsregion_used(&env->allocator);
	int count = 0, write_count = 0;
	/* repsert - REPLACE/UPSERT */
	const struct tuple *delete = NULL, *repsert = NULL;
	MAYBE_UNUSED uint32_t current_space_id = 0;
	struct txv *v;
	int rc = 0;
	stailq_foreach_entry(v, &tx->log, next_in_log) {
		/* Save only writes. */
		if (v->is_read)
			continue;

		rc = vy_tx_write_prepare(v);
		if (rc != 0)
			break;
		assert(v->mem != NULL);

		struct vy_index *index = v->index;
		if (index->id == 0) {
			/* The beginning of the new txn_stmt is met. */
			current_space_id = index->space_id;
			repsert = NULL;
			delete = NULL;
		}
		assert(index->space_id == current_space_id);

		/* In secondary indexes only REPLACE/DELETE can be written. */
		vy_stmt_set_lsn(v->stmt, MAX_LSN + tx->psn);
		enum iproto_type type = vy_stmt_type(v->stmt);
		const struct tuple **region_stmt =
			(type == IPROTO_DELETE) ? &delete : &repsert;
		rc = vy_tx_write(index, v->mem, v->stmt, region_stmt);
		if (rc != 0)
			break;
		v->region_stmt = *region_stmt;
		write_count++;
	}
	size_t mem_used_after = lsregion_used(&env->allocator);
	assert(mem_used_after >= mem_used_before);
	size_t write_size = mem_used_after - mem_used_before;
	/*
	 * Insertion of a statement into an in-memory tree can trigger
	 * an allocation of a new tree block. This should not normally
	 * result in a noticeable excess of the memory limit, because
	 * most memory is occupied by statements anyway, but we need to
	 * adjust the quota accordingly in this case.
	 *
	 * The actual allocation size can also be less than reservation
	 * if a statement is allocated from an lsregion slab allocated
	 * by a previous transaction. Take this into account, too.
	 */
	if (write_size >= tx->write_size)
		vy_quota_force_use(&env->quota, write_size - tx->write_size);
	else
		vy_quota_release(&env->quota, tx->write_size - write_size);
	if (rc != 0)
		return -1;
	vy_stat_tx(env->stat, tx->start, count, write_count, write_size);
	xm->last_prepared_tx = tx;
	return 0;
}

static void
vy_tx_commit(struct vy_tx *tx, int64_t lsn)
{
	assert(tx->state == VINYL_TX_COMMIT);
	struct tx_manager *xm = tx->xm;

	if (xm->last_prepared_tx == tx)
		xm->last_prepared_tx = NULL;

	if (vy_tx_is_ro(tx)) {
		vy_tx_destroy(tx);
		return;
	}

	assert(xm->lsn < lsn);
	xm->lsn = lsn;

	/* Fix LSNs of the records and commit changes */
	struct txv *v;
	stailq_foreach_entry(v, &tx->log, next_in_log) {
		if (v->region_stmt != 0) {
			vy_stmt_set_lsn((struct tuple *)v->region_stmt, lsn);
			vy_index_commit_stmt(v->index, v->mem, v->region_stmt);
		}
		if (v->mem != 0)
			vy_mem_unpin(v->mem);
	}

	/* Update read views of dependant transactions. */
	if (tx->read_view != &xm->global_read_view)
		tx->read_view->vlsn = lsn;
	vy_tx_destroy(tx);
}

static void
vy_tx_rollback_after_prepare(struct vy_tx *tx)
{
	assert(tx->state == VINYL_TX_COMMIT);

	struct tx_manager *xm = tx->xm;

	/** expect cascading rollback in reverse order */
	assert(xm->last_prepared_tx == tx);
	xm->last_prepared_tx = NULL;

	struct txv *v;
	stailq_foreach_entry(v, &tx->log, next_in_log) {
		if (v->region_stmt != NULL)
			vy_index_rollback_stmt(v->index, v->mem,
					       v->region_stmt);
		if (v->mem != 0)
			vy_mem_unpin(v->mem);
	}

	/* Abort read views of depened TXs */
	if (tx->read_view != &xm->global_read_view)
		tx->read_view->is_aborted = true;

	for (struct txv *v = write_set_first(&tx->write_set);
	     v != NULL; v = write_set_next(&tx->write_set, v)) {
		vy_tx_abort_readers(tx, v);
	}
}

static void
vy_tx_rollback(struct vy_tx *tx)
{
	tx->xm->env->stat->tx_rlb++;

	if (tx->state == VINYL_TX_COMMIT)
		vy_tx_rollback_after_prepare(tx);

	vy_tx_destroy(tx);
}

struct vy_tx *
vy_begin(struct vy_env *e)
{
	struct vy_tx *tx = mempool_alloc(&e->xm->tx_mempool);
	if (unlikely(tx == NULL)) {
		diag_set(OutOfMemory, sizeof(struct vy_tx), "mempool_alloc",
			 "struct vy_tx");
		return NULL;
	}
	vy_tx_create(e->xm, tx);
	return tx;
}

int
vy_prepare(struct vy_tx *tx)
{
	return vy_tx_prepare(tx);
}

void
vy_commit(struct vy_tx *tx, int64_t lsn)
{
	vy_tx_commit(tx, lsn);
	mempool_free(&tx->xm->tx_mempool, tx);
}

void
vy_rollback(struct vy_tx *tx)
{
	vy_tx_rollback(tx);
	mempool_free(&tx->xm->tx_mempool, tx);
}

void *
vy_savepoint(struct vy_tx *tx)
{
	assert(tx->state == VINYL_TX_READY);
	return stailq_last(&tx->log);
}

void
vy_rollback_to_savepoint(struct vy_tx *tx, void *svp)
{
	assert(tx->state == VINYL_TX_READY);
	struct stailq_entry *last = svp;
	/* Start from the first statement after the savepoint. */
	last = last == NULL ? stailq_first(&tx->log) : stailq_next(last);
	if (last == NULL) {
		/* Empty transaction or no changes after the savepoint. */
		return;
	}
	struct stailq tail;
	stailq_create(&tail);
	stailq_splice(&tx->log, last, &tail);
	struct txv *v, *tmp;
	stailq_foreach_entry_safe(v, tmp, &tail, next_in_log) {
		/* Remove from the conflict manager index */
		if (v->is_read)
			read_set_remove(&v->index->read_set, v);
		/* Remove from the transaction write log. */
		if (!v->is_read) {
			write_set_remove(&tx->write_set, v);
			tx->write_set_version++;
		}
		txv_delete(v);
	}
}

/* }}} Public API of transaction control */

int
vy_get(struct vy_tx *tx, struct vy_index *index, const char *key,
       uint32_t part_count, struct tuple **result)
{
	assert(tx == NULL || tx->state == VINYL_TX_READY);
	assert(result != NULL);
	struct tuple *vyresult = NULL;
	assert(part_count <= index->key_def->part_count);
	if (vy_index_full_by_key(tx, index, key, part_count, &vyresult))
		return -1;
	if (vyresult == NULL)
		return 0;
	*result = vyresult;
	return 0;
}


/** {{{ Environment */

static void
vy_env_quota_timer_cb(ev_loop *loop, ev_timer *timer, int events)
{
	(void)loop;
	(void)events;

	struct vy_env *e = timer->data;

	int64_t tx_write_rate = vy_stat_tx_write_rate(e->stat);
	int64_t dump_bandwidth = vy_stat_dump_bandwidth(e->stat);

	/*
	 * Due to log structured nature of the lsregion allocator,
	 * which is used for allocating statements, we cannot free
	 * memory in chunks, only all at once. Therefore we should
	 * configure the watermark so that by the time we hit the
	 * limit, all memory have been dumped, i.e.
	 *
	 *   limit - watermark      watermark
	 *   ----------------- = --------------
	 *     tx_write_rate     dump_bandwidth
	 */
	size_t watermark = ((double)e->quota.limit * dump_bandwidth /
			    (dump_bandwidth + tx_write_rate + 1));

	vy_quota_set_watermark(&e->quota, watermark);
}

static struct vy_squash_queue *
vy_squash_queue_new(void);
static void
vy_squash_queue_delete(struct vy_squash_queue *q);

struct vy_env *
vy_env_new(void)
{
	struct vy_env *e = malloc(sizeof(*e));
	if (unlikely(e == NULL)) {
		diag_set(OutOfMemory, sizeof(*e), "malloc", "struct vy_env");
		return NULL;
	}
	memset(e, 0, sizeof(*e));
	e->status = VINYL_OFFLINE;
	e->conf = vy_conf_new();
	if (e->conf == NULL)
		goto error_conf;
	e->xm = tx_manager_new(e);
	if (e->xm == NULL)
		goto error_xm;
	e->stat = vy_stat_new();
	if (e->stat == NULL)
		goto error_stat;
	e->scheduler = vy_scheduler_new(e);
	if (e->scheduler == NULL)
		goto error_sched;
	e->squash_queue = vy_squash_queue_new();
	if (e->squash_queue == NULL)
		goto error_squash_queue;
	e->key_format = tuple_format_new(&vy_tuple_format_vtab,
					  NULL, 0, 0);
	if (e->key_format == NULL)
		goto error_key_format;
	tuple_format_ref(e->key_format, 1);

	struct slab_cache *slab_cache = cord_slab_cache();
	mempool_create(&e->cursor_pool, slab_cache,
	               sizeof(struct vy_cursor));
	lsregion_create(&e->allocator, slab_cache->arena);

	vy_quota_init(&e->quota, vy_scheduler_quota_exceeded_cb,
		                 vy_scheduler_quota_throttled_cb,
				 vy_scheduler_quota_released_cb);
	ev_timer_init(&e->quota_timer, vy_env_quota_timer_cb, 0, 1.);
	e->quota_timer.data = e;
	ev_timer_start(loop(), &e->quota_timer);
	vy_cache_env_create(&e->cache_env, slab_cache,
			    e->conf->cache);
	vy_run_env_create(&e->run_env);
	vy_log_init(e->conf->path);
	return e;
error_key_format:
	vy_squash_queue_delete(e->squash_queue);
error_squash_queue:
	vy_scheduler_delete(e->scheduler);
error_sched:
	vy_stat_delete(e->stat);
error_stat:
	tx_manager_delete(e->xm);
error_xm:
	vy_conf_delete(e->conf);
error_conf:
	free(e);
	return NULL;
}

void
vy_env_delete(struct vy_env *e)
{
	ev_timer_stop(loop(), &e->quota_timer);
	vy_squash_queue_delete(e->squash_queue);
	vy_scheduler_delete(e->scheduler);
	tx_manager_delete(e->xm);
	vy_conf_delete(e->conf);
	vy_stat_delete(e->stat);
	tuple_format_ref(e->key_format, -1);
	mempool_destroy(&e->cursor_pool);
	vy_run_env_destroy(&e->run_env);
	lsregion_destroy(&e->allocator);
	vy_cache_env_destroy(&e->cache_env);
	if (e->recovery != NULL)
		vy_recovery_delete(e->recovery);
	vy_log_free();
	TRASH(e);
	free(e);
}

/** }}} Environment */

/** {{{ Recovery */

int
vy_bootstrap(struct vy_env *e)
{
	assert(e->status == VINYL_OFFLINE);
	e->status = VINYL_ONLINE;
	vy_quota_set_limit(&e->quota, e->conf->memory_limit);
	if (vy_log_bootstrap() != 0)
		return -1;
	return 0;
}

int
vy_begin_initial_recovery(struct vy_env *e,
			  const struct vclock *recovery_vclock)
{
	assert(e->status == VINYL_OFFLINE);
	if (recovery_vclock != NULL) {
		e->xm->lsn = vclock_sum(recovery_vclock);
		e->status = VINYL_INITIAL_RECOVERY_LOCAL;
		e->recovery_vclock = recovery_vclock;
		e->recovery = vy_log_begin_recovery(recovery_vclock);
		if (e->recovery == NULL)
			return -1;
	} else {
		e->status = VINYL_INITIAL_RECOVERY_REMOTE;
		vy_quota_set_limit(&e->quota, e->conf->memory_limit);
		if (vy_log_bootstrap() != 0)
			return -1;
	}
	return 0;
}

int
vy_begin_final_recovery(struct vy_env *e)
{
	switch (e->status) {
	case VINYL_INITIAL_RECOVERY_LOCAL:
		e->status = VINYL_FINAL_RECOVERY_LOCAL;
		break;
	case VINYL_INITIAL_RECOVERY_REMOTE:
		e->status = VINYL_FINAL_RECOVERY_REMOTE;
		break;
	default:
		unreachable();
	}
	return 0;
}

int
vy_end_recovery(struct vy_env *e)
{
	switch (e->status) {
	case VINYL_FINAL_RECOVERY_LOCAL:
		vy_quota_set_limit(&e->quota, e->conf->memory_limit);
		if (vy_log_end_recovery() != 0)
			return -1;
		/*
		 * If the instance is shut down while a dump or
		 * compaction task is in progress, we'll get an
		 * unfinished run file on disk, i.e. a run file
		 * which was either not written to the end or not
		 * inserted into a range. We need to delete such
		 * runs on recovery.
		 */
		vy_gc(e, e->recovery, VY_GC_INCOMPLETE, INT64_MAX);
		vy_recovery_delete(e->recovery);
		e->recovery = NULL;
		e->recovery_vclock = NULL;
		break;
	case VINYL_FINAL_RECOVERY_REMOTE:
		break;
	default:
		unreachable();
	}
	e->status = VINYL_ONLINE;
	return 0;
}

/** }}} Recovery */

/**
 * Return statements from the write set of the current
 * transactions.
 *
 * @sa vy_run_iterator, vy_mem_iterator, with which
 * this iterator shares the interface.
 */
struct vy_txw_iterator {
	/** Parent class, must be the first member */
	struct vy_stmt_iterator base;
	/** Iterator usage statistics */
	struct vy_iterator_stat *stat;

	struct vy_index *index;
	struct vy_tx *tx;

	/* Search options */
	/**
	 * Iterator type, that specifies direction, start position and stop
	 * criteria if key == NULL: GT and EQ are changed to GE, LT to LE for
	 * beauty.
	 */
	enum iterator_type iterator_type;
	/** Key to search. */
	const struct tuple *key;

	/* Last version of vy_tx */
	uint32_t version;
	/* Current pos in txw tree */
	struct txv *curr_txv;
	/* Is false until first .._get ot .._next_.. method is called */
	bool search_started;
};

static void
vy_txw_iterator_open(struct vy_txw_iterator *itr, struct vy_iterator_stat *stat,
		     struct vy_index *index, struct vy_tx *tx,
		     enum iterator_type iterator_type,
		     const struct tuple *key);

static void
vy_txw_iterator_close(struct vy_stmt_iterator *vitr);

/* }}} Iterator over transaction writes : forward declaration */

/* {{{ Iterator over transaction writes : implementation */

/** Vtable for vy_stmt_iterator - declared below */
static struct vy_stmt_iterator_iface vy_txw_iterator_iface;

/* Open the iterator. */
static void
vy_txw_iterator_open(struct vy_txw_iterator *itr, struct vy_iterator_stat *stat,
		     struct vy_index *index, struct vy_tx *tx,
		     enum iterator_type iterator_type,
		     const struct tuple *key)
{
	itr->base.iface = &vy_txw_iterator_iface;
	itr->stat = stat;

	itr->index = index;
	itr->tx = tx;

	itr->iterator_type = iterator_type;
	if (tuple_field_count(key) == 0) {
		/* NULL key. change itr->iterator_type for simplification */
		itr->iterator_type = iterator_type == ITER_LT ||
				     iterator_type == ITER_LE ?
				     ITER_LE : ITER_GE;
	}
	itr->key = key;

	itr->version = UINT32_MAX;
	itr->curr_txv = NULL;
	itr->search_started = false;
}

/**
 * Find position in write set of transaction. Used once in first call of
 *  get/next.
 */
static void
vy_txw_iterator_start(struct vy_txw_iterator *itr, struct tuple **ret)
{
	itr->stat->lookup_count++;
	*ret = NULL;
	itr->search_started = true;
	itr->version = itr->tx->write_set_version;
	itr->curr_txv = NULL;
	struct vy_index *index = itr->index;
	struct write_set_key key = { index, itr->key };
	struct txv *txv;
	if (tuple_field_count(itr->key) > 0) {
		if (itr->iterator_type == ITER_EQ)
			txv = write_set_search(&itr->tx->write_set, &key);
		else if (itr->iterator_type == ITER_GE ||
			 itr->iterator_type == ITER_GT)
			txv = write_set_nsearch(&itr->tx->write_set, &key);
		else
			txv = write_set_psearch(&itr->tx->write_set, &key);
		if (txv == NULL || txv->index != index)
			return;
		if (vy_stmt_compare(itr->key, txv->stmt, index->key_def) == 0) {
			while (true) {
				struct txv *next;
				if (itr->iterator_type == ITER_LE ||
				    itr->iterator_type == ITER_GT)
					next = write_set_next(&itr->tx->write_set, txv);
				else
					next = write_set_prev(&itr->tx->write_set, txv);
				if (next == NULL || next->index != index)
					break;
				if (vy_stmt_compare(itr->key, next->stmt,
						    index->key_def) != 0)
					break;
				txv = next;
			}
			if (itr->iterator_type == ITER_GT)
				txv = write_set_next(&itr->tx->write_set, txv);
			else if (itr->iterator_type == ITER_LT)
				txv = write_set_prev(&itr->tx->write_set, txv);
		}
	} else if (itr->iterator_type == ITER_LE) {
		txv = write_set_nsearch(&itr->tx->write_set, &key);
	} else {
		assert(itr->iterator_type == ITER_GE);
		txv = write_set_psearch(&itr->tx->write_set, &key);
	}
	if (txv == NULL || txv->index != index)
		return;
	itr->curr_txv = txv;
	*ret = txv->stmt;
	return;
}

/**
 * Move to next stmt
 * @retval 0 success or EOF (*ret == NULL)
 */
static NODISCARD int
vy_txw_iterator_next_key(struct vy_stmt_iterator *vitr, struct tuple **ret,
			 bool *stop)
{
	(void)stop;
	assert(vitr->iface->next_key == vy_txw_iterator_next_key);
	struct vy_txw_iterator *itr = (struct vy_txw_iterator *) vitr;
	*ret = NULL;

	if (!itr->search_started) {
		vy_txw_iterator_start(itr, ret);
		return 0;
	}
	itr->stat->step_count++;
	itr->version = itr->tx->write_set_version;
	if (itr->curr_txv == NULL)
		return 0;
	if (itr->iterator_type == ITER_LE || itr->iterator_type == ITER_LT)
		itr->curr_txv = write_set_prev(&itr->tx->write_set, itr->curr_txv);
	else
		itr->curr_txv = write_set_next(&itr->tx->write_set, itr->curr_txv);
	if (itr->curr_txv != NULL && itr->curr_txv->index != itr->index)
		itr->curr_txv = NULL;
	if (itr->curr_txv != NULL && itr->iterator_type == ITER_EQ &&
	    vy_stmt_compare(itr->key, itr->curr_txv->stmt,
			    itr->index->key_def) != 0)
		itr->curr_txv = NULL;
	if (itr->curr_txv != NULL)
		*ret = itr->curr_txv->stmt;
	return 0;
}

/**
 * Function for compatibility with run/mem iterators.
 * @retval 0 EOF always
 */
static NODISCARD int
vy_txw_iterator_next_lsn(struct vy_stmt_iterator *vitr, struct tuple **ret)
{
	assert(vitr->iface->next_lsn == vy_txw_iterator_next_lsn);
	(void)vitr;
	*ret = NULL;
	return 0;
}

/**
 * Restore iterator position after some changes in write set. Iterator
 *  position is placed to the next position after last_stmt.
 * @sa struct vy_stmt_iterator comments.
 *
 * Can restore iterator that was out of data previously
 * @retval 0 nothing significant was happend and itr position left the same
 * @retval 1 iterator restored and position changed
 */
static int
vy_txw_iterator_restore(struct vy_stmt_iterator *vitr,
			const struct tuple *last_stmt, struct tuple **ret,
			bool *stop)
{
	(void)stop;
	assert(vitr->iface->restore == vy_txw_iterator_restore);
	struct vy_txw_iterator *itr = (struct vy_txw_iterator *) vitr;
	*ret = NULL;

	if (!itr->search_started && last_stmt == NULL) {
		vy_txw_iterator_start(itr, ret);
		return 0;
	}
	if (last_stmt == NULL || itr->version == itr->tx->write_set_version) {
		if (itr->curr_txv)
			*ret = itr->curr_txv->stmt;
		return 0;
	}

	itr->search_started = true;
	itr->version = itr->tx->write_set_version;
	struct write_set_key key = { itr->index, last_stmt };
	struct tuple *was_stmt = itr->curr_txv != NULL ?
				 itr->curr_txv->stmt : NULL;
	itr->curr_txv = NULL;
	struct txv *txv;
	struct key_def *def = itr->index->key_def;
	if (itr->iterator_type == ITER_LE || itr->iterator_type == ITER_LT)
		txv = write_set_psearch(&itr->tx->write_set, &key);
	else
		txv = write_set_nsearch(&itr->tx->write_set, &key);
	if (txv != NULL && txv->index == itr->index &&
	    vy_tuple_compare(txv->stmt, last_stmt, def) == 0) {
		if (itr->iterator_type == ITER_LE ||
		    itr->iterator_type == ITER_LT)
			txv = write_set_prev(&itr->tx->write_set, txv);
		else
			txv = write_set_next(&itr->tx->write_set, txv);
	}
	if (txv != NULL && txv->index == itr->index &&
	    itr->iterator_type == ITER_EQ &&
	    vy_stmt_compare(itr->key, txv->stmt, def) != 0)
		txv = NULL;
	if (txv == NULL || txv->index != itr->index) {
		assert(was_stmt == NULL);
		return 0;
	}
	itr->curr_txv = txv;
	*ret = txv->stmt;
	return txv->stmt != was_stmt;
}

/**
 * Close the iterator.
 */
static void
vy_txw_iterator_close(struct vy_stmt_iterator *vitr)
{
	assert(vitr->iface->close == vy_txw_iterator_close);
	struct vy_txw_iterator *itr = (struct vy_txw_iterator *) vitr;
	(void)itr; /* suppress warn if NDEBUG */
	TRASH(itr);
}

static struct vy_stmt_iterator_iface vy_txw_iterator_iface = {
	.next_key = vy_txw_iterator_next_key,
	.next_lsn = vy_txw_iterator_next_lsn,
	.restore = vy_txw_iterator_restore,
	.cleanup = NULL,
	.close = vy_txw_iterator_close
};

/* }}} Iterator over transaction writes : implementation */

/* {{{ Merge iterator */

/**
 * Merge source, support structure for vy_merge_iterator
 * Contains source iterator, additional properties and merge state
 */
struct vy_merge_src {
	/** Source iterator */
	union {
		struct vy_run_iterator run_iterator;
		struct vy_mem_iterator mem_iterator;
		struct vy_txw_iterator txw_iterator;
		struct vy_cache_iterator cache_iterator;
		struct vy_stmt_iterator iterator;
	};
	/** Source can change during merge iteration */
	bool is_mutable;
	/** Source belongs to a range (@sa vy_merge_iterator comments). */
	bool belong_range;
	/**
	 * All sources with the same front_id as in struct
	 * vy_merge_iterator are on the same key of current output
	 * stmt (optimization)
	 */
	uint32_t front_id;
	struct tuple *stmt;
};

/**
 * Open the iterator.
 */
static void
vy_merge_iterator_open(struct vy_merge_iterator *itr,
		       enum iterator_type iterator_type,
		       const struct tuple *key,
		       const struct key_def *key_def,
		       struct tuple_format *format,
		       struct tuple_format *upsert_format,
		       bool is_primary)
{
	assert(key != NULL);
	itr->key_def = key_def;
	itr->format = format;
	itr->upsert_format = upsert_format;
	itr->is_primary = is_primary;
	itr->index_version = 0;
	itr->range_version = 0;
	itr->p_index_version = NULL;
	itr->p_range_version = NULL;
	itr->key = key;
	itr->iterator_type = iterator_type;
	itr->src = NULL;
	itr->src_count = 0;
	itr->src_capacity = 0;
	itr->src = NULL;
	itr->curr_src = UINT32_MAX;
	itr->front_id = 1;
	itr->mutable_start = 0;
	itr->mutable_end = 0;
	itr->skipped_start = 0;
	itr->curr_stmt = NULL;
	itr->is_one_value = iterator_type == ITER_EQ &&
		tuple_field_count(key) >= key_def->part_count;
	itr->unique_optimization =
		(iterator_type == ITER_EQ || iterator_type == ITER_GE ||
		 iterator_type == ITER_LE) &&
		tuple_field_count(key) >= key_def->part_count;
	itr->search_started = false;
	itr->range_ended = false;
}

/**
 * Free all resources allocated in a worker thread.
 */
static void
vy_merge_iterator_cleanup(struct vy_merge_iterator *itr)
{
	if (itr->curr_stmt != NULL) {
		tuple_unref(itr->curr_stmt);
		itr->curr_stmt = NULL;
	}
	for (size_t i = 0; i < itr->src_count; i++) {
		vy_iterator_close_f cb =
			itr->src[i].iterator.iface->cleanup;
		if (cb != NULL)
			cb(&itr->src[i].iterator);
	}
	itr->src_capacity = 0;
	itr->range_version = 0;
	itr->index_version = 0;
	itr->p_index_version = NULL;
	itr->p_range_version = NULL;
}

/**
 * Close the iterator and free resources.
 * Can be called only after cleanup().
 */
static void
vy_merge_iterator_close(struct vy_merge_iterator *itr)
{
	assert(cord_is_main());

	assert(itr->curr_stmt == NULL);
	for (size_t i = 0; i < itr->src_count; i++)
		itr->src[i].iterator.iface->close(&itr->src[i].iterator);
	free(itr->src);
	itr->src_count = 0;
	itr->src = NULL;
}

/**
 * Extend internal source array capacity to fit capacity sources.
 * Not necessary to call is but calling it allows to optimize internal memory
 * allocation
 */
static NODISCARD int
vy_merge_iterator_reserve(struct vy_merge_iterator *itr, uint32_t capacity)
{
	if (itr->src_capacity >= capacity)
		return 0;
	struct vy_merge_src *new_src = calloc(capacity, sizeof(*new_src));
	if (new_src == NULL) {
		diag_set(OutOfMemory, capacity * sizeof(*new_src),
			 "calloc", "new_src");
		return -1;
	}
	if (itr->src_count > 0) {
		memcpy(new_src, itr->src, itr->src_count * sizeof(*new_src));
		free(itr->src);
	}
	itr->src = new_src;
	itr->src_capacity = capacity;
	return 0;
}

/**
 * Add another source to merge iterator. Must be called before actual
 * iteration start and must not be called after.
 * @sa necessary order of adding requirements in struct vy_merge_iterator
 * comments.
 * The resulting vy_stmt_iterator must be properly initialized before merge
 * iteration start.
 * param is_mutable - Source can change during merge iteration
 * param belong_range - Source belongs to a range (see vy_merge_iterator comments)
 */
static struct vy_merge_src *
vy_merge_iterator_add(struct vy_merge_iterator *itr,
		      bool is_mutable, bool belong_range)
{
	assert(!itr->search_started);
	if (itr->src_count == itr->src_capacity) {
		if (vy_merge_iterator_reserve(itr, itr->src_count + 1) != 0)
			return NULL;
	}
	if (is_mutable) {
		if (itr->mutable_start == itr->mutable_end)
			itr->mutable_start = itr->src_count;
		itr->mutable_end = itr->src_count + 1;
	}
	itr->src[itr->src_count].front_id = 0;
	struct vy_merge_src *src = &itr->src[itr->src_count++];
	src->is_mutable = is_mutable;
	src->belong_range = belong_range;
	return src;
}

/*
 * Enable version checking.
 */
static void
vy_merge_iterator_set_version(struct vy_merge_iterator *itr,
			      const uint32_t *p_index_version,
			      const uint32_t *p_range_version)
{
	itr->p_index_version = p_index_version;
	if (itr->p_index_version != NULL)
		itr->index_version = *p_index_version;
	itr->p_range_version = p_range_version;
	if (itr->p_range_version != NULL)
		itr->range_version = *p_range_version;
}

/*
 * Try to restore position of merge iterator
 * @retval 0	if position did not change (iterator started)
 * @retval -2	iterator is no more valid
 */
static NODISCARD int
vy_merge_iterator_check_version(struct vy_merge_iterator *itr)
{
	if (itr->p_index_version != NULL &&
	    *itr->p_index_version != itr->index_version)
		return -2;
	if (itr->p_range_version != NULL &&
	    *itr->p_range_version != itr->range_version)
		return -2;
	return 0;
}

/**
 * Iterate to the next key
 * @retval 0 success or EOF (*ret == NULL)
 * @retval -1 read error
 * @retval -2 iterator is not valid anymore
 */
static NODISCARD int
vy_merge_iterator_next_key(struct vy_merge_iterator *itr, struct tuple **ret)
{
	*ret = NULL;
	if (itr->search_started && itr->is_one_value)
		return 0;
	itr->search_started = true;
	if (vy_merge_iterator_check_version(itr))
		return -2;
	const struct key_def *def = itr->key_def;
	int dir = iterator_direction(itr->iterator_type);
	uint32_t prev_front_id = itr->front_id;
	itr->front_id++;
	itr->curr_src = UINT32_MAX;
	struct tuple *min_stmt = NULL;
	itr->range_ended = true;
	int rc = 0;

	bool was_yield_possible = false;
	for (uint32_t i = 0; i < itr->src_count; i++) {
		bool is_yield_possible = i >= itr->mutable_end;
		was_yield_possible = was_yield_possible || is_yield_possible;

		struct vy_merge_src *src = &itr->src[i];
		bool stop = false;
		if (src->front_id == prev_front_id) {
			assert(itr->curr_stmt != NULL);
			assert(i < itr->skipped_start);
			rc = src->iterator.iface->next_key(&src->iterator,
							   &src->stmt, &stop);
		} else if (i < itr->skipped_start || src->stmt == NULL) {
			/*
			 * Do not restore skipped unless it's the first round.
			 * Generally skipped srcs are handled below, but some
			 * iterators need to be restored before next_key call.
			 */
			rc = src->iterator.iface->restore(&src->iterator,
							  itr->curr_stmt,
							  &src->stmt, &stop);
			rc = rc > 0 ? 0 : rc;
		}
		if (vy_merge_iterator_check_version(itr))
			return -2;
		if (rc != 0)
			return rc;
		if (i >= itr->skipped_start && itr->curr_stmt != NULL) {
			while (src->stmt != NULL &&
				dir * vy_tuple_compare(src->stmt, itr->curr_stmt,
						       def) <= 0) {
				rc = src->iterator.iface->next_key(&src->iterator,
								   &src->stmt,
								   &stop);
				if (vy_merge_iterator_check_version(itr))
					return -2;
				if (rc != 0)
					return rc;
			}
		}
		if (i >= itr->skipped_start)
			itr->skipped_start++;

		if (stop && src->stmt == NULL && min_stmt == NULL) {
			itr->front_id++;
			itr->curr_src = i;
			src->front_id = itr->front_id;
			itr->skipped_start = i + 1;
			break;
		}
		if (src->stmt == NULL)
			continue;

		itr->range_ended = itr->range_ended && !src->belong_range;

		if (itr->unique_optimization &&
		    vy_stmt_compare(src->stmt, itr->key, def) == 0)
			stop = true;

		int cmp = min_stmt == NULL ? -1 :
			  dir * vy_tuple_compare(src->stmt, min_stmt, def);
		if (cmp < 0) {
			itr->front_id++;
			if (min_stmt)
				tuple_unref(min_stmt);
			min_stmt = src->stmt;
			tuple_ref(min_stmt);
			itr->curr_src = i;
		}
		if (cmp <= 0)
			src->front_id = itr->front_id;

		if (stop) {
			itr->skipped_start = i + 1;
			break;
		}
	}
	if (itr->skipped_start < itr->src_count)
		itr->range_ended = false;

	if (itr->curr_stmt != NULL && min_stmt != NULL)
		assert(dir * vy_tuple_compare(min_stmt, itr->curr_stmt, def) >= 0);

	for (int i = MIN(itr->skipped_start, itr->mutable_end) - 1;
	     was_yield_possible && i >= (int) itr->mutable_start; i--) {
		struct vy_merge_src *src = &itr->src[i];
		bool stop;
		rc = src->iterator.iface->restore(&src->iterator,
						  itr->curr_stmt,
						  &src->stmt, &stop);
		if (vy_merge_iterator_check_version(itr))
			return -2;
		if (rc < 0)
			return rc;
		if (rc == 0)
			continue;

		int cmp = min_stmt == NULL ? -1 :
			  dir * vy_tuple_compare(src->stmt, min_stmt, def);
		if (cmp < 0) {
			itr->front_id++;
			if (min_stmt)
				tuple_unref(min_stmt);
			min_stmt = src->stmt;
			tuple_ref(min_stmt);
			itr->curr_src = i;
			src->front_id = itr->front_id;
		} else if (cmp == 0) {
			itr->curr_src = MIN(itr->curr_src, (uint32_t)i);
			src->front_id = itr->front_id;
		}
		if (itr->curr_stmt != NULL && min_stmt != NULL)
			assert(dir * vy_tuple_compare(min_stmt, itr->curr_stmt, def) >= 0);
	}

	if (itr->skipped_start < itr->src_count)
		itr->range_ended = false;

	itr->unique_optimization = false;

	if (itr->curr_stmt != NULL)
		tuple_unref(itr->curr_stmt);
	itr->curr_stmt = min_stmt;
	*ret = itr->curr_stmt;

	return 0;
}

/**
 * Iterate to the next (elder) version of the same key
 * @retval 0 success or EOF (*ret == NULL)
 * @retval -1 read error
 * @retval -2 iterator is not valid anymore
 */
static NODISCARD int
vy_merge_iterator_next_lsn(struct vy_merge_iterator *itr, struct tuple **ret)
{
	if (!itr->search_started)
		return vy_merge_iterator_next_key(itr, ret);
	*ret = NULL;
	if (itr->curr_src == UINT32_MAX)
		return 0;
	assert(itr->curr_stmt != NULL);
	const struct key_def *def = itr->key_def;
	struct vy_merge_src *src = &itr->src[itr->curr_src];
	struct vy_stmt_iterator *sub_itr = &src->iterator;
	int rc = sub_itr->iface->next_lsn(sub_itr, &src->stmt);
	if (vy_merge_iterator_check_version(itr))
		return -2;
	if (rc != 0)
		return rc;
	if (src->stmt != NULL) {
		tuple_unref(itr->curr_stmt);
		itr->curr_stmt = src->stmt;
		tuple_ref(itr->curr_stmt);
		*ret = itr->curr_stmt;
		return 0;
	}
	for (uint32_t i = itr->curr_src + 1; i < itr->src_count; i++) {
		src = &itr->src[i];

		if (i >= itr->skipped_start) {
			itr->skipped_start++;
			bool stop = false;
			int cmp = -1;
			while (true) {
				rc = src->iterator.iface->next_key(&src->iterator,
								   &src->stmt,
								   &stop);
				if (vy_merge_iterator_check_version(itr))
					return -2;
				if (rc != 0)
					return rc;
				if (src->stmt == NULL)
					break;
				cmp = vy_tuple_compare(src->stmt, itr->curr_stmt,
						       def);
				if (cmp >= 0)
					break;
			}
			if (cmp == 0)
				itr->src[i].front_id = itr->front_id;
		}

		if (itr->src[i].front_id == itr->front_id) {
			itr->curr_src = i;
			tuple_unref(itr->curr_stmt);
			itr->curr_stmt = itr->src[i].stmt;
			tuple_ref(itr->curr_stmt);
			*ret = itr->curr_stmt;
			return 0;
		}
	}
	itr->curr_src = UINT32_MAX;
	return 0;
}

/**
 * Squash in the single statement all rest statements of current key
 * starting from the current statement.
 *
 * @retval 0 success or EOF (*ret == NULL)
 * @retval -1 error
 * @retval -2 invalid iterator
 */
static NODISCARD int
vy_merge_iterator_squash_upsert(struct vy_merge_iterator *itr,
				struct tuple **ret, bool suppress_error,
				struct vy_stat *stat)
{
	*ret = NULL;
	struct tuple *t = itr->curr_stmt;

	if (t == NULL)
		return 0;
	/* Upserts enabled only in the primary index. */
	assert(vy_stmt_type(t) != IPROTO_UPSERT || itr->is_primary);
	tuple_ref(t);
	while (vy_stmt_type(t) == IPROTO_UPSERT) {
		struct tuple *next;
		int rc = vy_merge_iterator_next_lsn(itr, &next);
		if (rc != 0) {
			tuple_unref(t);
			return rc;
		}
		if (next == NULL)
			break;
		struct tuple *applied;
		assert(itr->is_primary);
		applied = vy_apply_upsert(t, next, itr->key_def, itr->format,
					  itr->upsert_format, suppress_error);
		if (stat != NULL)
			rmean_collect(stat->rmean, VY_STAT_UPSERT_APPLIED, 1);
		tuple_unref(t);
		if (applied == NULL)
			return -1;
		t = applied;
	}
	*ret = t;
	return 0;
}

/**
 * Restore the position of merge iterator after the given key
 * and according to the initial retrieval order.
 */
static NODISCARD int
vy_merge_iterator_restore(struct vy_merge_iterator *itr,
			  const struct tuple *last_stmt)
{
	itr->unique_optimization = false;
	int result = 0;
	for (uint32_t i = 0; i < itr->src_count; i++) {
		struct vy_stmt_iterator *sub_itr = &itr->src[i].iterator;
		bool stop;
		int rc = sub_itr->iface->restore(sub_itr, last_stmt,
						 &itr->src[i].stmt, &stop);
		if (rc < 0)
			return rc;
		if (vy_merge_iterator_check_version(itr) != 0)
			return -2;
		result = result || rc;
	}
	itr->skipped_start = itr->src_count;
	return result;
}

/* }}} Merge iterator */

/* {{{ Iterator over index */

static void
vy_read_iterator_add_tx(struct vy_read_iterator *itr)
{
	assert(itr->tx != NULL);
	struct vy_iterator_stat *stat = &itr->index->env->stat->txw_stat;
	struct vy_merge_src *sub_src =
		vy_merge_iterator_add(&itr->merge_iterator, true, false);
	vy_txw_iterator_open(&sub_src->txw_iterator, stat, itr->index, itr->tx,
			     itr->iterator_type, itr->key);
	vy_txw_iterator_restore(&sub_src->iterator, itr->curr_stmt,
				&sub_src->stmt, NULL);
}

static void
vy_read_iterator_add_cache(struct vy_read_iterator *itr)
{
	struct vy_merge_src *sub_src =
		vy_merge_iterator_add(&itr->merge_iterator, true, false);
	struct vy_iterator_stat *stat = &itr->index->env->stat->cache_stat;
	vy_cache_iterator_open(&sub_src->cache_iterator, stat,
			       &itr->index->cache, itr->iterator_type,
			       itr->key, itr->read_view);
	if (itr->curr_stmt != NULL) {
		/*
		 * In order not to loose stop flag, do not restore cache
		 * iterator in general case (itr->curr_stmt)
		 */
		bool stop = false;
		int rc = sub_src->iterator.iface->restore(&sub_src->iterator,
							  itr->curr_stmt,
							  &sub_src->stmt, &stop);
		(void)rc;
	}
}

static void
vy_read_iterator_add_mem(struct vy_read_iterator *itr)
{
	struct vy_index *index = itr->index;
	struct vy_iterator_stat *stat = &index->env->stat->mem_stat;
	struct vy_merge_src *sub_src;

	/* Add the active in-memory index. */
	assert(index->mem != NULL);
	sub_src = vy_merge_iterator_add(&itr->merge_iterator, true, false);
	vy_mem_iterator_open(&sub_src->mem_iterator, stat, index->mem,
			     itr->iterator_type, itr->key,
			     itr->read_view, itr->curr_stmt);
	/* Add sealed in-memory indexes. */
	struct vy_mem *mem;
	rlist_foreach_entry(mem, &index->sealed, in_sealed) {
		sub_src = vy_merge_iterator_add(&itr->merge_iterator,
						false, false);
		vy_mem_iterator_open(&sub_src->mem_iterator, stat, mem,
				     itr->iterator_type, itr->key,
				     itr->read_view, itr->curr_stmt);
	}
}

static void
vy_read_iterator_add_disk(struct vy_read_iterator *itr)
{
	assert(itr->curr_range != NULL);
	struct vy_index *index = itr->index;
	struct vy_iterator_stat *stat = &index->env->stat->run_stat;
	struct tuple_format *format;
	struct vy_slice *slice;
	/*
	 * The format of the statement must be exactly the space
	 * format with the same identifier to fully match the
	 * format in vy_mem.
	 */
	format = (index->space_index_count == 1 ?
		  index->space_format : index->surrogate_format);
	bool coio_read = cord_is_main() && index->env->status == VINYL_ONLINE;
	rlist_foreach_entry(slice, &itr->curr_range->slices, in_range) {
		/*
		 * vy_task_dump_complete() may yield after adding
		 * a new run slice to a range and before removing
		 * dumped in-memory trees. We must not add both
		 * the slice and the trees in this case, because
		 * merge iterator can't deal with duplicates.
		 * Since index->dump_lsn is bumped after deletion
		 * of dumped in-memory trees, we can filter out
		 * the run slice containing duplicates by LSN.
		 */
		if (slice->run->info.min_lsn > index->dump_lsn)
			continue;
		assert(slice->run->info.max_lsn <= index->dump_lsn);
		struct vy_merge_src *sub_src = vy_merge_iterator_add(
			&itr->merge_iterator, false, true);
		vy_run_iterator_open(&sub_src->run_iterator, coio_read, stat,
				     &index->env->run_env, slice,
				     itr->iterator_type, itr->key,
				     itr->read_view, index->key_def,
				     index->user_key_def, format,
				     index->upsert_format, index->id == 0);
	}
}

/**
 * Set up merge iterator for the current range.
 */
static void
vy_read_iterator_use_range(struct vy_read_iterator *itr)
{
	if (itr->tx != NULL)
		vy_read_iterator_add_tx(itr);

	vy_read_iterator_add_cache(itr);
	vy_read_iterator_add_mem(itr);

	if (itr->curr_range != NULL)
		vy_read_iterator_add_disk(itr);

	/* Enable range and range index version checks */
	vy_merge_iterator_set_version(&itr->merge_iterator,
				      &itr->index->version,
				      itr->curr_range != NULL ?
				      &itr->curr_range->version : NULL);
}

/**
 * Open the iterator.
 */
static void
vy_read_iterator_open(struct vy_read_iterator *itr, struct vy_index *index,
		      struct vy_tx *tx, enum iterator_type iterator_type,
		      const struct tuple *key, const struct vy_read_view **rv)
{
	itr->index = index;
	itr->tx = tx;
	itr->iterator_type = iterator_type;
	itr->key = key;
	itr->read_view = rv;
	itr->search_started = false;
	itr->curr_stmt = NULL;
	itr->curr_range = NULL;
}

/**
 * Start lazy search
 */
void
vy_read_iterator_start(struct vy_read_iterator *itr)
{
	assert(!itr->search_started);
	assert(itr->curr_stmt == NULL);
	assert(itr->curr_range == NULL);
	itr->search_started = true;

	vy_range_iterator_open(&itr->range_iterator, itr->index,
			       itr->iterator_type, itr->key);
	vy_range_iterator_next(&itr->range_iterator, &itr->curr_range);
	vy_merge_iterator_open(&itr->merge_iterator, itr->iterator_type,
			       itr->key, itr->index->key_def,
			       itr->index->space_format,
			       itr->index->upsert_format, itr->index->id == 0);
	vy_read_iterator_use_range(itr);
}

/**
 * Check versions of index and current range and restores position if
 * something was changed
 */
static NODISCARD int
vy_read_iterator_restore(struct vy_read_iterator *itr)
{
	int rc;
restart:
	vy_range_iterator_restore(&itr->range_iterator, itr->curr_stmt,
				  &itr->curr_range);
	/* Re-create merge iterator */
	vy_merge_iterator_cleanup(&itr->merge_iterator);
	vy_merge_iterator_close(&itr->merge_iterator);
	vy_merge_iterator_open(&itr->merge_iterator, itr->iterator_type,
			       itr->key, itr->index->key_def,
			       itr->index->space_format,
			       itr->index->upsert_format, itr->index->id == 0);
	vy_read_iterator_use_range(itr);
	rc = vy_merge_iterator_restore(&itr->merge_iterator, itr->curr_stmt);
	if (rc == -1)
		return -1;
	if (rc == -2)
		goto restart;
	return rc;
}

/**
 * Conventional wrapper around vy_merge_iterator_next_key() to automatically
 * re-create the merge iterator on vy_index/vy_range/vy_run changes.
 */
static NODISCARD int
vy_read_iterator_merge_next_key(struct vy_read_iterator *itr,
				struct tuple **ret)
{
	int rc;
	struct vy_merge_iterator *mi = &itr->merge_iterator;
retry:
	*ret = NULL;
	while ((rc = vy_merge_iterator_next_key(mi, ret)) == -2)
		if (vy_read_iterator_restore(itr) < 0)
			return -1;
	/*
	 * If the iterator after next_key is on the same key then
	 * go to the next.
	 */
	if (*ret != NULL && itr->curr_stmt != NULL &&
	    vy_tuple_compare(itr->curr_stmt, *ret, itr->index->key_def) == 0)
		goto retry;
	return rc;
}

/**
 * Goto next range according to order
 * return 0 : something was found
 * return 1 : no more data
 * return -1 : read error
 */
static NODISCARD int
vy_read_iterator_next_range(struct vy_read_iterator *itr, struct tuple **ret)
{
	assert(itr->curr_range != NULL);
	*ret = NULL;
	struct tuple *stmt = NULL;
	int rc = 0;
	struct vy_index *index = itr->index;
restart:
	vy_merge_iterator_cleanup(&itr->merge_iterator);
	vy_merge_iterator_close(&itr->merge_iterator);
	vy_merge_iterator_open(&itr->merge_iterator, itr->iterator_type,
			       itr->key, index->key_def, index->space_format,
			       index->upsert_format, index->id == 0);
	vy_range_iterator_next(&itr->range_iterator, &itr->curr_range);
	vy_read_iterator_use_range(itr);
	rc = vy_read_iterator_merge_next_key(itr, &stmt);
	if (rc < 0)
		return -1;
	assert(rc >= 0);
	if (!stmt && itr->merge_iterator.range_ended && itr->curr_range != NULL)
		goto restart;

	if (stmt != NULL && itr->curr_range != NULL) {
		/** Check if the statement is out of the range. */
		int dir = iterator_direction(itr->iterator_type);
		if (dir >= 0 && itr->curr_range->end != NULL &&
		    vy_tuple_compare_with_key(stmt, itr->curr_range->end,
					      index->key_def) >= 0) {
			goto restart;
		}
		if (dir < 0 && itr->curr_range->begin != NULL &&
		    vy_tuple_compare_with_key(stmt, itr->curr_range->begin,
					      index->key_def) < 0) {
			goto restart;
		}
	}

	*ret = stmt;
	return rc;
}

static NODISCARD int
vy_read_iterator_next(struct vy_read_iterator *itr, struct tuple **result)
{
	*result = NULL;

	if (!itr->search_started)
		vy_read_iterator_start(itr);

	struct tuple *prev_key = itr->curr_stmt;
	if (prev_key != NULL)
		tuple_ref(prev_key);

	struct tuple *t = NULL;
	struct vy_merge_iterator *mi = &itr->merge_iterator;
	struct vy_index *index = itr->index;
	struct vy_stat *stat = index->env->stat;
	int rc = 0;
	while (true) {
		if (vy_read_iterator_merge_next_key(itr, &t)) {
			rc = -1;
			goto clear;
		}
		restart:
		if (mi->range_ended && itr->curr_range != NULL &&
		    vy_read_iterator_next_range(itr, &t)) {
			rc = -1;
			goto clear;
		}
		if (t == NULL) {
			if (itr->curr_stmt != NULL)
				tuple_unref(itr->curr_stmt);
			itr->curr_stmt = NULL;
			rc = 0; /* No more data. */
			break;
		}
		rc = vy_merge_iterator_squash_upsert(mi, &t, true, stat);
		if (rc != 0) {
			if (rc == -1)
				goto clear;
			do {
				if (vy_read_iterator_restore(itr) < 0) {
					rc = -1;
					goto clear;
				}
				rc = vy_merge_iterator_next_lsn(mi, &t);
			} while (rc == -2);
			if (rc != 0)
				goto clear;
			goto restart;
		}
		assert(t != NULL);
		if (vy_stmt_type(t) != IPROTO_DELETE) {
			if (vy_stmt_type(t) == IPROTO_UPSERT) {
				struct tuple *applied;
				assert(index->id == 0);
				applied = vy_apply_upsert(t, NULL,
							  index->key_def,
							  mi->format,
							  mi->upsert_format,
							  true);
				rmean_collect(stat->rmean,
					      VY_STAT_UPSERT_APPLIED, 1);
				tuple_unref(t);
				t = applied;
				assert(vy_stmt_type(t) == IPROTO_REPLACE);
			}
			if (itr->curr_stmt != NULL)
				tuple_unref(itr->curr_stmt);
			itr->curr_stmt = t;
			break;
		} else {
			tuple_unref(t);
		}
	}

	*result = itr->curr_stmt;
	assert(*result == NULL || vy_stmt_type(*result) == IPROTO_REPLACE);

	/**
	 * Add a statement to the cache
	 */
	if ((**itr->read_view).vlsn == INT64_MAX) /* Do not store non-latest data */
		vy_cache_add(&itr->index->cache, *result, prev_key,
			     itr->key, itr->iterator_type);

clear:
	if (prev_key != NULL) {
		if (itr->curr_stmt != NULL)
			/*
			 * It is impossible to return fully equal
			 * statements in sequence. At least they
			 * must have different primary keys.
			 * (index->key_def includes primary
			 * parts).
			 */
			assert(vy_tuple_compare(prev_key, itr->curr_stmt,
						index->key_def) != 0);
		tuple_unref(prev_key);
	}

	return rc;
}

/**
 * Close the iterator and free resources
 */
static void
vy_read_iterator_close(struct vy_read_iterator *itr)
{
	assert(cord_is_main());
	if (itr->curr_stmt != NULL)
		tuple_unref(itr->curr_stmt);
	itr->curr_stmt = NULL;
	if (itr->search_started)
		vy_merge_iterator_cleanup(&itr->merge_iterator);

	if (itr->search_started)
		vy_merge_iterator_close(&itr->merge_iterator);
}

/* }}} Iterator over index */

/** {{{ Replication */

/** Relay context, passed to all relay functions. */
struct vy_join_ctx {
	/** Environment. */
	struct vy_env *env;
	/** Stream to relay statements to. */
	struct xstream *stream;
	/** Pipe to the relay thread. */
	struct cpipe relay_pipe;
	/** Pipe to the tx thread. */
	struct cpipe tx_pipe;
	/**
	 * Cbus message, used for calling functions
	 * on behalf of the relay thread.
	 */
	struct cbus_call_msg cmsg;
	/** ID of the space currently being relayed. */
	uint32_t space_id;
	/** Ordinal number of the index. */
	uint32_t index_id;
	/** Index key definition. */
	struct key_def *key_def;
	/** Index format used for REPLACE and DELETE statements. */
	struct tuple_format *format;
	/** Index format used for UPSERT statements. */
	struct tuple_format *upsert_format;
	/**
	 * Write iterator for merging runs before sending
	 * them to the replica.
	 */
	struct vy_stmt_stream *wi;
	/**
	 * List of run slices of the current range, linked by
	 * vy_slice::in_join. The newer a slice the closer it
	 * is to the head of the list.
	 */
	struct rlist slices;
	/**
	 * LSN to assign to the next statement.
	 *
	 * We can't use original statements' LSNs, because we
	 * send statements not in the chronological order while
	 * the receiving end expects LSNs to grow monotonically
	 * due to the design of the lsregion allocator, which is
	 * used for storing statements in memory.
	 */
	int64_t lsn;
};

static int
vy_send_range_f(struct cbus_call_msg *cmsg)
{
	struct vy_join_ctx *ctx = container_of(cmsg, struct vy_join_ctx, cmsg);

	struct tuple *stmt;
	int rc = ctx->wi->iface->start(ctx->wi);
	if (rc != 0)
		goto err;
	while ((rc = ctx->wi->iface->next(ctx->wi, &stmt)) == 0 &&
	       stmt != NULL) {
		struct xrow_header xrow;
		rc = vy_stmt_encode_primary(stmt, ctx->key_def,
					    ctx->space_id, &xrow);
		if (rc != 0)
			break;
		/* See comment to vy_join_ctx::lsn. */
		xrow.lsn = ++ctx->lsn;
		rc = xstream_write(ctx->stream, &xrow);
		if (rc != 0)
			break;
		fiber_gc();
	}
err:
	ctx->wi->iface->stop(ctx->wi);
	fiber_gc();
	return rc;
}

/**
 * Merge and send all runs from the given relay context.
 * On success, delete runs.
 */
static int
vy_send_range(struct vy_join_ctx *ctx)
{
	if (rlist_empty(&ctx->slices))
		return 0; /* nothing to do */

	int rc = -1;
	ctx->wi = vy_write_iterator_new(ctx->key_def,
					ctx->format, ctx->upsert_format,
					true, true, INT64_MAX);
	if (ctx->wi == NULL)
		goto out;

	struct vy_slice *slice;
	rlist_foreach_entry(slice, &ctx->slices, in_join) {
		if (vy_write_iterator_add_slice(ctx->wi, slice,
						&ctx->env->run_env) != 0)
			goto out_delete_wi;
	}

	/* Do the actual work from the relay thread. */
	bool cancellable = fiber_set_cancellable(false);
	rc = cbus_call(&ctx->relay_pipe, &ctx->tx_pipe, &ctx->cmsg,
		       vy_send_range_f, NULL, TIMEOUT_INFINITY);
	fiber_set_cancellable(cancellable);

	struct vy_slice *tmp;
	rlist_foreach_entry_safe(slice, &ctx->slices, in_join, tmp)
		vy_slice_delete(slice);
	rlist_create(&ctx->slices);

out_delete_wi:
	ctx->wi->iface->close(ctx->wi);
	ctx->wi = NULL;
out:
	return rc;
}

/** Relay callback, passed to vy_recovery_iterate(). */
static int
vy_join_cb(const struct vy_log_record *record, void *arg)
{
	struct vy_join_ctx *ctx = arg;

	if (record->type == VY_LOG_CREATE_INDEX ||
	    record->type == VY_LOG_INSERT_RANGE) {
		/*
		 * All runs of the current range have been recovered,
		 * so send them to the replica.
		 */
		if (vy_send_range(ctx) != 0)
			return -1;
	}

	if (record->type == VY_LOG_CREATE_INDEX) {
		ctx->space_id = record->space_id;
		ctx->index_id = record->index_id;
		if (ctx->key_def != NULL)
			free(ctx->key_def);
		ctx->key_def = key_def_dup(record->key_def);
		if (ctx->key_def == NULL)
			return -1;
		if (ctx->format != NULL)
			tuple_format_ref(ctx->format, -1);
		ctx->format = tuple_format_new(&vy_tuple_format_vtab,
				(struct key_def **)&ctx->key_def, 1, 0);
		if (ctx->format == NULL)
			return -1;
		tuple_format_ref(ctx->format, 1);
		if (ctx->upsert_format != NULL)
			tuple_format_ref(ctx->upsert_format, -1);
		ctx->upsert_format = vy_tuple_format_new_upsert(ctx->format);
		if (ctx->upsert_format == NULL)
			return -1;
		tuple_format_ref(ctx->upsert_format, 1);
	}

	/*
	 * We are only interested in the primary index.
	 * Secondary keys will be rebuilt on the destination.
	 */
	if (ctx->index_id != 0)
		return 0;

	if (record->type == VY_LOG_INSERT_SLICE) {
		struct tuple_format *key_format = ctx->env->key_format;
		struct tuple *begin = NULL, *end = NULL;
		bool success = false;

		struct vy_run *run = vy_run_new(record->run_id);
		if (run == NULL)
			goto done_slice;
		if (vy_run_recover(run, ctx->env->conf->path,
				   ctx->space_id, ctx->index_id) != 0)
			goto done_slice;

		if (record->begin != NULL) {
			begin = vy_key_from_msgpack(key_format, record->begin);
			if (begin == NULL)
				goto done_slice;
		}
		if (record->end != NULL) {
			end = vy_key_from_msgpack(key_format, record->end);
			if (end == NULL)
				goto done_slice;
		}

		struct vy_slice *slice = vy_slice_new(record->slice_id,
						run, begin, end, ctx->key_def);
		if (slice == NULL)
			goto done_slice;

		rlist_add_entry(&ctx->slices, slice, in_join);
		success = true;
done_slice:
		if (run != NULL)
			vy_run_unref(run);
		if (begin != NULL)
			tuple_unref(begin);
		if (end != NULL)
			tuple_unref(end);
		if (!success)
			return -1;
	}
	return 0;
}

/** Relay cord function. */
static int
vy_join_f(va_list ap)
{
	struct vy_join_ctx *ctx = va_arg(ap, struct vy_join_ctx *);

	coio_enable();

	cpipe_create(&ctx->tx_pipe, "tx");

	struct cbus_endpoint endpoint;
	cbus_endpoint_create(&endpoint, cord_name(cord()),
			     fiber_schedule_cb, fiber());

	cbus_loop(&endpoint);

	cbus_endpoint_destroy(&endpoint, cbus_process);
	cpipe_destroy(&ctx->tx_pipe);
	return 0;
}

int
vy_join(struct vy_env *env, struct vclock *vclock, struct xstream *stream)
{
	int rc = -1;

	/* Allocate the relay context. */
	struct vy_join_ctx *ctx = malloc(sizeof(*ctx));
	if (ctx == NULL) {
		diag_set(OutOfMemory, PATH_MAX, "malloc", "struct vy_join_ctx");
		goto out;
	}
	memset(ctx, 0, sizeof(*ctx));
	ctx->env = env;
	ctx->stream = stream;
	rlist_create(&ctx->slices);

	/* Start the relay cord. */
	char name[FIBER_NAME_MAX];
	snprintf(name, sizeof(name), "initial_join_%p", stream);
	struct cord cord;
	if (cord_costart(&cord, name, vy_join_f, ctx) != 0)
		goto out_free_ctx;
	cpipe_create(&ctx->relay_pipe, name);

	/*
	 * Load the recovery context from the given point in time.
	 * Send all runs stored in it to the replica.
	 */
	struct vy_recovery *recovery;
	recovery = vy_recovery_new(vclock_sum(vclock), true);
	if (recovery == NULL)
		goto out_join_cord;
	rc = vy_recovery_iterate(recovery, false, vy_join_cb, ctx);
	vy_recovery_delete(recovery);
	/* Send the last range. */
	if (rc == 0)
		rc = vy_send_range(ctx);

	/* Cleanup. */
	if (ctx->key_def != NULL)
		free(ctx->key_def);
	if (ctx->format != NULL)
		tuple_format_ref(ctx->format, -1);
	if (ctx->upsert_format != NULL)
		tuple_format_ref(ctx->upsert_format, -1);
	struct vy_slice *slice, *tmp;
	rlist_foreach_entry_safe(slice, &ctx->slices, in_join, tmp)
		vy_slice_delete(slice);
out_join_cord:
	cbus_stop_loop(&ctx->relay_pipe);
	cpipe_destroy(&ctx->relay_pipe);
	if (cord_cojoin(&cord) != 0)
		rc = -1;
out_free_ctx:
	free(ctx);
out:
	return rc;
}

/* }}} Replication */

/* {{{ Garbage collection */

/** Argument passed to vy_gc_cb(). */
struct vy_gc_arg {
	/** Vinyl environment. */
	struct vy_env *env;
	/**
	 * Specifies what kinds of runs to delete.
	 * See VY_GC_*.
	 */
	unsigned int gc_mask;
	/** LSN of the oldest checkpoint to save. */
	int64_t gc_lsn;
	/**
	 * ID of the current space and index.
	 * Needed for file name formatting.
	 */
	uint32_t space_id;
	uint32_t index_id;
	/** Number of times the callback has been called. */
	int loops;
};

/**
 * Garbage collection callback, passed to vy_recovery_iterate().
 *
 * Given a record encoding information about a vinyl run, try to
 * delete the corresponding files. On success, write a "forget" record
 * to the log so that all information about the run is deleted on the
 * next log rotation.
 */
static int
vy_gc_cb(const struct vy_log_record *record, void *cb_arg)
{
	struct vy_gc_arg *arg = cb_arg;

	switch (record->type) {
	case VY_LOG_CREATE_INDEX:
		arg->space_id = record->space_id;
		arg->index_id = record->index_id;
		goto out;
	case VY_LOG_PREPARE_RUN:
		if ((arg->gc_mask & VY_GC_INCOMPLETE) == 0)
			goto out;
		break;
	case VY_LOG_DROP_RUN:
		if ((arg->gc_mask & VY_GC_DROPPED) == 0 ||
		    record->gc_lsn >= arg->gc_lsn)
			goto out;
		break;
	default:
		goto out;
	}

	ERROR_INJECT(ERRINJ_VY_GC,
		     {say_error("error injection: vinyl run %lld not deleted",
				(long long)record->run_id); goto out;});

	/* Try to delete files. */
	bool forget = true;
	char path[PATH_MAX];
	for (int type = 0; type < vy_file_MAX; type++) {
		vy_run_snprint_path(path, sizeof(path), arg->env->conf->path,
				    arg->space_id, arg->index_id,
				    record->run_id, type);
		if (coio_unlink(path) < 0 && errno != ENOENT) {
			say_syserror("failed to delete file '%s'", path);
			forget = false;
		}
	}

	if (!forget)
		goto out;

	/* Forget the run on success. */
	vy_log_tx_begin();
	vy_log_forget_run(record->run_id);
	if (vy_log_tx_commit() < 0) {
		say_warn("failed to log vinyl run %lld cleanup: %s",
			 (long long)record->run_id,
			 diag_last_error(diag_get())->errmsg);
	}
out:
	if (++arg->loops % VY_YIELD_LOOPS == 0)
		fiber_sleep(0);
	return 0;
}

/** Delete unused run files, see vy_gc_arg for more details. */
static void
vy_gc(struct vy_env *env, struct vy_recovery *recovery,
      unsigned int gc_mask, int64_t gc_lsn)
{
	struct vy_gc_arg arg = {
		.env = env,
		.gc_mask = gc_mask,
		.gc_lsn = gc_lsn,
	};
	vy_recovery_iterate(recovery, true, vy_gc_cb, &arg);
}

void
vy_collect_garbage(struct vy_env *env, int64_t lsn)
{
	/* Cleanup old metadata log files. */
	vy_log_collect_garbage(lsn);

	/* Cleanup run files. */
	int64_t signature = vclock_sum(&env->scheduler->last_checkpoint);
	struct vy_recovery *recovery = vy_recovery_new(signature, false);
	if (recovery == NULL) {
		say_warn("vinyl garbage collection failed: %s",
			 diag_last_error(diag_get())->errmsg);
		return;
	}
	vy_gc(env, recovery, VY_GC_DROPPED, lsn);
	vy_recovery_delete(recovery);
}

/* }}} Garbage collection */

/* {{{ Backup */

/** Argument passed to vy_backup_cb(). */
struct vy_backup_arg {
	/** Vinyl environment. */
	struct vy_env *env;
	/** Backup callback. */
	int (*cb)(const char *, void *);
	/** Argument passed to @cb. */
	void *cb_arg;
	/**
	 * ID of the current space and index.
	 * Needed for file name formatting.
	 */
	uint32_t space_id;
	uint32_t index_id;
	/** Number of times the callback has been called. */
	int loops;
};

/** Backup callback, passed to vy_recovery_iterate(). */
static int
vy_backup_cb(const struct vy_log_record *record, void *cb_arg)
{
	struct vy_backup_arg *arg = cb_arg;

	if (record->type == VY_LOG_CREATE_INDEX) {
		arg->space_id = record->space_id;
		arg->index_id = record->index_id;
	}

	if (record->type != VY_LOG_CREATE_RUN)
		goto out;

	char path[PATH_MAX];
	for (int type = 0; type < vy_file_MAX; type++) {
		vy_run_snprint_path(path, sizeof(path), arg->env->conf->path,
				    arg->space_id, arg->index_id,
				    record->run_id, type);
		if (arg->cb(path, arg->cb_arg) != 0)
			return -1;
	}
out:
	if (++arg->loops % VY_YIELD_LOOPS == 0)
		fiber_sleep(0);
	return 0;
}

int
vy_backup(struct vy_env *env, struct vclock *vclock,
	  int (*cb)(const char *, void *), void *cb_arg)
{
	/* Backup the metadata log. */
	const char *path = vy_log_backup_path(vclock);
	if (path == NULL)
		return 0; /* vinyl not used */
	if (cb(path, cb_arg) != 0)
		return -1;

	/* Backup run files. */
	struct vy_recovery *recovery;
	recovery = vy_recovery_new(vclock_sum(vclock), true);
	if (recovery == NULL)
		return -1;
	struct vy_backup_arg arg = {
		.env = env,
		.cb = cb,
		.cb_arg = cb_arg,
	};
	int rc = vy_recovery_iterate(recovery, false, vy_backup_cb, &arg);
	vy_recovery_delete(recovery);
	return rc;
}

/* }}} Backup */

/**
 * This structure represents a request to squash a sequence of
 * UPSERT statements by inserting the resulting REPLACE statement
 * after them.
 */
struct vy_squash {
	/** Next in vy_squash_queue->queue. */
	struct stailq_entry next;
	/** Index this request is for. */
	struct vy_index *index;
	/** Key to squash upserts for. */
	struct tuple *stmt;
};

struct vy_squash_queue {
	/** Fiber doing background upsert squashing. */
	struct fiber *fiber;
	/** Used to wake up the fiber to process more requests. */
	struct ipc_cond cond;
	/** Queue of vy_squash objects to be processed. */
	struct stailq queue;
	/** Mempool for struct vy_squash. */
	struct mempool pool;
};

static struct vy_squash *
vy_squash_new(struct mempool *pool, struct vy_index *index,
	      struct tuple *stmt)
{
	struct vy_squash *squash;
	squash = mempool_alloc(pool);
	if (squash == NULL)
		return NULL;
	vy_index_ref(index);
	squash->index = index;
	tuple_ref(stmt);
	squash->stmt = stmt;
	return squash;
}

static void
vy_squash_delete(struct mempool *pool, struct vy_squash *squash)
{
	vy_index_unref(squash->index);
	tuple_unref(squash->stmt);
	mempool_free(pool, squash);
}

static int
vy_squash_process(struct vy_squash *squash)
{
	struct errinj *inj = errinj(ERRINJ_VY_SQUASH_TIMEOUT, ERRINJ_DOUBLE);
	if (inj != NULL && inj->dparam > 0)
		fiber_sleep(inj->dparam);

	struct vy_index *index = squash->index;
	struct vy_env *env = index->env;
	struct vy_stat *stat = env->stat;
	struct key_def *def = index->key_def;

	/* Upserts enabled only in the primary index. */
	assert(index->id == 0);

	struct vy_read_iterator itr;
	/*
	 * Use the committed read view to avoid squashing
	 * prepared, but not committed statements.
	 */
	vy_read_iterator_open(&itr, index, NULL, ITER_EQ, squash->stmt,
			      &env->xm->p_committed_read_view);
	struct tuple *result;
	int rc = vy_read_iterator_next(&itr, &result);
	if (rc == 0 && result != NULL)
		tuple_ref(result);
	vy_read_iterator_close(&itr);
	if (rc != 0)
		return -1;
	if (result == NULL)
		return 0;

	/*
	 * While we were reading on-disk runs, new statements could
	 * have been inserted into the in-memory tree. Apply them to
	 * the result.
	 */
	struct vy_mem *mem = index->mem;
	struct tree_mem_key tree_key = {
		.stmt = result,
		.lsn = vy_stmt_lsn(result),
	};
	struct vy_mem_tree_iterator mem_itr =
		vy_mem_tree_lower_bound(&mem->tree, &tree_key, NULL);
	if (vy_mem_tree_iterator_is_invalid(&mem_itr)) {
		/*
		 * The in-memory tree we are squashing an upsert
		 * for was dumped, nothing to do.
		 */
		tuple_unref(result);
		return 0;
	}
	/**
	 * Algorithm of the squashing.
	 * Assume, during building the non-UPSERT statement
	 * 'result' in the mem some new UPSERTs were inserted, and
	 * some of them were commited, while the other were just
	 * prepared. And lets UPSERT_THRESHOLD to be equal to 3,
	 * for example.
	 *                    Mem
	 *    -------------------------------------+
	 *    UPSERT, lsn = 1, n_ups = 0           |
	 *    UPSERT, lsn = 2, n_ups = 1           | Commited
	 *    UPSERT, lsn = 3, n_ups = 2           |
	 *    -------------------------------------+
	 *    UPSERT, lsn = MAX,     n_ups = 3     |
	 *    UPSERT, lsn = MAX + 1, n_ups = 4     | Prepared
	 *    UPSERT, lsn = MAX + 2, n_ups = 5     |
	 *    -------------------------------------+
	 * In such a case the UPSERT statements with
	 * lsns = {1, 2, 3} are squashed. But now the n_upsert
	 * values in the prepared statements are not correct.
	 * If we will not update values, then the
	 * vy_index_commit_upsert will not be able to squash them.
	 *
	 * So after squashing it is necessary to update n_upsert
	 * value in the prepared statements:
	 *                    Mem
	 *    -------------------------------------+
	 *    UPSERT, lsn = 1, n_ups = 0           |
	 *    UPSERT, lsn = 2, n_ups = 1           | Commited
	 *    REPLACE, lsn = 3                     |
	 *    -------------------------------------+
	 *    UPSERT, lsn = MAX,     n_ups = 0 !!! |
	 *    UPSERT, lsn = MAX + 1, n_ups = 1 !!! | Prepared
	 *    UPSERT, lsn = MAX + 2, n_ups = 2 !!! |
	 *    -------------------------------------+
	 */
	vy_mem_tree_iterator_prev(&mem->tree, &mem_itr);
	const struct tuple *mem_stmt;
	int64_t stmt_lsn;
	/*
	 * According to the described algorithm, squash the
	 * commited UPSERTs at first.
	 */
	while (!vy_mem_tree_iterator_is_invalid(&mem_itr)) {
		mem_stmt = *vy_mem_tree_iterator_get_elem(&mem->tree, &mem_itr);
		stmt_lsn = vy_stmt_lsn(mem_stmt);
		if (vy_tuple_compare(result, mem_stmt, def) != 0)
			break;
		/**
		 * Leave alone prepared statements; they will be handled
		 * in vy_range_commit_stmt.
		 */
		if (stmt_lsn >= MAX_LSN)
			break;
		if (vy_stmt_type(mem_stmt) != IPROTO_UPSERT) {
			/**
			 * Somebody inserted non-upsert statement,
			 * squashing is useless.
			 */
			tuple_unref(result);
			return 0;
		}
		assert(index->id == 0);
		struct tuple *applied =
			vy_apply_upsert(mem_stmt, result, def, mem->format,
					mem->upsert_format, true);
		rmean_collect(stat->rmean, VY_STAT_UPSERT_APPLIED, 1);
		tuple_unref(result);
		if (applied == NULL)
			return -1;
		result = applied;
		/**
		 * In normal cases we get a result with the same lsn as
		 * in mem_stmt.
		 * But if there are buggy upserts that do wrong things,
		 * they are ignored and the result has lower lsn.
		 * We should fix the lsn in any case to replace
		 * exactly mem_stmt in general and the buggy upsert
		 * in particular.
		 */
		vy_stmt_set_lsn(result, stmt_lsn);
		vy_mem_tree_iterator_prev(&mem->tree, &mem_itr);
	}
	/*
	 * The second step of the algorithm above is updating of
	 * n_upsert values of the prepared UPSERTs.
	 */
	if (stmt_lsn >= MAX_LSN) {
		uint8_t n_upserts = 0;
		while (!vy_mem_tree_iterator_is_invalid(&mem_itr)) {
			mem_stmt = *vy_mem_tree_iterator_get_elem(&mem->tree,
								  &mem_itr);
			if (vy_tuple_compare(result, mem_stmt, def) != 0 ||
			    vy_stmt_type(mem_stmt) != IPROTO_UPSERT)
				break;
			assert(vy_stmt_lsn(mem_stmt) >= MAX_LSN);
			vy_stmt_set_n_upserts((struct tuple *)mem_stmt,
					      n_upserts);
			if (n_upserts <= VY_UPSERT_THRESHOLD)
				++n_upserts;
			vy_mem_tree_iterator_prev(&mem->tree, &mem_itr);
		}
	}

	rmean_collect(stat->rmean, VY_STAT_UPSERT_SQUASHED, 1);

	/*
	 * Insert the resulting REPLACE statement to the mem
	 * and adjust the quota.
	 */
	size_t mem_used_before = lsregion_used(&env->allocator);
	const struct tuple *region_stmt = NULL;
	rc = vy_index_set(index, mem, result, &region_stmt);
	tuple_unref(result);
	size_t mem_used_after = lsregion_used(&env->allocator);
	assert(mem_used_after >= mem_used_before);
	if (rc == 0) {
		/*
		 * We don't modify the resulting statement,
		 * so there's no need in invalidating the cache.
		 */
		vy_mem_commit_stmt(mem, region_stmt);
		vy_quota_force_use(&env->quota,
				   mem_used_after - mem_used_before);
	}
	return rc;
}

static struct vy_squash_queue *
vy_squash_queue_new(void)
{
	struct vy_squash_queue *sq = malloc(sizeof(*sq));
	if (sq == NULL) {
		diag_set(OutOfMemory, sizeof(*sq), "malloc", "sq");
		return NULL;
	}
	sq->fiber = NULL;
	ipc_cond_create(&sq->cond);
	stailq_create(&sq->queue);
	mempool_create(&sq->pool, cord_slab_cache(),
		       sizeof(struct vy_squash));
	return sq;
}

static void
vy_squash_queue_delete(struct vy_squash_queue *sq)
{
	if (sq->fiber != NULL) {
		sq->fiber = NULL;
		/* Sic: fiber_cancel() can't be used here */
		ipc_cond_signal(&sq->cond);
	}
	struct vy_squash *squash, *next;
	stailq_foreach_entry_safe(squash, next, &sq->queue, next)
		vy_squash_delete(&sq->pool, squash);
	free(sq);
}

static int
vy_squash_queue_f(va_list va)
{
	struct vy_squash_queue *sq = va_arg(va, struct vy_squash_queue *);
	while (sq->fiber != NULL) {
		if (stailq_empty(&sq->queue)) {
			ipc_cond_wait(&sq->cond);
			continue;
		}
		struct vy_squash *squash;
		squash = stailq_shift_entry(&sq->queue, struct vy_squash, next);
		if (vy_squash_process(squash) != 0)
			error_log(diag_last_error(diag_get()));
		vy_squash_delete(&sq->pool, squash);
	}
	return 0;
}

/*
 * For a given UPSERT statement, insert the resulting REPLACE
 * statement after it. Done in a background fiber.
 */
static void
vy_index_squash_upserts(struct vy_index *index, struct tuple *stmt)
{
	struct vy_squash_queue *sq = index->env->squash_queue;

	say_debug("optimize upsert slow: %"PRIu32"/%"PRIu32": %s",
		  index->space_id, index->id, vy_stmt_str(stmt));

	/* Start the upsert squashing fiber on demand. */
	if (sq->fiber == NULL) {
		sq->fiber = fiber_new("vinyl.squash_queue", vy_squash_queue_f);
		if (sq->fiber == NULL)
			goto fail;
		fiber_start(sq->fiber, sq);
	}

	struct vy_squash *squash = vy_squash_new(&sq->pool, index, stmt);
	if (squash == NULL)
		goto fail;

	stailq_add_tail_entry(&sq->queue, squash, next);
	ipc_cond_signal(&sq->cond);
	return;
fail:
	error_log(diag_last_error(diag_get()));
	diag_clear(diag_get());
}

/* {{{ Cursor */

struct vy_cursor *
vy_cursor_new(struct vy_tx *tx, struct vy_index *index, const char *key,
	      uint32_t part_count, enum iterator_type type)
{
	struct vy_env *e = index->env;
	struct vy_cursor *c = mempool_alloc(&e->cursor_pool);
	if (c == NULL) {
		diag_set(OutOfMemory, sizeof(*c), "cursor", "cursor pool");
		return NULL;
	}
	assert(part_count <= index->key_def->part_count);
	c->key = vy_stmt_new_select(e->key_format, key, part_count);
	if (c->key == NULL) {
		mempool_free(&e->cursor_pool, c);
		return NULL;
	}
	c->index = index;
	c->n_reads = 0;
	c->env = e;
	if (tx == NULL) {
		tx = &c->tx_autocommit;
		vy_tx_create(e->xm, tx);
	} else {
		rlist_add(&tx->cursors, &c->next_in_tx);
	}
	c->tx = tx;
	c->start = tx->start;
	c->need_check_eq = false;
	enum iterator_type iterator_type;
	switch (type) {
	case ITER_ALL:
		iterator_type = ITER_GE;
		break;
	case ITER_GE:
	case ITER_GT:
	case ITER_LE:
	case ITER_LT:
	case ITER_EQ:
		iterator_type = type;
		break;
	case ITER_REQ: {
		/* point-lookup iterator (optimization) */
		if (index->opts.is_unique &&
		    part_count == index->key_def->part_count) {
			iterator_type = ITER_EQ;
		} else {
			c->need_check_eq = true;
			iterator_type = ITER_LE;
		}
		break;
	}
	default:
		unreachable();
	}
	vy_read_iterator_open(&c->iterator, index, tx, iterator_type, c->key,
			      (const struct vy_read_view **)&tx->read_view);
	c->iterator_type = iterator_type;
	return c;
}

int
vy_cursor_next(struct vy_cursor *c, struct tuple **result)
{
	struct tuple *vyresult = NULL;
	struct vy_index *index = c->index;
	assert(index->space_index_count > 0);
	*result = NULL;

	if (c->tx == NULL) {
		diag_set(ClientError, ER_NO_ACTIVE_TRANSACTION);
		return -1;
	}
	if (c->tx->state == VINYL_TX_ABORT || c->tx->read_view->is_aborted) {
		diag_set(ClientError, ER_READ_VIEW_ABORTED);
		return -1;
	}

	assert(c->key != NULL);
	int rc = vy_read_iterator_next(&c->iterator, &vyresult);
	if (rc)
		return -1;
	c->n_reads++;
	if (vy_tx_track(c->tx, index, vyresult ? vyresult : c->key,
			vyresult == NULL))
		return -1;
	if (vyresult == NULL)
		return 0;
	if (c->need_check_eq &&
	    vy_tuple_compare_with_key(vyresult, c->key, index->key_def) != 0)
		return 0;
	if (index->id > 0 && vy_index_full_by_stmt(c->tx, index, vyresult,
						   &vyresult))
		return -1;
	*result = vyresult;
	/**
	 * If the index is not primary (def->iid != 0) then no
	 * need to reference the tuple, because it is returned
	 * from vy_index_full_by_stmt() as new statement with 1
	 * reference.
	 */
	if (index->id == 0)
		tuple_ref(vyresult);
	return *result != NULL ? 0 : -1;
}

void
vy_cursor_delete(struct vy_cursor *c)
{
	vy_read_iterator_close(&c->iterator);
	struct vy_env *e = c->env;
	if (c->tx != NULL) {
		if (c->tx == &c->tx_autocommit) {
			/* Rollback the automatic transaction. */
			vy_tx_rollback(c->tx);
		} else {
			/*
			 * Delete itself from the list of open cursors
			 * in the transaction
			 */
			rlist_del(&c->next_in_tx);
		}
	}
	if (c->key)
		tuple_unref(c->key);
	vy_stat_cursor(e->stat, c->start, c->n_reads);
	TRASH(c);
	mempool_free(&e->cursor_pool, c);
}

/*** }}} Cursor */
