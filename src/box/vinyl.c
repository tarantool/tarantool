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

#include "vy_stmt.h"
#include "vy_quota.h"
#include "vy_stmt_iterator.h"
#include "vy_mem.h"
#include "vy_cache.h"

#include <dirent.h>

#include <bit/bit.h>
#include <small/rlist.h>
#define RB_COMPACT 1
#include <small/rb.h>
#include <small/mempool.h>
#include <small/region.h>
#include <small/lsregion.h>
#include <msgpuck/msgpuck.h>
#include <coeio_file.h>

#include "trivia/util.h"
#include "crc32.h"
#include "clock.h"
#include "trivia/config.h"
#include "tt_pthread.h"
#include "cfg.h"
#include "diag.h"
#include "fiber.h" /* cord_slab_cache() */
#include "ipc.h"
#include "coeio.h"
#include "histogram.h"
#include "rmean.h"
#include "salad/bloom.h"
#include "errinj.h"

#include "errcode.h"
#include "key_def.h"
#include "tuple.h"
#include "tuple_update.h"
#include "txn.h" /* box_txn_alloc() */
#include "iproto_constants.h"
#include "replication.h" /* INSTANCE_UUID */
#include "vclock.h"
#include "box.h"
#include "schema.h"
#include "xrow.h"
#include "xlog.h"
#include "fio.h"
#include "space.h"
#include "index.h"
#include "vy_log.h"
#include "xstream.h"
#include "info.h"
#include "request.h"

#define HEAP_FORWARD_DECLARATION
#include "salad/heap.h"

#define vy_cmp(a, b) \
	((a) == (b) ? 0 : (((a) > (b)) ? 1 : -1))

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
	/* bloom filter false positive rate */
	double bloom_fpr;
};

struct vy_env {
	/** Recovery status */
	enum vy_status status;
	/** The list of indexes for vinyl_info(). */
	struct rlist indexes;
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
	/** Mempool for struct vy_page_read_task */
	struct mempool      read_task_pool;
	/** Allocator for tuples */
	struct lsregion     allocator;
	/** Key for thread-local ZSTD context */
	pthread_key_t       zdctx_key;
	/** Memory quota */
	struct vy_quota     quota;
	/** Timer for updating quota watermark. */
	ev_timer            quota_timer;
	/** Enviroment for cache subsystem */
	struct vy_cache_env cache_env;
	/** Local recovery context. */
	struct vy_recovery *recovery;
};

#define vy_crcs(p, size, crc) \
	crc32_calc(crc, (char*)p + sizeof(uint32_t), size - sizeof(uint32_t))

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
vy_stat_dump(struct vy_stat *s, ev_tstamp time, size_t written,
	     uint64_t dumped_statements)
{
	histogram_collect(s->dump_bw, written / time);
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

/**
 * Apply the UPSERT statement to the REPLACE, UPSERT or DELETE statement.
 * If the second statement is
 * - REPLACE then update operations of the first one will be applied to the
 *   second and a REPLACE statement will be returned;
 *
 * - UPSERT then the new UPSERT will be created with combined operations of both
 *   arguments;
 *
 * - DELETE or NULL then the first one will be turned into REPLACE and returned
 *   as the result;
 *
 * @param upsert         An UPSERT statement.
 * @param object         An REPLACE/DELETE/UPSERT statement or NULL.
 * @param index_def        Key definition of an index.
 * @param format         Format for REPLACE/DELETE tuples.
 * @param upsert_format  Format for UPSERT tuples.
 * @param suppress_error True if ClientErrors must not be written to log.
 *
 * @retval NULL     Memory allocation error.
 * @retval not NULL Success.
 */
static struct tuple *
vy_apply_upsert(const struct tuple *upsert, const struct tuple *object,
		const struct index_def *index_def, struct tuple_format *format,
		struct tuple_format *upsert_format, bool suppress_error,
		struct vy_stat *stat);

/**
 * Run metadata. A run is a written to a file as a single
 * chunk.
 */
struct vy_run_info {
	/** Run page count. */
	uint32_t  count;
	/** Number of keys. */
	uint32_t  keys;
	/* Min and max lsn over all statements in the run. */
	int64_t  min_lsn;
	int64_t  max_lsn;
	/** Size of run on disk. */
	uint64_t size;
	/** Bloom filter of all tuples in run */
	bool has_bloom;
	struct bloom bloom;
	/** Pages meta. */
	struct vy_page_info *page_infos;
};

struct vy_page_info {
	/* count of statements in the page */
	uint32_t count;
	/* offset of page data in run */
	uint64_t offset;
	/* size of page data in file */
	uint32_t size;
	/* size of page data in memory, i.e. unpacked */
	uint32_t unpacked_size;
	/* Offset of the min key in the parent run->pages_min. */
	uint32_t min_key_offset;
	/* minimal lsn of all records in page */
	int64_t min_lsn;
	/* maximal lsn of all records in page */
	int64_t max_lsn;
	/* minimal key */
	char *min_key;
	/* row index offset in page */
	uint32_t page_index_offset;
};

static int
vy_page_info_create(struct vy_page_info *page_info, uint64_t offset,
		    const struct index_def *index_def, struct tuple *min_key);

static void
vy_page_info_destroy(struct vy_page_info *page_info);

struct vy_run {
	struct vy_run_info info;
	/** Run data file. */
	int fd;
	/**
	 * Reference counter. The run file is closed and the run
	 * in-memory structure is freed only when it reaches 0.
	 * Needed to prevent coeio thread from using a closed
	 * (worse, reopened) file descriptor.
	 */
	int refs;
	/** Link in range->runs list. */
	struct rlist in_range;
	/** Unique ID of this run. */
	int64_t id;
};

struct vy_range {
	/** Unique ID of this range. */
	int64_t   id;
	/**
	 * Range lower bound. NULL if range is leftmost.
	 * Both 'begin' and 'end' statements have SELECT type with the full
	 * idexed key.
	 */
	char *begin;
	/** Range upper bound. NULL if range is rightmost. */
	char *end;
	struct vy_index *index;
	/* Size of data stored on disk (sum of run->info.size). */
	uint64_t size;
	/** Total amount of memory used by this range (sum of mem->used). */
	size_t used;
	/** Minimal in-memory lsn (min over mem->min_lsn). */
	int64_t min_lsn;
	/** New run created for dump/compaction. */
	struct vy_run *new_run;
	/**
	 * List of all on-disk runs, linked by vy_run->in_range.
	 * The newer a run, the closer it to the list head.
	 */
	struct rlist runs;
	/** Number of entries in the ->runs list. */
	int run_count;
	/** Active in-memory index, i.e. the one used for insertions. */
	struct vy_mem *mem;
	/**
	 * List of frozen in-memory indexes, i.e. indexes that can't
	 * be inserted into, only read from, linked by vy_mem->in_frozen.
	 * The newer an index, the closer it to the list head.
	 */
	struct rlist frozen;
	/**
	 * Size of the largest run that was dumped since the last
	 * range compaction. Required for computing the size of
	 * the base level in the LSM tree.
	 */
	uint64_t max_dump_size;
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
	/**
	 * If this range is a part of a range that is being split,
	 * this field points to the original range.
	 */
	struct vy_range *shadow;
	/** List of ranges this range is being split into. */
	struct rlist split_list;
	rb_node(struct vy_range) tree_node;
	struct heap_node   in_compact;
	struct heap_node   in_dump;
	/**
	 * Incremented whenever an in-memory index or on disk
	 * run is added to or deleted from this range. Used to
	 * invalidate iterators.
	 */
	uint32_t version;
	/**
	 * Ranges tree now is flat, so this flag always is true.
	 * But when we will have introduced the single mem per
	 * index, this flag will be false for ranges from the
	 * index tree and true for the index range.
	 */
	bool is_level_zero;
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
 *   secondary key are only stored once. (@sa index_def_merge
 *   function). This reduces the disk and RAM space necessary to
 *   maintain a secondary index, but adds an extra look-up in the
 *   primary key for every fetched tuple.
 *
 * When a search in a secondary index is made, we first look up
 * the secondary index tuple, containing the primary key, and then
 * use this key to find the original tuple in the primary index.

 * While the primary index has only one index_def that is
 * used for validating tuples, secondary index needs two:
 *
 * - the first one is defined by the user. It contains the key
 *   parts of the secondary key, as present in the original tuple.
 *   This is user_index_def.
 *
 * - the second one is used to fetch key parts of the secondary
 *   key, *augmented* with the parts of the primary key from the
 *   original tuple and compare secondary index tuples. These
 *   parts concatenated together construe the tuple of the
 *   secondary key, i.e. the tuple stored. This is index_def.
 */
struct vy_index {
	struct vy_env *env;
	/* An merge cache for current index. Contains the hotest tuples
	 * with continuation markers.
	 */
	struct vy_cache *cache;
	/**
	 * Conflict manager index. Contains all changes
	 * made by transaction before they commit. Is used
	 * to implement read committed isolation level, i.e.
	 * the changes made by a transaction are only present
	 * in this tree, and thus not seen by other transactions.
	 */
	read_set_t read_set;
	vy_range_tree_t tree;
	/** Number of ranges in this index. */
	int range_count;
	/** Number of runs in all ranges. */
	int run_count;
	/** Number of pages in all runs. */
	int page_count;
	/**
	 * Total number of statements in this index,
	 * stored both in memory and on disk.
	 */
	uint64_t stmt_count;
	/** Size of data stored on disk. */
	uint64_t size;
	/** Amount of memory used by in-memory indexes. */
	uint64_t used;
	/** Histogram of number of runs in range. */
	struct histogram *run_hist;
	/**
	 * Reference counter. Used to postpone index drop
	 * until all pending operations have completed.
	 */
	uint32_t refs;
	/** A schematic name for profiler output. */
	char *name;
	/** The path with index files. */
	char *path;
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
	struct index_def *index_def;
	/**
	 * A key definition that was declared by an user with
	 * space:create_index().
	 */
	struct index_def *user_index_def;
	/** A tuple format for index_def. */
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

	/** Member of env->indexes. */
	struct rlist link;
	/**
	 * Incremented for each change of the range list,
	 * to invalidate iterators.
	 */
	uint32_t version;
	/** Space to which the index belongs. */
	struct space *space;
	/**
	 * column_mask is the bitmask in that bit 'n' is set if
	 * user_index_def parts contains a part with fieldno equal
	 * to 'n'. This mask is used for update optimization
	 * (@sa vy_update).
	 */
	uint64_t column_mask;
};

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
	if (index != NULL && !index->user_index_def->opts.is_unique) {
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
	/** A transaction is aborted or rolled back. */
	VINYL_TX_ROLLBACK
};

struct read_set_key {
	struct tuple *stmt;
	struct vy_tx *tx;
};

typedef rb_tree(struct txv) write_set_t;

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
	/** Transaction start time saved in vy_begin() */
	ev_tstamp start;
	/** Current state of the transaction.*/
	enum tx_state state;
	/**
	 * The transaction is forbidden to commit unless it's read-only.
	 */
	bool is_in_read_view;
	/**
	 * Consistent read view LSN. Originally read-only transactions
	 * receive a read view lsn upon creation and do not see further
	 * changes.
	 * Other transactions are expected to be read-write and
	 * have vlsn == INT64_MAX to read newest data. Once a value read
	 * by such a transaction (T) is overwritten by another
	 * commiting transaction, T permanently goes to read view that does
	 * not see this change.
	 * If T does not have any write statements by the commit time it will
	 * be committed successfully, or aborted as conflicted otherwise.
	 */
	int64_t vlsn;
	union {
		/** The link in read_views of the TX manager */
		struct rlist in_read_views;
	};
	/*
	 * For non-autocommit transactions, the list of open
	 * cursors. When a transaction ends, all open cursors are
	 * forcibly closed.
	 */
	struct rlist cursors;
	struct tx_manager *manager;
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
	/* Index for index_def and index->version */
	struct vy_index *index;
	/** Format to allocate REPLACE and DELETE tuples. */
	struct tuple_format *format;
	/** Format to allocate UPSERT tuples. */
	struct tuple_format *upsert_format;

	/* {{{ Range version checking */
	/* copy of index->version to track range tree changes */
	uint32_t index_version;
	/* current range */
	struct vy_range *curr_range;
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
	bool only_disk;

	/* search options */
	enum iterator_type iterator_type;
	const struct tuple *key;
	const int64_t *vlsn;

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
 * @param only_disk     True, if no need to open vy_mems and tx.
 */
static void
vy_read_iterator_open(struct vy_read_iterator *itr,
		      struct vy_index *index, struct vy_tx *tx,
		      enum iterator_type iterator_type,
		      const struct tuple *key, const int64_t *vlsn,
		      bool only_disk);

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

/**
 * coio task for vinyl page read
 */
struct vy_page_read_task {
	/** parent */
	struct coio_task base;
	/** vinyl page metadata */
	struct vy_page_info page_info;
	/** vy_run with fd - ref. counted */
	struct vy_run *run;
	/** vy_env - contains environment with task mempool */
	struct vy_env *env;
	/** [out] resulting vinyl page */
	struct vy_page *page;
	/** [out] result code */
	int rc;
};

static struct txv *
txv_new(struct vy_index *index, struct tuple *stmt, struct vy_tx *tx)
{
	struct txv *v = malloc(sizeof(struct txv));
	if (unlikely(v == NULL)) {
		diag_set(OutOfMemory, sizeof(struct txv), "malloc",
			 "struct txv");
		return NULL;
	}
	v->index = index;
	v->mem = NULL;
	v->stmt = stmt;
	tuple_ref(stmt);
	v->tx = tx;
	return v;
}

static void
txv_delete(struct txv *v)
{
	tuple_unref(v->stmt);
	free(v);
}

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
	int rc = vy_stmt_compare(a->stmt, b->stmt, &a->index->index_def->key_def);
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
	int rc = vy_stmt_compare(a->stmt, b->stmt, &b->index->index_def->key_def);
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
	 * The list of TXs with a read view in order of vlsn.
	 *
	 */
	struct rlist read_views;
	struct vy_env *env;
};

static int
write_set_cmp(struct txv *a, struct txv *b)
{
	/* Order by index first, by key in the index second. */
	int rc = a->index < b->index ? -1 : a->index > b->index;
	if (rc == 0) {
		struct index_def *index_def = a->index->index_def;
		return vy_stmt_compare(a->stmt, b->stmt, &index_def->key_def);
	}
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
	if (rc == 0) {
		if (a->stmt == NULL) {
			/*
			 * A special key to position search at the
			 * beginning of the index.
			 */
			return -1;
		}
		struct index_def *index_def = a->index->index_def;
		return vy_stmt_compare(a->stmt, b->stmt, &index_def->key_def);
	}
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
	m->env = env;
	rlist_create(&m->read_views);
	return m;
}

static int
tx_manager_delete(struct tx_manager *m)
{
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
	struct vy_tx *lowest = rlist_first_entry(&xm->read_views,
						 struct vy_tx, in_read_views);
	return lowest->vlsn;
}

static struct vy_page_info *
vy_run_page_info(struct vy_run *run, uint32_t pos)
{
	assert(pos < run->info.count);
	return &run->info.page_infos[pos];
}

static uint64_t
vy_run_size(struct vy_run *run)
{
	return run->info.size;
}

static bool
vy_run_is_empty(struct vy_run *run)
{
	return run->info.count == 0;
}

static struct vy_run *
vy_run_new(int64_t id)
{
	struct vy_run *run = (struct vy_run *)malloc(sizeof(struct vy_run));
	if (unlikely(run == NULL)) {
		diag_set(OutOfMemory, sizeof(struct vy_run), "malloc",
			 "struct vy_run");
		return NULL;
	}
	memset(&run->info, 0, sizeof(run->info));
	run->id = id;
	run->fd = -1;
	run->refs = 1;
	rlist_create(&run->in_range);
	TRASH(&run->info.bloom);
	run->info.has_bloom = false;
	return run;
}

static void
vy_run_delete(struct vy_run *run)
{
	assert(run->refs == 0);
	if (run->fd >= 0 && close(run->fd) < 0)
		say_syserror("close failed");
	if (run->info.page_infos != NULL) {
		uint32_t page_no;
		for (page_no = 0; page_no < run->info.count; ++page_no)
			vy_page_info_destroy(run->info.page_infos + page_no);
		free(run->info.page_infos);
	}
	if (run->info.has_bloom)
		bloom_destroy(&run->info.bloom, runtime.quota);
	TRASH(run);
	free(run);
}

/** Increment a run's reference counter. */
static void
vy_run_ref(struct vy_run *run)
{
	assert(run->refs > 0);
	run->refs++;
}

/*
 * Decrement a run's reference counter.
 * Return true if the run was deleted.
 */
static bool
vy_run_unref(struct vy_run *run)
{
	assert(run->refs > 0);
	if (--run->refs == 0) {
		vy_run_delete(run);
		return true;
	}
	return false;
}

enum vy_file_type {
	VY_FILE_INDEX,
	VY_FILE_RUN,
	vy_file_MAX,
};

static const char *vy_file_suffix[] = {
	"index",	/* VY_FILE_INDEX */
	"run",		/* VY_FILE_RUN */
};

/** xlog meta type for .run files */
#define XLOG_META_TYPE_RUN "RUN"

/** xlog meta type for .index files */
#define XLOG_META_TYPE_INDEX "INDEX"

static int
vy_index_snprint_path(char *buf, int size, const char *dir,
		      uint32_t space_id, uint32_t iid)
{
	return snprintf(buf, size, "%s/%u/%u",
			dir, (unsigned)space_id, (unsigned)iid);
}

static int
vy_run_snprint_name(char *buf, int size, int64_t run_id, enum vy_file_type type)
{
	return snprintf(buf, size, "%020lld.%s",
			(long long)run_id, vy_file_suffix[type]);
}

static int
vy_run_snprint_path(char *buf, int size, const char *dir,
		    int64_t run_id, enum vy_file_type type)
{
	int total = 0;
	SNPRINT(total, snprintf, buf, size, "%s/", dir);
	SNPRINT(total, vy_run_snprint_name, buf, size, run_id, type);
	return total;
}

/** Return the path to the run encoded by a metadata log record. */
static int
vy_run_record_snprint_path(char *buf, int size, const char *vinyl_dir,
			   const struct vy_log_record *record,
			   enum vy_file_type type)
{
	assert(record->type == VY_LOG_PREPARE_RUN ||
	       record->type == VY_LOG_INSERT_RUN ||
	       record->type == VY_LOG_DELETE_RUN);

	int total = 0;
	SNPRINT(total, vy_index_snprint_path, buf, size, vinyl_dir,
		record->space_id, record->index_id);
	SNPRINT(total, snprintf, buf, size, "/");
	SNPRINT(total, vy_run_snprint_name, buf, size, record->run_id, type);
	return total;
}

/**
 * Given a record encoding information about a vinyl run, try to
 * delete the corresponding files. On success, write a "forget" record
 * to the log so that all information about the run is deleted on the
 * next log rotation.
 */
static void
vy_run_record_gc(const char *vinyl_dir, const struct vy_log_record *record)
{
	assert(record->type == VY_LOG_PREPARE_RUN ||
	       record->type == VY_LOG_DELETE_RUN);

	ERROR_INJECT(ERRINJ_VY_GC,
		     {say_error("error injection: vinyl run %lld not deleted",
				(long long)record->run_id); return;});

	/* Try to delete files. */
	bool forget = true;
	char path[PATH_MAX];
	for (int type = 0; type < vy_file_MAX; type++) {
		vy_run_record_snprint_path(path, sizeof(path),
					   vinyl_dir, record, type);
		if (coeio_unlink(path) < 0 && errno != ENOENT) {
			say_syserror("failed to delete file '%s'", path);
			forget = false;
		}
	}

	if (!forget)
		return;

	/* Forget the run on success. */
	struct vy_log_record gc_record = {
		.type = VY_LOG_FORGET_RUN,
		.signature = record->signature,
		.run_id = record->run_id,
	};
	vy_log_tx_begin();
	vy_log_write(&gc_record);
	if (vy_log_tx_commit() < 0) {
		say_warn("failed to log vinyl run %lld cleanup: %s",
			 (long long)record->run_id,
			 diag_last_error(diag_get())->errmsg);
	}
}

static void
vy_index_acct_mem(struct vy_index *index, struct vy_mem *mem)
{
	index->used += mem->used;
	index->stmt_count += mem->tree.size;
}

static void
vy_index_unacct_mem(struct vy_index *index, struct vy_mem *mem)
{
	index->used -= mem->used;
	index->stmt_count -= mem->tree.size;
}

static void
vy_index_acct_run(struct vy_index *index, struct vy_run *run)
{
	index->run_count++;
	index->page_count += run->info.count;
	index->stmt_count += run->info.keys;
	index->size += vy_run_size(run);
}

static void
vy_index_unacct_run(struct vy_index *index, struct vy_run *run)
{
	index->run_count--;
	index->page_count -= run->info.count;
	index->stmt_count -= run->info.keys;
	index->size -= vy_run_size(run);
}

static void
vy_index_acct_range(struct vy_index *index, struct vy_range *range)
{
	if (range->mem != NULL)
		vy_index_acct_mem(index, range->mem);
	struct vy_mem *mem;
	rlist_foreach_entry(mem, &range->frozen, in_frozen)
		vy_index_acct_mem(index, mem);
	struct vy_run *run;
	rlist_foreach_entry(run, &range->runs, in_range)
		vy_index_acct_run(index, run);
	histogram_collect(index->run_hist, range->run_count);
}

static void
vy_index_unacct_range(struct vy_index *index, struct vy_range *range)
{
	if (range->mem != NULL)
		vy_index_unacct_mem(index, range->mem);
	struct vy_mem *mem;
	rlist_foreach_entry(mem, &range->frozen, in_frozen)
		vy_index_unacct_mem(index, mem);
	struct vy_run *run;
	rlist_foreach_entry(run, &range->runs, in_range)
		vy_index_unacct_run(index, run);
	histogram_discard(index->run_hist, range->run_count);
}

/** An snprint-style function to print a range's boundaries. */
static int
vy_range_snprint(char *buf, int size, const struct vy_range *range)
{
	int total = 0;
	SNPRINT(total, snprintf, buf, size, "(");
	if (range->begin != NULL)
		SNPRINT(total, vy_key_snprint, buf, size, range->begin);
	else
		SNPRINT(total, snprintf, buf, size, "-inf");
	SNPRINT(total, snprintf, buf, size, "..");
	if (range->end != NULL)
		SNPRINT(total, vy_key_snprint, buf, size, range->end);
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

/** Add a run to the head of a range's list. */
static void
vy_range_add_run(struct vy_range *range, struct vy_run *run)
{
	rlist_add_entry(&range->runs, run, in_range);
	range->run_count++;
	range->size += vy_run_size(run);
}

/** Remove a run from a range's list. */
static void
vy_range_remove_run(struct vy_range *range, struct vy_run *run)
{
	assert(range->run_count > 0);
	assert(range->size >= vy_run_size(run));
	assert(!rlist_empty(&range->runs));
	rlist_del_entry(run, in_range);
	range->run_count--;
	range->size -= vy_run_size(run);
}

/**
 * Allocate a new run for a range and write the information
 * about it to the metadata log so that we could still find
 * and delete it in case a write error occured. This function
 * is called from dump/compaction task constructor.
 */
static int
vy_range_prepare_new_run(struct vy_range *range)
{
	struct vy_index *index = range->index;
	struct vy_run *run = vy_run_new(vy_log_next_run_id());
	if (run == NULL)
		return -1;
	vy_log_tx_begin();
	vy_log_prepare_run(index->index_def->opts.lsn, run->id);
	if (vy_log_tx_commit() < 0) {
		vy_run_unref(run);
		return -1;
	}
	range->new_run = run;
	return 0;
}

/**
 * Free range->new_run and write a record to the metadata
 * log indicating that the run is not needed any more.
 * This function is called on dump/compaction task abort.
 */
static void
vy_range_discard_new_run(struct vy_range *range)
{
	int64_t run_id = range->new_run->id;

	vy_run_unref(range->new_run);
	range->new_run = NULL;

	ERROR_INJECT(ERRINJ_VY_RUN_DISCARD,
		     {say_error("error injection: run %lld not discarded",
				(long long)run_id); return;});

	vy_log_tx_begin();
	vy_log_delete_run(run_id);
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

/**
 * Wait until all in-memory indexes of the given range are unpinned.
 * This is called right before devolving a dump or compaction task
 * on a worker thread to make sure we don't dump an in-memory index
 * which has active transactions.
 */
static void
vy_range_wait_pinned(struct vy_range *range)
{
	struct vy_mem *mem;
	rlist_foreach_entry(mem, &range->frozen, in_frozen)
		vy_mem_wait_pinned(mem);
}

/** Return true if a task was scheduled for a given range. */
static bool
vy_range_is_scheduled(struct vy_range *range)
{
	return range->in_dump.pos == UINT32_MAX;
}

static void
vy_scheduler_add_range(struct vy_scheduler *, struct vy_range *range);
static void
vy_scheduler_update_range(struct vy_scheduler *, struct vy_range *range);
static void
vy_scheduler_remove_range(struct vy_scheduler *, struct vy_range*);
static void
vy_scheduler_mem_dirtied(struct vy_scheduler *scheduler, struct vy_mem *mem);
static void
vy_scheduler_mem_dumped(struct vy_scheduler *scheduler, struct vy_mem *mem);

static int
vy_range_tree_cmp(struct vy_range *a, struct vy_range *b);

static int
vy_range_tree_key_cmp(const struct tuple *a, struct vy_range *b);

rb_gen_ext_key(MAYBE_UNUSED static inline, vy_range_tree_, vy_range_tree_t,
	       struct vy_range, tree_node, vy_range_tree_cmp,
	       const struct tuple *, vy_range_tree_key_cmp);

static void
vy_range_delete(struct vy_range *);

static void
vy_index_ref(struct vy_index *index);

static void
vy_index_unref(struct vy_index *index);

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
	struct index_def *index_def = range_a->index->index_def;
	return key_compare(range_a->begin, range_b->begin, &index_def->key_def);
}

static int
vy_range_tree_key_cmp(const struct tuple *stmt, struct vy_range *range)
{
	/* Any key > -inf. */
	if (range->begin == NULL)
		return 1;

	struct index_def *index_def = range->index->index_def;
	return vy_stmt_compare_with_raw_key(stmt, range->begin, &index_def->key_def);
}

static void
vy_index_delete(struct vy_index *index);

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
 * This function only takes into account the actual range tree layout
 * and does not handle the split case, when a range being split
 * is replaced by new ranges back-pointing to it via range->shadow.
 * Therefore, as is, this function is only suitable for handling
 * insertions, which always go to in-memory indexes of ranges found in
 * the range tree. Select operations have to check range->shadow to
 * guarantee that no keys are skipped no matter if there is a
 * split operation in progress (see vy_range_iterator_next()).
 */
static struct vy_range *
vy_range_tree_find_by_key(vy_range_tree_t *tree,
			  enum iterator_type iterator_type,
			  struct index_def *index_def, const struct tuple *key)
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
		    key_field_count < index_def->key_def.part_count &&
		    vy_stmt_compare_with_raw_key(key, range->begin,
						 &index_def->key_def) == 0)
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
			    vy_stmt_compare_with_raw_key(key, range->begin,
							 &index_def->key_def) != 0) {
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
 * This function is supposed to be used for iterating over a subset of
 * keys in an index. Therefore it should handle the split case,
 * i.e. check if the range returned by vy_range_tree_find_by_key()
 * is a replacement range and return a pointer to the range being
 * split if it is.
 */
static void
vy_range_iterator_next(struct vy_range_iterator *itr, struct vy_range **result)
{
	struct vy_range *curr = itr->curr_range;
	struct vy_range *next;
	struct vy_index *index = itr->index;
	struct index_def *def = index->index_def;

	if (curr == NULL) {
		/* First iteration */
		if (unlikely(index->range_count == 1))
			next = vy_range_tree_first(&index->tree);
		else
			next = vy_range_tree_find_by_key(&index->tree,
							 itr->iterator_type,
							 def, itr->key);
		goto check;
	}
next:
	switch (itr->iterator_type) {
	case ITER_LT:
	case ITER_LE:
		next = vy_range_tree_prev(&index->tree, curr);
		break;
	case ITER_GT:
	case ITER_GE:
		next = vy_range_tree_next(&index->tree, curr);
		break;
	case ITER_EQ:
		if (curr->end != NULL &&
		    vy_stmt_compare_with_raw_key(itr->key, curr->end,
						 &def->key_def) >= 0) {
			/* A partial key can be found in more than one range. */
			next = vy_range_tree_next(&index->tree, curr);
		} else {
			next = NULL;
		}
		break;
	default:
		unreachable();
	}
check:
	/*
	 * When range split starts, the selected range is replaced with
	 * several new ranges, each of which has ->shadow pointing to
	 * the original range (see vy_task_split_new()). New ranges
	 * must not be read from until split has finished, because they
	 * only contain in-memory data added after split was initiated,
	 * while on-disk runs and older in-memory indexes are still
	 * linked to the original range. So whenever we encounter such
	 * a range we return ->shadow instead, and assume that the
	 * caller will handle it (as vy_read_iterator_add_mem() does).
	 * Note, we have to be careful not to return the same range
	 * twice.
	 */
	if (next != NULL && next->shadow != NULL) {
		if (curr != NULL && curr->shadow == next->shadow) {
			curr = next;
			goto next;
		}
		*result = next->shadow;
	} else {
		*result = next;
	}
	itr->curr_range = next;
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
	struct vy_range *curr = vy_range_tree_find_by_key(&index->tree,
				itr->iterator_type, index->index_def,
				last_stmt != NULL ? last_stmt : itr->key);
	itr->curr_range = curr;
	*result = curr->shadow != NULL ? curr->shadow : curr;
}

static void
vy_index_add_range(struct vy_index *index, struct vy_range *range)
{
	vy_range_tree_insert(&index->tree, range);
	index->range_count++;
}

static void
vy_index_remove_range(struct vy_index *index, struct vy_range *range)
{
	vy_range_tree_remove(&index->tree, range);
	index->range_count--;
}

/*
 * Check if a is left-adjacent to b, i.e. a->end == b->begin.
 */
static bool
vy_range_is_adjacent(struct vy_range *a, struct vy_range *b,
		     struct index_def *index_def)
{
	if (a->end == NULL || b->begin == NULL)
		return false;
	assert(a->index == b->index);
	return key_compare(a->end, b->begin, &index_def->key_def) == 0;
}

/* dump statement to the run page buffers (stmt header and data) */
static int
vy_run_dump_stmt(struct tuple *value, struct xlog *data_xlog,
		 struct vy_page_info *info, const struct index_def *index_def)
{
	struct region *region = &fiber()->gc;
	size_t used = region_used(region);

	struct xrow_header xrow;
	if (vy_stmt_encode(value, &index_def->key_def,
			   index_def->space_id, index_def->iid,
			   &xrow) != 0)
		return -1;

	ssize_t row_size;
	if ((row_size = xlog_write_row(data_xlog, &xrow)) < 0)
		return -1;

	region_truncate(region, used);

	info->unpacked_size += row_size;

	++info->count;
	if (vy_stmt_lsn(value) > info->max_lsn)
		info->max_lsn = vy_stmt_lsn(value);
	if (vy_stmt_lsn(value) < info->min_lsn)
		info->min_lsn = vy_stmt_lsn(value);
	return 0;
}

struct vy_write_iterator;

static struct vy_write_iterator *
vy_write_iterator_new(struct vy_index *index, bool is_last_level,
		      int64_t oldest_vlsn);
static NODISCARD int
vy_write_iterator_add_run(struct vy_write_iterator *wi, struct vy_run *run);
static NODISCARD int
vy_write_iterator_add_mem(struct vy_write_iterator *wi, struct vy_mem *mem);
static NODISCARD int
vy_write_iterator_next(struct vy_write_iterator *wi, struct tuple **ret);

/**
 * Delete the iterator and free resources.
 * Can be called only after cleanup().
 */
static void
vy_write_iterator_delete(struct vy_write_iterator *wi);

/**
 * Free all resources allocated in a worker thread.
 */
static void
vy_write_iterator_cleanup(struct vy_write_iterator *wi);

/**
 * Initialize page info struct
 *
 * @retval 0 for Success
 * @retval -1 for error
 */
static int
vy_page_info_create(struct vy_page_info *page_info, uint64_t offset,
		    const struct index_def *index_def, struct tuple *min_stmt)
{
	memset(page_info, 0, sizeof(*page_info));
	page_info->min_lsn = INT64_MAX;
	page_info->offset = offset;
	page_info->unpacked_size = 0;
	struct region *region = &fiber()->gc;
	size_t used = region_used(region);
	uint32_t size;
	const char *region_key = tuple_extract_key(min_stmt,
				&index_def->key_def, &size);
	if (region_key == NULL)
		return -1;
	page_info->min_key = vy_key_dup(region_key);
	region_truncate(region, used);
	return page_info->min_key == NULL ? -1 : 0;
}

/**
 * Destroy page info struct
 */
static void
vy_page_info_destroy(struct vy_page_info *page_info)
{
	if (page_info->min_key != NULL)
		free(page_info->min_key);
}

/**
 * Encode uint32_t array of row offsets (a page index) as xrow
 *
 * @param page_index row index
 * @param count size of row index
 * @param[out] xrow xrow to fill.
 * @retval 0 for success
 * @retval -1 for error
 */
static int
vy_page_index_encode(const uint32_t *page_index, uint32_t count,
		    struct xrow_header *xrow)
{
	memset(xrow, 0, sizeof(*xrow));
	xrow->type = VY_RUN_PAGE_INDEX;

	size_t size = mp_sizeof_map(1) +
		      mp_sizeof_uint(VY_PAGE_INDEX_INDEX) +
		      mp_sizeof_bin(sizeof(uint32_t) * count);
	char *pos = region_alloc(&fiber()->gc, size);
	if (pos == NULL) {
		diag_set(OutOfMemory, size, "region", "row index");
		return -1;
	}
	xrow->body->iov_base = pos;
	pos = mp_encode_map(pos, 1);
	pos = mp_encode_uint(pos, VY_PAGE_INDEX_INDEX);
	pos = mp_encode_binl(pos, sizeof(uint32_t) * count);
	for (uint32_t i = 0; i < count; ++i)
		pos = mp_store_u32(pos, page_index[i]);
	xrow->body->iov_len = (void *)pos - xrow->body->iov_base;
	assert(xrow->body->iov_len == size);
	xrow->bodycnt = 1;
	return 0;
}

/**
 * Write statements from the iterator to a new page in the run,
 * update page and run statistics.
 *
 *  @retval  1 all is ok, the iterator is finished
 *  @retval  0 all is ok, the iterator isn't finished
 *  @retval -1 error occurred
 */
static int
vy_run_write_page(struct vy_run_info *run_info, struct xlog *data_xlog,
		  struct vy_write_iterator *wi, const char *split_key,
		  uint32_t *page_info_capacity, struct bloom_spectrum *bs,
		  struct tuple **curr_stmt, const struct index_def *index_def,
		  const struct index_def *user_index_def, const char **max_key)
{
	assert(curr_stmt != NULL);
	assert(*curr_stmt != NULL);

	/* row offsets accumulator */
	struct ibuf page_index_buf;
	ibuf_create(&page_index_buf, &cord()->slabc, sizeof(uint32_t) * 4096);

	if (run_info->count >= *page_info_capacity) {
		uint32_t cap = *page_info_capacity > 0 ?
			*page_info_capacity * 2 : 16;
		struct vy_page_info *new_infos =
			realloc(run_info->page_infos, cap * sizeof(*new_infos));
		if (new_infos == NULL) {
			diag_set(OutOfMemory, cap, "realloc",
				 "struct vy_page_info");
			goto error_page_index;
		}
		run_info->page_infos = new_infos;
		*page_info_capacity = cap;
	}
	assert(*page_info_capacity >= run_info->count);

	struct vy_page_info *page = run_info->page_infos + run_info->count;
	vy_page_info_create(page, data_xlog->offset, index_def, *curr_stmt);
	bool end_of_run = false;
	xlog_tx_begin(data_xlog);
	struct tuple *stmt = NULL;

	do {
		uint32_t *offset = (uint32_t *) ibuf_alloc(&page_index_buf,
							   sizeof(uint32_t));
		if (offset == NULL) {
			diag_set(OutOfMemory, sizeof(uint32_t),
				 "ibuf", "row index");
			goto error_rollback;
		}
		*offset = page->unpacked_size;

		if (stmt != NULL)
			tuple_unref(stmt);
		stmt = *curr_stmt;
		tuple_ref(stmt);
		if (vy_run_dump_stmt(stmt, data_xlog, page, index_def) != 0)
			goto error_rollback;
		bloom_spectrum_add(bs, tuple_hash(stmt, &user_index_def->key_def));

		if (vy_write_iterator_next(wi, curr_stmt))
			goto error_rollback;

		end_of_run = *curr_stmt == NULL ||
			/* Split key reached, proceed to the next run. */
			(split_key != NULL &&
			 vy_tuple_compare_with_raw_key(*curr_stmt, split_key,
						       &index_def->key_def) >= 0);
	} while (end_of_run == false &&
		 obuf_size(&data_xlog->obuf) < (uint64_t)index_def->opts.page_size);

	if (end_of_run && stmt != NULL && max_key != NULL) {
		/*
		 * Tuple_extract_key allocates the key on a
		 * region, but the max_key must be allocated on
		 * the heap, because the max_key can live longer
		 * than a fiber. To reach this, we must copy the
		 * key into malloced memory.
		 */
		*max_key = tuple_extract_key(stmt, &index_def->key_def, NULL);
		tuple_unref(stmt);
		if (*max_key == NULL)
			goto error_rollback;
		stmt = NULL;
		*max_key = vy_key_dup(*max_key);
		if (*max_key == NULL)
			goto error_rollback;
	}
	if (stmt != NULL)
		tuple_unref(stmt);
	/* Save offset to row index  */
	page->page_index_offset = page->unpacked_size;

	/* Write row index */
	struct xrow_header xrow;
	const uint32_t *page_index = (const uint32_t *) page_index_buf.rpos;
	assert(ibuf_used(&page_index_buf) == sizeof(uint32_t) * page->count);
	if (vy_page_index_encode(page_index, page->count, &xrow) < 0)
		goto error_rollback;

	ssize_t written = xlog_write_row(data_xlog, &xrow);
	if (written < 0)
		goto error_rollback;

	page->unpacked_size += written;

	written = xlog_tx_commit(data_xlog);
	if (written == 0)
		written = xlog_flush(data_xlog);
	if (written < 0)
		goto error_page_index;

	page->size = written;

	assert(page->count > 0);

	++run_info->count;
	if (page->min_lsn < run_info->min_lsn)
		run_info->min_lsn = page->min_lsn;
	if (page->max_lsn > run_info->max_lsn)
		run_info->max_lsn = page->max_lsn;
	run_info->size += page->size;
	run_info->keys += page->count;

	ibuf_destroy(&page_index_buf);
	return !end_of_run ? 0: 1;

error_rollback:
	xlog_tx_rollback(data_xlog);
error_page_index:
	ibuf_destroy(&page_index_buf);
	return -1;
}

/**
 * Write statements from the iterator to a new run file.
 *
 *  @retval 0, curr_stmt != NULL: all is ok, the iterator is not finished
 *  @retval 0, curr_stmt == NULL: all is ok, the iterator finished
 *  @retval -1 error occurred
 */
static int
vy_run_write_data(struct vy_run *run, const char *dirpath,
		  struct vy_write_iterator *wi, struct tuple **curr_stmt,
		  const char *end_key, struct bloom_spectrum *bs,
		  const struct index_def *index_def,
		  const struct index_def *user_index_def, const char **max_key)
{
	assert(curr_stmt != NULL);
	assert(*curr_stmt != NULL);

	struct vy_run_info *run_info = &run->info;

	char path[PATH_MAX];
	vy_run_snprint_path(path, sizeof(path), dirpath,
			    run->id, VY_FILE_RUN);
	struct xlog data_xlog;
	struct xlog_meta meta = {
		.filetype = XLOG_META_TYPE_RUN,
		.instance_uuid = INSTANCE_UUID,
	};
	if (xlog_create(&data_xlog, path, &meta) < 0)
		return -1;

	/*
	 * Read from the iterator until it's exhausted or
	 * the split key is reached.
	 */
	run_info->min_lsn = INT64_MAX;
	assert(run_info->page_infos == NULL);
	uint32_t page_infos_capacity = 0;
	int rc;
	do {
		rc = vy_run_write_page(run_info, &data_xlog, wi,
				       end_key, &page_infos_capacity, bs,
				       curr_stmt, index_def, user_index_def,
				       max_key);
		if (rc < 0)
			goto err;
		fiber_gc();
	} while (rc == 0);

	/* Sync data and link the file to the final name. */
	if (xlog_sync(&data_xlog) < 0 ||
	    xlog_rename(&data_xlog) < 0)
		goto err;

	run->fd = data_xlog.fd;
	xlog_close(&data_xlog, true);
	fiber_gc();

	return 0;
err:
	xlog_close(&data_xlog, false);
	fiber_gc();
	return -1;
}

/** {{{ vy_page_info */

const uint64_t vy_page_info_key_map = (1 << VY_PAGE_INFO_OFFSET) |
				      (1 << VY_PAGE_INFO_SIZE) |
				      (1 << VY_PAGE_INFO_UNPACKED_SIZE) |
				      (1 << VY_PAGE_INFO_ROW_COUNT) |
				      (1 << VY_PAGE_INFO_MIN_KEY) |
				      (1 << VY_PAGE_INFO_PAGE_INDEX_OFFSET);

/**
 * Encode vy_page_info as xrow.
 * Allocates using region_alloc.
 *
 * @param page_info page information to encode
 * @param[out] xrow xrow to fill
 *
 * @retval  0 success
 * @retval -1 error, check diag
 */
static int
vy_page_info_encode(const struct vy_page_info *page_info,
		    struct xrow_header *xrow)
{
	struct region *region = &fiber()->gc;

	uint32_t min_key_size;
	const char *tmp = page_info->min_key;
	assert(mp_typeof(*tmp) == MP_ARRAY);
	mp_next(&tmp);
	min_key_size = tmp - page_info->min_key;

	/* calc tuple size */
	uint32_t size;
	/* 3 items: page offset, size, and map */
	size = mp_sizeof_map(6) +
	       mp_sizeof_uint(VY_PAGE_INFO_OFFSET) +
	       mp_sizeof_uint(page_info->offset) +
	       mp_sizeof_uint(VY_PAGE_INFO_SIZE) +
	       mp_sizeof_uint(page_info->size) +
	       mp_sizeof_uint(VY_PAGE_INFO_ROW_COUNT) +
	       mp_sizeof_uint(page_info->count) +
	       mp_sizeof_uint(VY_PAGE_INFO_MIN_KEY) +
	       min_key_size +
	       mp_sizeof_uint(VY_PAGE_INFO_UNPACKED_SIZE) +
	       mp_sizeof_uint(page_info->unpacked_size) +
	       mp_sizeof_uint(VY_PAGE_INFO_PAGE_INDEX_OFFSET) +
	       mp_sizeof_uint(page_info->page_index_offset);

	char *pos = region_alloc(region, size);
	if (pos == NULL) {
		diag_set(OutOfMemory, size, "region", "page encode");
		return -1;
	}

	memset(xrow, 0, sizeof(*xrow));
	/* encode page */
	xrow->body->iov_base = pos;
	pos = mp_encode_map(pos, 6);
	pos = mp_encode_uint(pos, VY_PAGE_INFO_OFFSET);
	pos = mp_encode_uint(pos, page_info->offset);
	pos = mp_encode_uint(pos, VY_PAGE_INFO_SIZE);
	pos = mp_encode_uint(pos, page_info->size);
	pos = mp_encode_uint(pos, VY_PAGE_INFO_ROW_COUNT);
	pos = mp_encode_uint(pos, page_info->count);
	pos = mp_encode_uint(pos, VY_PAGE_INFO_MIN_KEY);
	memcpy(pos, page_info->min_key, min_key_size);
	pos += min_key_size;
	pos = mp_encode_uint(pos, VY_PAGE_INFO_UNPACKED_SIZE);
	pos = mp_encode_uint(pos, page_info->unpacked_size);
	pos = mp_encode_uint(pos, VY_PAGE_INFO_PAGE_INDEX_OFFSET);
	pos = mp_encode_uint(pos, page_info->page_index_offset);
	xrow->body->iov_len = (void *)pos - xrow->body->iov_base;
	xrow->bodycnt = 1;

	xrow->type = VY_INDEX_PAGE_INFO;
	return 0;
}

/**
 * Decode page information from xrow.
 *
 * @param[out] page Page information.
 * @param xrow      Xrow to decode.
 *
 * @retval  0 Success.
 * @retval -1 Error.
 */
static int
vy_page_info_decode(struct vy_page_info *page, const struct xrow_header *xrow)
{
	assert(xrow->type == VY_INDEX_PAGE_INFO);
	const char *pos = xrow->body->iov_base;
	memset(page, 0, sizeof(*page));
	uint64_t key_map = vy_page_info_key_map;
	uint32_t map_size = mp_decode_map(&pos);
	uint32_t map_item;
	const char *key_beg;
	for (map_item = 0; map_item < map_size; ++map_item) {
		uint32_t key = mp_decode_uint(&pos);
		key_map &= ~(1 << key);
		switch (key) {
		case VY_PAGE_INFO_OFFSET:
			page->offset = mp_decode_uint(&pos);
			break;
		case VY_PAGE_INFO_SIZE:
			page->size = mp_decode_uint(&pos);
			break;
		case VY_PAGE_INFO_ROW_COUNT:
			page->count = mp_decode_uint(&pos);
			break;
		case VY_PAGE_INFO_MIN_KEY:
			key_beg = pos;
			mp_next(&pos);
			page->min_key = vy_key_dup(key_beg);
			if (page->min_key == NULL)
				return -1;
			break;
		case VY_PAGE_INFO_UNPACKED_SIZE:
			page->unpacked_size = mp_decode_uint(&pos);
			break;
		case VY_PAGE_INFO_PAGE_INDEX_OFFSET:
			page->page_index_offset = mp_decode_uint(&pos);
			break;
		default: {
				char errmsg[512];
				snprintf(errmsg, sizeof(errmsg), "%s %d",
					"Can't decode page info: unknown key ",
					key);
				diag_set(ClientError, ER_VINYL, errmsg);
			}
			return -1;
		}
	}
	if (key_map) {
		enum vy_page_info_key key = bit_ctz_u64(key_map);
		diag_set(ClientError, ER_MISSING_REQUEST_FIELD,
			 vy_page_info_key_name(key));
		return -1;
	}

	return 0;
}

/** vy_page_info }}} */

/** {{{ vy_run_info */

const uint64_t vy_run_info_key_map = (1 << VY_RUN_INFO_MIN_LSN) |
				     (1 << VY_RUN_INFO_MAX_LSN) |
				     (1 << VY_RUN_INFO_PAGE_COUNT);

enum { VY_BLOOM_VERSION = 0 };

static size_t
vy_run_bloom_encode_size(const struct bloom *bloom)
{
	size_t size = mp_sizeof_array(4);
	size += mp_sizeof_uint(VY_BLOOM_VERSION); /* version */
	size += mp_sizeof_uint(bloom->table_size);
	size += mp_sizeof_uint(bloom->hash_count);
	size += mp_sizeof_bin(bloom_store_size(bloom));
	return size;
}

char *
vy_run_bloom_encode(char *buffer, const struct bloom *bloom)
{
	char *pos = buffer;
	pos = mp_encode_array(pos, 4);
	pos = mp_encode_uint(pos, VY_BLOOM_VERSION);
	pos = mp_encode_uint(pos, bloom->table_size);
	pos = mp_encode_uint(pos, bloom->hash_count);
	pos = mp_encode_binl(pos, bloom_store_size(bloom));
	pos = bloom_store(bloom, pos);
	return pos;
}

int
vy_run_bloom_decode(const char **buffer, struct bloom *bloom)
{
	const char **pos = buffer;
	memset(bloom, 0, sizeof(*bloom));
	uint32_t array_size = mp_decode_array(pos);
	if (array_size != 4) {
		diag_set(ClientError, ER_VINYL, "Can't decode bloom meta: "
			"wrong size of an array");
		return -1;
	}
	uint64_t version = mp_decode_uint(pos);
	if (version != VY_BLOOM_VERSION) {
		diag_set(ClientError, ER_VINYL, "Can't decode bloom meta: "
			"wrong version");
		return -1;
	}
	bloom->table_size = mp_decode_uint(pos);
	bloom->hash_count = mp_decode_uint(pos);
	size_t table_size = mp_decode_binl(pos);
	if (table_size != bloom_store_size(bloom)) {
		diag_set(ClientError, ER_VINYL, "Can't decode bloom meta: "
			"wrong size of a table");
		return -1;
	}
	if (bloom_load_table(bloom, *pos, runtime.quota) != 0) {
		diag_set(ClientError, ER_VINYL, "Can't decode bloom meta: "
			"alloc failed");
		return -1;
	}
	*pos += table_size;
	return 0;
}

/**
 * Encode vy_run_info as xrow
 * Allocates using region alloc
 *
 * @param run_info the run information
 * @param xrow xrow to fill.
 *
 * @retval  0 success
 * @retval -1 on error, check diag
 */
static int
vy_run_info_encode(const struct vy_run_info *run_info,
		   struct xrow_header *xrow)
{
	assert(run_info->has_bloom);
	size_t size = mp_sizeof_map(4);
	size += mp_sizeof_uint(VY_RUN_INFO_MIN_LSN) +
		mp_sizeof_uint(run_info->min_lsn);
	size += mp_sizeof_uint(VY_RUN_INFO_MAX_LSN) +
		mp_sizeof_uint(run_info->max_lsn);
	size += mp_sizeof_uint(VY_RUN_INFO_PAGE_COUNT) +
		mp_sizeof_uint(run_info->count);
	size += mp_sizeof_uint(VY_RUN_INFO_BLOOM) +
		vy_run_bloom_encode_size(&run_info->bloom);

	char *pos = region_alloc(&fiber()->gc, size);
	if (pos == NULL) {
		diag_set(OutOfMemory, size, "region", "run encode");
		return -1;
	}
	memset(xrow, 0, sizeof(*xrow));
	xrow->body->iov_base = pos;
	/* encode values */
	pos = mp_encode_map(pos, 4);
	pos = mp_encode_uint(pos, VY_RUN_INFO_MIN_LSN);
	pos = mp_encode_uint(pos, run_info->min_lsn);
	pos = mp_encode_uint(pos, VY_RUN_INFO_MAX_LSN);
	pos = mp_encode_uint(pos, run_info->max_lsn);
	pos = mp_encode_uint(pos, VY_RUN_INFO_PAGE_COUNT);
	pos = mp_encode_uint(pos, run_info->count);
	pos = mp_encode_uint(pos, VY_RUN_INFO_BLOOM);
	pos = vy_run_bloom_encode(pos, &run_info->bloom);
	xrow->body->iov_len = (void *)pos - xrow->body->iov_base;
	xrow->bodycnt = 1;
	xrow->type = VY_INDEX_RUN_INFO;
	xrow->lsn = run_info->min_lsn;
	return 0;
}

/**
 * Decode the run metadata from xrow.
 *
 * @param xrow xrow to decode
 * @param[out] run_info the run information
 *
 * @retval  0 success
 * @retval -1 error (check diag)
 */
static int
vy_run_info_decode(struct vy_run_info *run_info,
		   const struct xrow_header *xrow)
{
	assert(xrow->type == VY_INDEX_RUN_INFO);
	/* decode run */
	const char *pos = xrow->body->iov_base;
	memset(run_info, 0, sizeof(*run_info));
	uint64_t key_map = vy_run_info_key_map;
	uint32_t map_size = mp_decode_map(&pos);
	uint32_t map_item;
	/* decode run values */
	for (map_item = 0; map_item < map_size; ++map_item) {
		uint32_t key = mp_decode_uint(&pos);
		key_map &= ~(1 << key);
		switch (key) {
		case VY_RUN_INFO_MIN_LSN:
			run_info->min_lsn = mp_decode_uint(&pos);
			break;
		case VY_RUN_INFO_MAX_LSN:
			run_info->max_lsn = mp_decode_uint(&pos);
			break;
		case VY_RUN_INFO_PAGE_COUNT:
			run_info->count = mp_decode_uint(&pos);
			break;
		case VY_RUN_INFO_BLOOM:
			if (vy_run_bloom_decode(&pos, &run_info->bloom) == 0)
				run_info->has_bloom = true;
			else
				return -1;
			break;
		default:
			diag_set(ClientError, ER_VINYL,
				 "Unknown run meta key %d", key);
			return -1;
		}
	}
	if (key_map) {
		enum vy_run_info_key key = bit_ctz_u64(key_map);
		diag_set(ClientError, ER_MISSING_REQUEST_FIELD,
			 vy_run_info_key_name(key));
		return -1;
	}
	return 0;
}

/* vy_run_info }}} */

/**
 * Write run index to file.
 */
static int
vy_run_write_index(struct vy_run *run, const char *dirpath)
{
	char path[PATH_MAX];
	vy_run_snprint_path(path, sizeof(path), dirpath,
			    run->id, VY_FILE_INDEX);

	struct xlog index_xlog;
	struct xlog_meta meta = {
		.filetype = XLOG_META_TYPE_INDEX,
		.instance_uuid = INSTANCE_UUID,
	};
	if (xlog_create(&index_xlog, path, &meta) < 0)
		return -1;

	xlog_tx_begin(&index_xlog);

	struct xrow_header xrow;
	if (vy_run_info_encode(&run->info, &xrow) != 0 ||
	    xlog_write_row(&index_xlog, &xrow) < 0)
		goto fail;

	for (uint32_t page_no = 0; page_no < run->info.count; ++page_no) {
		struct vy_page_info *page_info = vy_run_page_info(run, page_no);
		if (vy_page_info_encode(page_info, &xrow) < 0) {
			goto fail;
		}
		if (xlog_write_row(&index_xlog, &xrow) < 0)
			goto fail;
	}

	if (xlog_tx_commit(&index_xlog) < 0 ||
	    xlog_flush(&index_xlog) < 0 ||
	    xlog_rename(&index_xlog) < 0)
		goto fail;
	xlog_close(&index_xlog, false);
	fiber_gc();
	return 0;
fail:
	fiber_gc();
	xlog_tx_rollback(&index_xlog);
	xlog_close(&index_xlog, false);
	unlink(path);
	return -1;
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
	     const char *begin, const char *end)
{
	struct tx_manager *xm = index->env->xm;
	struct lsregion *allocator = &index->env->allocator;
	const int64_t *allocator_lsn = &xm->lsn;

	struct vy_range *range = (struct vy_range*) calloc(1, sizeof(*range));
	if (range == NULL) {
		diag_set(OutOfMemory, sizeof(struct vy_range), "malloc",
			 "struct vy_range");
		goto fail;
	}
	range->mem = vy_mem_new(allocator, allocator_lsn,
				index->index_def, index->space_format,
				index->space_format_with_colmask,
				index->upsert_format, sc_version);
	if (range->mem == NULL)
		goto fail_mem;
	/* Allocate a new id unless specified. */
	range->id = (id >= 0 ? id : vy_log_next_range_id());
	range->is_level_zero = true;
	if (begin != NULL) {
		range->begin = vy_key_dup(begin);
		if (range->begin == NULL)
			goto fail_begin;
	}
	if (end != NULL) {
		range->end = vy_key_dup(end);
		if (range->end == NULL)
			goto fail_end;
	}
	rlist_create(&range->runs);
	rlist_create(&range->frozen);
	range->min_lsn = INT64_MAX;
	range->index = index;
	range->in_dump.pos = UINT32_MAX;
	range->in_compact.pos = UINT32_MAX;
	rlist_create(&range->split_list);
	return range;
fail_end:
	if (range->begin != NULL)
		free(range->begin);
fail_begin:
	vy_mem_delete(range->mem);
fail_mem:
	free(range);
fail:
	return NULL;
}

static int
vy_run_recover(struct vy_run *run, const char *dir)
{
	char path[PATH_MAX];
	vy_run_snprint_path(path, sizeof(path), dir, run->id, VY_FILE_INDEX);
	struct xlog_cursor cursor;
	if (xlog_cursor_open(&cursor, path))
		goto fail;

	struct xlog_meta *meta = &cursor.meta;
	if (strcmp(meta->filetype, XLOG_META_TYPE_INDEX) != 0) {
		diag_set(ClientError, ER_INVALID_XLOG_TYPE,
			 XLOG_META_TYPE_INDEX, meta->filetype);
		goto fail_close;
	}

	/* Read run header. */
	struct xrow_header xrow;
	/* all rows should be in one tx */
	if ((xlog_cursor_next_tx(&cursor)) != 0 ||
	    (xlog_cursor_next_row(&cursor, &xrow)) != 0) {
		diag_set(ClientError, ER_VINYL, "Invalid .index file");
		goto fail_close;
	}

	if (xrow.type != VY_INDEX_RUN_INFO) {
		diag_set(ClientError, ER_VINYL, "Invalid run info type");
		return -1;
	}
	if (vy_run_info_decode(&run->info, &xrow) != 0)
		goto fail_close;

	/* Allocate buffer for page info. */
	run->info.page_infos = calloc(run->info.count,
				      sizeof(struct vy_page_info));
	if (run->info.page_infos == NULL) {
		diag_set(OutOfMemory,
			 run->info.count * sizeof(struct vy_page_info),
			 "malloc", "struct vy_page_info");
		goto fail_close;
	}

	for (uint32_t page_no = 0; page_no < run->info.count; page_no++) {
		int rc = xlog_cursor_next_row(&cursor, &xrow);
		if (rc != 0) {
			if (rc > 0) {
				/** To few pages in file */
				diag_set(ClientError, ER_VINYL,
					 "Too few pages in run meta file");
			}
			/*
			 * Limit the count of pages to
			 * successfully created pages.
			 */
			run->info.count = page_no;
			goto fail_close;
		}
		if (xrow.type != VY_INDEX_PAGE_INFO) {
			diag_set(ClientError, ER_VINYL, "Invalid page info type");
			goto fail_close;
		}
		struct vy_page_info *page = run->info.page_infos + page_no;
		if (vy_page_info_decode(page, &xrow) < 0) {
			/**
			 * Limit the count of pages to successfully
			 * created pages
			 */
			run->info.count = page_no;
			goto fail_close;
		}
		run->info.size += page->size;
		run->info.keys += page->count;
	}

	/* We don't need to keep metadata file open any longer. */
	xlog_cursor_close(&cursor, false);

	/* Prepare data file for reading. */
	vy_run_snprint_path(path, sizeof(path), dir, run->id, VY_FILE_RUN);
	if (xlog_cursor_open(&cursor, path))
		goto fail;
	meta = &cursor.meta;
	if (strcmp(meta->filetype, XLOG_META_TYPE_RUN) != 0) {
		diag_set(ClientError, ER_INVALID_XLOG_TYPE,
			 XLOG_META_TYPE_RUN, meta->filetype);
		goto fail_close;
	}
	run->fd = cursor.fd;
	xlog_cursor_close(&cursor, true);
	return 0;

fail_close:
	xlog_cursor_close(&cursor, false);
fail:
	return -1;
}

/* Move the active in-memory index of a range to the frozen list. */
static void
vy_range_freeze_mem(struct vy_range *range)
{
	assert(range->mem != NULL);
	rlist_add_entry(&range->frozen, range->mem, in_frozen);
	range->mem = NULL;
}

/* Activate the newest frozen in-memory index of a range. */
static void
vy_range_unfreeze_mem(struct vy_range *range)
{
	assert(range->mem == NULL);
	assert(!rlist_empty(&range->frozen));
	range->mem = rlist_shift_entry(&range->frozen,
				       struct vy_mem, in_frozen);
}

/**
 * Allocate a new active in-memory index for a range while moving
 * the old one to the frozen list. Used by dump/compaction in order
 * not to bother about synchronization with concurrent insertions
 * while a range is being dumped. If the active in-memory index is
 * empty and not pinned by an ongoing transaction, we don't need
 * to dump it and therefore can delete it right away.
 */
static int
vy_range_rotate_mem(struct vy_range *range)
{
	struct vy_index *index = range->index;
	struct lsregion *allocator = &index->env->allocator;
	const int64_t *allocator_lsn = &index->env->xm->lsn;
	struct vy_mem *mem;

	assert(range->mem != NULL);
	mem = vy_mem_new(allocator, allocator_lsn,
			 index->index_def, index->space_format,
			 index->space_format_with_colmask,
			 index->upsert_format, sc_version);
	if (mem == NULL)
		return -1;
	if (range->mem->used > 0 || range->mem->pin_count > 0)
		vy_range_freeze_mem(range);
	else
		vy_mem_delete(range->mem);
	range->mem = mem;
	range->version++;
	return 0;
}

/**
 * Delete frozen in-memory trees created at <= @dump_lsn and notify
 * the scheduler. Called after successful dump or compaction.
 */
static void
vy_range_dump_mems(struct vy_range *range, struct vy_scheduler *scheduler,
		   int64_t dump_lsn)
{
	struct vy_mem *mem, *tmp;

	range->used = range->mem->used;
	range->min_lsn = range->mem->min_lsn;
	rlist_foreach_entry_safe(mem, &range->frozen, in_frozen, tmp) {
		if (mem->min_lsn <= dump_lsn) {
			rlist_del_entry(mem, in_frozen);
			vy_scheduler_mem_dumped(scheduler, mem);
			vy_mem_delete(mem);
		} else {
			range->used += mem->used;
			range->min_lsn = MIN(range->min_lsn, mem->min_lsn);
		}
	}
}

static void
vy_range_delete(struct vy_range *range)
{
	/* The range has been deleted from the scheduler queues. */
	assert(range->in_dump.pos == UINT32_MAX);
	assert(range->in_compact.pos == UINT32_MAX);

	if (range->begin)
		free(range->begin);
	if (range->end)
		free(range->end);

	/* Delete all runs. */
	if (range->new_run != NULL)
		vy_run_unref(range->new_run);
	while (!rlist_empty(&range->runs)) {
		struct vy_run *run = rlist_shift_entry(&range->runs,
						       struct vy_run, in_range);
		vy_run_unref(run);
	}
	/* Delete all mems. */
	if (range->mem != NULL)
		vy_mem_delete(range->mem);
	while (!rlist_empty(&range->frozen)) {
		struct vy_mem *mem;
		mem = rlist_shift_entry(&range->frozen,
					struct vy_mem, in_frozen);
		vy_mem_delete(mem);
	}

	TRASH(range);
	free(range);
}

/**
 * Create a write iterator to dump in-memory indexes.
 *
 * We only dump frozen in-memory indexes and skip the active
 * one in order not to conflict with concurrent insertions.
 * The caller is supposed to freeze the active mem for it to
 * be dumped.
 *
 * @dump_lsn is the maximal LSN to dump. Only in-memory trees
 * with @min_lsn <= @dump_lsn are addded to the write iterator.
 *
 * The maximum possible number of output tuples of the
 * iterator is returned in @p_max_output_count.
 */
static struct vy_write_iterator *
vy_range_get_dump_iterator(struct vy_range *range, int64_t vlsn,
			   int64_t dump_lsn, size_t *p_max_output_count)
{
	struct vy_write_iterator *wi;
	struct vy_mem *mem;
	*p_max_output_count = 0;

	wi = vy_write_iterator_new(range->index, range->run_count == 0, vlsn);
	if (wi == NULL)
		goto err_wi;
	rlist_foreach_entry(mem, &range->frozen, in_frozen) {
		if (mem->min_lsn > dump_lsn)
			continue;
		if (vy_write_iterator_add_mem(wi, mem) != 0)
			goto err_wi_sub;
		*p_max_output_count += mem->tree.size;
	}
	return wi;
err_wi_sub:
	vy_write_iterator_cleanup(wi);
	vy_write_iterator_delete(wi);
err_wi:
	return NULL;
}

/**
 * Create a write iterator to compact a range.
 * For @dump_lsn, @vlsn and @p_max_output_count @sa vy_range_get_dump_iterator.
 *
 * @run_count determines how many runs are added to the write
 * iterator. Set to @range->run_count for major compaction,
 * 0 < .. < @range->run_count for minor compaction, or 0 for
 * dump.
 */
static struct vy_write_iterator *
vy_range_get_compact_iterator(struct vy_range *range, int run_count,
			      int64_t vlsn, int64_t dump_lsn,
			      bool is_last_level, size_t *p_max_output_count)
{
	struct vy_write_iterator *wi;
	struct vy_run *run;
	struct vy_mem *mem;
	*p_max_output_count = 0;

	wi = vy_write_iterator_new(range->index, is_last_level, vlsn);
	if (wi == NULL)
		goto err_wi;
	/*
	 * Prepare for merge. Note, merge iterator requires newer
	 * sources to be added first so mems are added before runs.
	 */
	rlist_foreach_entry(mem, &range->frozen, in_frozen) {
		if (mem->min_lsn > dump_lsn)
			continue;
		if (vy_write_iterator_add_mem(wi, mem) != 0)
			goto err_wi_sub;
		*p_max_output_count += mem->tree.size;
	}
	assert(run_count >= 0 && run_count <= range->run_count);
	rlist_foreach_entry(run, &range->runs, in_range) {
		if (run_count-- == 0)
			break;
		if (vy_write_iterator_add_run(wi, run) != 0)
			goto err_wi_sub;
		*p_max_output_count += run->info.keys;
	}
	return wi;
err_wi_sub:
	vy_write_iterator_cleanup(wi);
	vy_write_iterator_delete(wi);
err_wi:
	return NULL;
}

/*
 * Create a new run for a range and write statements returned by a write
 * iterator to the run file until the end of the range is encountered.
 */
static int
vy_range_write_run(struct vy_range *range, struct vy_write_iterator *wi,
		   struct tuple **stmt, size_t *written,
		   size_t max_output_count, double bloom_fpr,
		   uint64_t *dumped_statements, const char **max_key)
{
	assert(stmt != NULL);

	/* Do not create empty run files. */
	if (*stmt == NULL)
		return 0;

	const struct vy_index *index = range->index;
	const struct index_def *index_def = index->index_def;
	const struct index_def *user_index_def = index->user_index_def;

	struct vy_run *run = range->new_run;
	assert(run != NULL);

	ERROR_INJECT(ERRINJ_VY_RANGE_DUMP,
		     {diag_set(ClientError, ER_INJECTION,
			       "vinyl range dump"); return -1;});

	struct bloom_spectrum bs;
	bloom_spectrum_create(&bs, max_output_count, bloom_fpr, runtime.quota);

	if (vy_run_write_data(run, index->path, wi, stmt, range->end, &bs,
			      index_def, user_index_def, max_key) != 0)
		return -1;

	bloom_spectrum_choose(&bs, &run->info.bloom);
	run->info.has_bloom = true;
	bloom_spectrum_destroy(&bs, runtime.quota);

	if (vy_run_write_index(run, index->path) != 0)
		return -1;

	assert(!vy_run_is_empty(run));
	*written += vy_run_size(run);
	*dumped_statements += run->info.keys;
	return 0;
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
	struct index_def *index_def = index->index_def;
	struct vy_run *run = NULL;

	/* The range hasn't been merged yet - too early to split it. */
	if (range->n_compactions < 1)
		return false;

	/* Find the oldest run. */
	assert(!rlist_empty(&range->runs));
	run = rlist_last_entry(&range->runs, struct vy_run, in_range);

	/* The range is too small to be split. */
	if (vy_run_size(run) < (uint64_t)index_def->opts.range_size * 4 / 3)
		return false;

	/* Find the median key in the oldest run (approximately). */
	struct vy_page_info *mid_page;
	mid_page = vy_run_page_info(run, run->info.count / 2);

	struct vy_page_info *first_page = vy_run_page_info(run, 0);

	/* No point in splitting if a new range is going to be empty. */
	if (key_compare(first_page->min_key, mid_page->min_key,
			&index_def->key_def) == 0)
		return false;
	*p_split_key = mid_page->min_key;
	return true;
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
	struct index_opts *opts = &range->index->index_def->opts;

	assert(opts->run_count_per_level > 0);
	assert(opts->run_size_ratio > 1);
	assert(range->max_dump_size > 0);

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
	 * For the first level, it's the maximal size of a dump.
	 * For lower levels it's computed as first level run size
	 * times run_size_ratio.
	 */
	uint64_t target_run_size = range->max_dump_size;

	struct vy_run *run;
	rlist_foreach_entry(run, &range->runs, in_range) {
		uint64_t run_size = vy_run_size(run);
		total_size += run_size;
		level_run_count++;
		total_run_count++;
		while (run_size > target_run_size) {
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
	uint64_t total_size = range->size + range->used;
	/* Coalesce ranges until total_size > max_size. */
	uint64_t max_size = index->index_def->opts.range_size / 2;

	/*
	 * We can't coalesce a range that was scheduled for dump
	 * or compaction, because it is about to be processed by
	 * a worker thread.
	 */
	assert(!vy_range_is_scheduled(range));

	*p_first = *p_last = range;
	for (it = vy_range_tree_next(&index->tree, range);
	     it != NULL && !vy_range_is_scheduled(it);
	     it = vy_range_tree_next(&index->tree, it)) {
		uint64_t size = it->size + it->used;
		if (total_size + size > max_size)
			break;
		total_size += size;
		*p_last = it;
	}
	for (it = vy_range_tree_prev(&index->tree, range);
	     it != NULL && !vy_range_is_scheduled(it);
	     it = vy_range_tree_prev(&index->tree, it)) {
		uint64_t size = it->size + it->used;
		if (total_size + size > max_size)
			break;
		total_size += size;
		*p_first = it;
	}
	return *p_first != *p_last;
}

/**
 * Coalesce a range with one or more its neighbors if it is too small.
 * In order to coalesce ranges, we splice their lists of in-memory
 * indexes and on-disk runs, and reflect the change in the metadata
 * log. No long-term operation involving a worker thread, like writing
 * a new run file, is necessary, because the merge iterator can deal
 * with runs that intersect by LSN coexisting in the same range as long
 * as they do not intersect for each particular key, which is true in
 * case of merging key ranges.
 */
static void
vy_range_maybe_coalesce(struct vy_range **p_range)
{
	struct vy_range *range = *p_range;
	struct vy_index *index = range->index;
	struct vy_scheduler *scheduler = index->env->scheduler;
	struct error *e;

	struct vy_range *first, *last;
	if (!vy_range_needs_coalesce(range, &first, &last))
		return;

	struct vy_range *result = vy_range_new(index, -1,
					       first->begin, last->end);
	if (result == NULL)
		goto fail_range;

	struct vy_range *it;
	struct vy_range *end = vy_range_tree_next(&index->tree, last);

	/*
	 * Log change in metadata.
	 */
	vy_log_tx_begin();
	vy_log_insert_range(index->index_def->opts.lsn, result->id,
			    result->begin, result->end, result->is_level_zero);
	for (it = first; it != end; it = vy_range_tree_next(&index->tree, it)) {
		struct vy_run *run;
		rlist_foreach_entry(run, &it->runs, in_range)
			vy_log_insert_run(result->id, run->id);
		vy_log_delete_range(it->id);
	}
	if (vy_log_tx_commit() < 0)
		goto fail_commit;

	/*
	 * Move runs and mems of the coalesced ranges to the
	 * resulting range and delete the former.
	 */
	it = first;
	while (it != end) {
		struct vy_range *next = vy_range_tree_next(&index->tree, it);
		vy_scheduler_remove_range(scheduler, it);
		vy_index_unacct_range(index, it);
		vy_index_remove_range(index, it);
		vy_range_freeze_mem(it);
		rlist_splice(&result->runs, &it->runs);
		rlist_splice(&result->frozen, &it->frozen);
		result->run_count += it->run_count;
		result->size += it->size;
		result->used += it->used;
		if (result->min_lsn > it->min_lsn)
			result->min_lsn = it->min_lsn;
		vy_range_delete(it);
		it = next;
	}
	/*
	 * Coalescing increases read amplification and breaks the log
	 * structured layout of the run list, so, although we could
	 * leave the resulting range as it is, we'd better compact it
	 * as soon as we can.
	 */
	result->compact_priority = result->run_count;
	vy_index_acct_range(index, result);
	vy_index_add_range(index, result);
	index->version++;
	vy_scheduler_add_range(scheduler, result);

	say_info("%s: coalesced ranges %s", index->name, vy_range_str(result));
	*p_range = result;
	return;

fail_commit:
	vy_range_delete(result);
fail_range:
	assert(!diag_is_empty(diag_get()));
	e = diag_last_error(diag_get());
	say_error("%s: failed to coalesce range %s: %s",
		  index->name, vy_range_str(range), e->errmsg);
}

/**
 * Create an index directory for a new index.
 * TODO: create index files only after the WAL
 * record is committed.
 */
static int
vy_index_create(struct vy_index *index)
{
	struct index_def *index_def = index->user_index_def;
	struct vy_scheduler *scheduler = index->env->scheduler;

	/* create directory */
	int rc;
	char *path_sep = index->path;
	while (*path_sep == '/') {
		/* Don't create root */
		++path_sep;
	}
	while ((path_sep = strchr(path_sep, '/'))) {
		/* Recursively create path hierarchy */
		*path_sep = '\0';
		rc = mkdir(index->path, 0777);
		if (rc == -1 && errno != EEXIST) {
			diag_set(SystemError, "failed to create directory '%s'",
		                 index->path);
			*path_sep = '/';
			return -1;
		}
		*path_sep = '/';
		++path_sep;
	}
	rc = mkdir(index->path, 0777);
	if (rc == -1 && errno != EEXIST) {
		diag_set(SystemError, "failed to create directory '%s'",
			 index->path);
		return -1;
	}

	/* create initial range */
	struct vy_range *range = vy_range_new(index, -1, NULL, NULL);
	if (unlikely(range == NULL))
		return -1;
	vy_index_add_range(index, range);
	vy_index_acct_range(index, range);
	vy_scheduler_add_range(scheduler, range);

	/*
	 * Log change in metadata.
	 */
	vy_log_tx_begin();
	vy_log_create_index(index_def->opts.lsn, index_def->iid,
			    index_def->space_id, &index_def->key_def);
	vy_log_insert_range(index->index_def->opts.lsn,
			    range->id, NULL, NULL, true);
	if (vy_log_tx_commit() < 0)
		return -1;

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
};

/** Index recovery callback, passed to vy_recovery_iterate_index(). */
static int
vy_index_recovery_cb(const struct vy_log_record *record, void *cb_arg)
{
	struct vy_index_recovery_cb_arg *arg = cb_arg;
	struct vy_index *index = arg->index;
	struct vy_range *range = arg->range;
	struct index_def *index_def = index->index_def;
	struct vy_run *run;

	switch (record->type) {
	case VY_LOG_CREATE_INDEX:
		assert(record->index_lsn == index_def->opts.lsn);
		break;
	case VY_LOG_DROP_INDEX:
		index->is_dropped = true;
		break;
	case VY_LOG_INSERT_RANGE:
		range = vy_range_new(index, record->range_id,
				     record->range_begin,
				     record->range_end);
		if (range == NULL)
			return -1;
		if (range->begin != NULL && range->end != NULL &&
		    key_compare(range->begin, range->end, &index_def->key_def) >= 0) {
			diag_set(ClientError, ER_VINYL, "invalid range");
			return -1;
		}
		vy_index_add_range(index, range);
		arg->range = range;
		break;
	case VY_LOG_INSERT_RUN:
		assert(range != NULL);
		assert(range->id == record->range_id);
		run = vy_run_new(record->run_id);
		if (run == NULL)
			return -1;
		if (vy_run_recover(run, index->path) != 0) {
			vy_run_unref(run);
			return -1;
		}
		vy_range_add_run(range, run);
		break;
	default:
		unreachable();
	}
	return 0;
}

static int
vy_index_open_ex(struct vy_index *index)
{
	struct vy_env *env = index->env;
	assert(env->recovery != NULL);

	struct vy_index_recovery_cb_arg arg = { .index = index };
	if (vy_recovery_iterate_index(env->recovery, index->index_def->opts.lsn,
				      false, vy_index_recovery_cb, &arg) != 0)
		return -1;

	/*
	 * Update index size and make ranges visible to the scheduler.
	 * Also, make sure that the index does not have holes, i.e.
	 * all data were recovered.
	 */
	struct vy_range *range, *prev = NULL;
	for (range = vy_range_tree_first(&index->tree); range != NULL;
	     prev = range, range = vy_range_tree_next(&index->tree, range)) {
		if ((prev == NULL && range->begin != NULL) ||
		    (prev != NULL && !vy_range_is_adjacent(prev, range,
							   index->index_def)))
			break;
		vy_index_acct_range(index, range);
		vy_scheduler_add_range(env->scheduler, range);
	}
	if (range != NULL || prev->end != NULL) {
		diag_set(ClientError, ER_VINYL, "range overlap or hole");
		return -1;
	}
	return 0;
}

/*
 * Save a statement in the range's in-memory index. If the
 * region_stmt is NULL and the statement successfully inserted
 * then the new lsregion statement is returned via @a region_stmt.
 * @param range Range to which the statement insert to.
 * @param mem Range's in-memory tree to insert the statement into.
 * @param stmt Statement, allocated on malloc().
 * @param region_stmt NULL or the same statement, allocated on
 *                    lsregion.
 * @retval  0 Success.
 * @retval -1 Memory error.
 */
static int
vy_range_set(struct vy_range *range, struct vy_mem *mem,
	     const struct tuple *stmt, const struct tuple **region_stmt)
{
	assert(!vy_stmt_is_region_allocated(stmt));
	assert(*region_stmt == NULL ||
	       vy_stmt_is_region_allocated(*region_stmt));
	struct vy_index *index = range->index;
	struct vy_scheduler *scheduler = index->env->scheduler;
	struct lsregion *allocator = &index->env->allocator;
	int64_t lsn = vy_stmt_lsn(stmt);

	bool was_empty = (mem->used == 0);
	/* Allocate region_stmt on demand. */
	if (*region_stmt == NULL) {
		*region_stmt = vy_stmt_dup_lsregion(stmt, allocator, lsn);
		if (*region_stmt == NULL)
			return -1;
	}
	if (vy_mem_insert(mem, *region_stmt) != 0) {
		/* Sic: can't free region_stmt allocated on lsregion */
		return -1;
	}

	if (was_empty)
		vy_scheduler_mem_dirtied(scheduler, mem);

	if (range->used == 0) {
		range->min_lsn = lsn;
		vy_scheduler_update_range(scheduler, range);
	}

	assert(mem->min_lsn <= lsn);
	assert(range->min_lsn <= lsn);

	size_t size = tuple_size(stmt);
	range->used += size;
	index->used += size;
	index->stmt_count++;

	return 0;
}

static void
vy_index_squash_upserts(struct vy_index *index, struct tuple *stmt);

static int
vy_range_set_upsert(struct vy_range *range, struct vy_mem *mem,
		    struct tuple *stmt)
{
	assert(vy_stmt_type(stmt) == IPROTO_UPSERT);

	struct vy_index *index = range->index;
	struct vy_stat *stat = index->env->stat;
	struct index_def *index_def = index->index_def;
	const struct tuple *older;
	int64_t lsn = vy_stmt_lsn(stmt);
	older = vy_mem_older_lsn(mem, stmt);
	const struct tuple *region_stmt = NULL;
	if ((older != NULL && vy_stmt_type(older) != IPROTO_UPSERT) ||
	    (older == NULL && range->shadow == NULL &&
	     rlist_empty(&range->frozen) && range->run_count == 0)) {
		/*
		 * Optimization:
		 *
		 *  1. An older non-UPSERT statement for the key has been
		 *     found in the active memory index.
		 *  2. Active memory index doesn't have statements for the
		 *     key, but there are no more mems and runs.
		 *
		 *  => apply UPSERT to the older statement and save
		 *     resulted REPLACE instead of original UPSERT.
		 */
		assert(older == NULL || vy_stmt_type(older) != IPROTO_UPSERT);
		struct tuple *upserted =
			vy_apply_upsert(stmt, older, index_def,
					index->space_format,
					index->upsert_format, false, stat);
		if (upserted == NULL)
			return -1; /* OOM */
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
			return 0;
		}
		assert(older == NULL || upserted_lsn != vy_stmt_lsn(older));
		assert(vy_stmt_type(upserted) == IPROTO_REPLACE);
		int rc = vy_range_set(range, mem, upserted, &region_stmt);
		tuple_unref(upserted);
		if (rc < 0)
			return -1;
		rmean_collect(stat->rmean, VY_STAT_UPSERT_SQUASHED, 1);
		return 0;
	}

	/*
	 * If there are a lot of successive upserts for the same key,
	 * select might take too long to squash them all. So once the
	 * number of upserts exceeds a certain threshold, we schedule
	 * a fiber to merge them and insert the resulting statement
	 * after the latest upsert.
	 */
	enum {
		VY_UPSERT_THRESHOLD = 128,
		VY_UPSERT_INF = 255,
	};
	if (older != NULL)
		vy_stmt_set_n_upserts(stmt, vy_stmt_n_upserts(older));
	if (vy_stmt_n_upserts(stmt) != VY_UPSERT_INF) {
		vy_stmt_set_n_upserts(stmt, vy_stmt_n_upserts(stmt) + 1);
		if (vy_stmt_n_upserts(stmt) > VY_UPSERT_THRESHOLD) {
			vy_index_squash_upserts(index, stmt);
			/*
			 * Prevent further upserts from starting new
			 * workers while this one is in progress.
			 */
			vy_stmt_set_n_upserts(stmt, VY_UPSERT_INF);
		}
	}
	return vy_range_set(range, mem, stmt, &region_stmt);
}

/*
 * Check if a statement was dumped to disk before the last shutdown and
 * therefore can be skipped on WAL replay.
 *
 * Since the minimal unit that can be dumped to disk is a range, a
 * statement is on disk iff its LSN is less than or equal to the max LSN
 * over all statements written to disk in the same range.
 */
static bool
vy_stmt_is_committed(struct vy_index *index, const struct tuple *stmt)
{
	/*
	 * If the index is going to be dropped on WAL recovery,
	 * there's no point in inserting statements into it.
	 */
	if (index->is_dropped)
		return true;

	struct vy_range *range;
	range = vy_range_tree_find_by_key(&index->tree, ITER_EQ, index->index_def,
					  stmt);
	if (rlist_empty(&range->runs))
		return false;

	/*
	 * The newest run, i.e. the run containing a statement with max
	 * LSN, is at the head of the list.
	 */
	struct vy_run *run = rlist_first_entry(&range->runs,
					       struct vy_run, in_range);
	return vy_stmt_lsn(stmt) <= run->info.max_lsn;
}

/**
 * Look up the range the given transaction goes into, rotate its
 * in-memory index if necessary, and pin it to make sure it is not
 * dumped until the transaction is complete.
 */
static int
vy_tx_write_prepare(struct txv *v)
{
	struct vy_index *index = v->index;
	struct tuple *stmt = v->stmt;
	struct vy_range *range;

	range = vy_range_tree_find_by_key(&index->tree, ITER_EQ,
					  index->index_def, stmt);
	/*
	 * Allocate a new in-memory tree if either of the following
	 * conditions is true:
	 *
	 * - Snapshot version has increased after the tree was created.
	 *   In this case we need to dump the tree as is in order to
	 *   guarantee snapshot consistency.
	 *
	 * - Schema version has increased after the tree was created.
	 *   We have to seal the tree, because we don't support mixing
	 *   statements of different formats in the same tree.
	 */
	if (unlikely(range->mem->sc_version != sc_version ||
		     range->mem->snapshot_version != snapshot_version)) {
		if (vy_range_rotate_mem(range) != 0)
			return -1;
	}
	vy_mem_pin(range->mem);
	v->mem = range->mem;
	return 0;
}

/**
 * Write a single statement into an index. If the statement has an
 * lsregion copy then use it, else create it.
 * @param index Index to write to.
 * @param mem In-memory tree to write to.
 * @param stmt Statement allocated from malloc().
 * @param status Vinyl engine status.
 * @param region_stmt NULL or the same statement as stmt,
 *                    but allocated on lsregion.
 * @param status Vinyl engine status.
 *
 * @retval  0 Success.
 * @retval -1 Memory error.
 */
static int
vy_tx_write(struct vy_index *index, struct vy_mem *mem,
	    struct tuple *stmt, const struct tuple **region_stmt,
	    enum vy_status status)
{
	assert(!vy_stmt_is_region_allocated(stmt));
	assert(*region_stmt == NULL ||
	       vy_stmt_is_region_allocated(*region_stmt));

	vy_mem_unpin(mem);

	/*
	 * If we're recovering the WAL, it may happen so that this
	 * particular run was dumped after the checkpoint, and we're
	 * replaying records already present in the database. In this
	 * case avoid overwriting a newer version with an older one.
	 */
	if (status == VINYL_FINAL_RECOVERY_LOCAL ||
	    status == VINYL_FINAL_RECOVERY_REMOTE) {
		if (vy_stmt_is_committed(index, stmt))
			return 0;
	}

	/* Match range. */
	struct vy_range *range;
	range = vy_range_tree_find_by_key(&index->tree, ITER_EQ,
					  index->index_def, stmt);

	int rc;
	switch (vy_stmt_type(stmt)) {
	case IPROTO_UPSERT:
		rc = vy_range_set_upsert(range, mem, stmt);
		break;
	default:
		rc = vy_range_set(range, mem, stmt, region_stmt);
		break;
	}
	/*
	 * Invalidate cache element.
	 */
	struct vy_cache *cache = index->cache;
	vy_cache_on_write(cache, stmt);

	return rc;
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
	/** How long ->execute took, in nanoseconds. */
	ev_tstamp exec_time;
	/** Number of bytes written to disk by this task. */
	size_t dump_size;
	/** Number of statements dumped to the disk. */
	uint64_t dumped_statements;
	/** Range to dump or compact. */
	struct vy_range *range;
	/** Write iterator producing statements for the new run. */
	struct vy_write_iterator *wi;
	/**
	 * Max LSN dumped by this task.
	 *
	 * When we dump or compact a range, we write all its frozen
	 * in-memory trees that existed when the task was scheduled
	 * (@sa vy_range_get_dump/compact_iterator()). During task
	 * execution, new trees can be added due to DDL
	 * (@sa vy_tx_write()), hence we need to remember which
	 * trees we are going to write so as to only delete dumped
	 * trees upon task completion.
	 */
	int64_t dump_lsn;
	/**
	 * A link in the list of all pending tasks, generated by
	 * task scheduler.
	 */
	struct stailq_entry link;
	/** For run-writing tasks: maximum possible number of tuple to write */
	size_t max_output_count;
	/** For run-writing tasks: bloom filter false-positive-rate setting */
	double bloom_fpr;
	/** Max written key. */
	char *max_written_key;
	/**
	 * Max dump size remembered before compaction. Used to
	 * restore the max dump size in case of compaction abort.
	 */
	uint64_t saved_max_dump_size;
	/** Count of ranges to compact. */
	int run_count;
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
	if (task->max_written_key != NULL)
		free(task->max_written_key);
	TRASH(task);
	mempool_free(pool, task);
}

static int
vy_task_dump_execute(struct vy_task *task)
{
	struct vy_range *range = task->range;
	struct vy_write_iterator *wi = task->wi;
	struct tuple *stmt;
	assert(range->is_level_zero);

	/* The range has been deleted from the scheduler queues. */
	assert(range->in_dump.pos == UINT32_MAX);
	assert(range->in_compact.pos == UINT32_MAX);
	const char **max_key = NULL;
	if (range->is_level_zero)
		max_key = (const char **) &task->max_written_key;

	/* Start iteration. */
	if (vy_write_iterator_next(wi, &stmt) != 0 ||
	    vy_range_write_run(range, wi, &stmt, &task->dump_size,
			       task->max_output_count, task->bloom_fpr,
			       &task->dumped_statements, max_key) != 0) {
		vy_write_iterator_cleanup(wi);
		return -1;
	}

	vy_write_iterator_cleanup(wi);
	return 0;
}

static int
vy_task_dump_complete(struct vy_task *task)
{
	struct vy_index *index = task->index;
	struct vy_range *range = task->range;
	assert(range->is_level_zero);
	struct vy_scheduler *scheduler = index->env->scheduler;

	/*
	 * Log change in metadata.
	 */
	if (!vy_run_is_empty(range->new_run)) {
		vy_log_tx_begin();
		vy_log_insert_run(range->id, range->new_run->id);
		if (vy_log_tx_commit() < 0)
			return -1;
	} else
		vy_range_discard_new_run(range);

	say_info("%s: completed dumping range %s",
		 index->name, vy_range_str(range));

	/* The iterator has been cleaned up in a worker thread. */
	vy_write_iterator_delete(task->wi);

	vy_index_unacct_range(index, range);
	vy_range_dump_mems(range, scheduler, task->dump_lsn);
	if (range->new_run != NULL) {
		range->max_dump_size = MAX(range->max_dump_size,
					   vy_run_size(range->new_run));
		vy_range_add_run(range, range->new_run);
		vy_range_update_compact_priority(range);
		range->new_run = NULL;
		assert(! range->is_level_zero || task->max_written_key != NULL);
	}
	range->version++;
	vy_index_acct_range(index, range);
	vy_scheduler_add_range(scheduler, range);
	return 0;
}

static void
vy_task_dump_abort(struct vy_task *task, bool in_shutdown)
{
	struct vy_index *index = task->index;
	struct vy_range *range = task->range;
	assert(range->is_level_zero);

	/* The iterator has been cleaned up in a worker thread. */
	vy_write_iterator_delete(task->wi);

	if (!in_shutdown && !index->is_dropped) {
		say_error("%s: failed to dump range %s: %s",
			  index->name, vy_range_str(range),
			  diag_last_error(&task->diag)->errmsg);
		vy_range_discard_new_run(range);
		vy_scheduler_add_range(index->env->scheduler, range);
	}

	/*
	 * No need to roll back anything if we failed to write a run.
	 * The range will carry on with a new shadow in-memory index.
	 */
}

/**
 * Create a task to dump a range. @dump_lsn is the max LSN to dump:
 * on success the task is supposed to dump all in-memory trees with
 * @min_lsn <= @dump_lsn.
 */
static int
vy_task_dump_new(struct mempool *pool, struct vy_range *range,
		 int64_t dump_lsn, struct vy_task **p_task)
{
	assert(range->is_level_zero);
	static struct vy_task_ops dump_ops = {
		.execute = vy_task_dump_execute,
		.complete = vy_task_dump_complete,
		.abort = vy_task_dump_abort,
	};

	struct vy_index *index = range->index;
	struct tx_manager *xm = index->env->xm;
	struct vy_scheduler *scheduler = index->env->scheduler;

	if (index->is_dropped) {
		vy_scheduler_remove_range(scheduler, range);
		return 0;
	}

	struct vy_task *task = vy_task_new(pool, index, &dump_ops);
	if (task == NULL)
		goto err_task;

	vy_range_maybe_coalesce(&range);

	if (vy_range_prepare_new_run(range) != 0)
		goto err_run;

	if (vy_range_rotate_mem(range) != 0)
		goto err_mem;

	/*
	 * Before adding in-memory trees to the write iterator,
	 * wait until all writes to them are over and mem->min_lsn,
	 * used for filtering dumped mems, is stabilized.
	 */
	vy_range_wait_pinned(range);
	/*
	 * Remember the current value of xm->lsn. It will be used
	 * to delete dumped in-memory trees on task completion
	 * (see vy_range_dump_mems()). Every in-memory tree created
	 * after this point (and so not dumped by this task) will
	 * have min_lsn > xm->lsn.
	 *
	 * In case checkpoint is in progress (dump_lsn != INT64_MAX)
	 * also filter mems that were created after checkpoint_lsn
	 * to make checkpoint consistent.
	 */
	dump_lsn = MIN(xm->lsn, dump_lsn);

	struct vy_write_iterator *wi;
	wi = vy_range_get_dump_iterator(range, tx_manager_vlsn(xm), dump_lsn,
					&task->max_output_count);
	if (wi == NULL)
		goto err_wi;

	task->range = range;
	task->wi = wi;
	task->dump_lsn = dump_lsn;
	task->bloom_fpr = index->env->conf->bloom_fpr;

	vy_scheduler_remove_range(scheduler, range);

	say_info("%s: started dumping range %s",
		 index->name, vy_range_str(range));
	*p_task = task;
	return 0;
err_wi:
	/* Leave the new mem on the list in case of failure. */
err_mem:
	vy_range_discard_new_run(range);
err_run:
	vy_task_delete(pool, task);
err_task:
	say_error("%s: can't start range dump %s: %s", index->name,
		  vy_range_str(range), diag_last_error(diag_get())->errmsg);
	return -1;
}

static int
vy_task_split_execute(struct vy_task *task)
{
	struct vy_range *range = task->range;
	struct vy_write_iterator *wi = task->wi;
	struct tuple *stmt;
	struct vy_range *r;
	uint64_t unused;

	/* The range has been deleted from the scheduler queues. */
	assert(range->in_dump.pos == UINT32_MAX);
	assert(range->in_compact.pos == UINT32_MAX);

	/* Start iteration. */
	if (vy_write_iterator_next(wi, &stmt) != 0)
		goto error;
	assert(!rlist_empty(&range->split_list));
	rlist_foreach_entry(r, &range->split_list, split_list) {
		assert(r->shadow == range);
		if (&r->split_list != rlist_first(&range->split_list)) {
			ERROR_INJECT(ERRINJ_VY_RANGE_SPLIT,
				     {diag_set(ClientError, ER_INJECTION,
					       "vinyl range split");
				      goto error;});
		}
		if (vy_range_write_run(r, wi, &stmt, &task->dump_size,
				       task->max_output_count, task->bloom_fpr,
				       &unused, NULL) != 0)
			goto error;
	}
	vy_write_iterator_cleanup(wi);
	return 0;
error:
	vy_write_iterator_cleanup(wi);
	return -1;
}

static int
vy_task_split_complete(struct vy_task *task)
{
	struct vy_index *index = task->index;
	struct vy_range *range = task->range;
	struct vy_scheduler *scheduler = index->env->scheduler;
	struct vy_range *r, *tmp;
	struct vy_mem *mem;

	/*
	 * Log change in metadata.
	 */
	vy_log_tx_begin();
	vy_log_delete_range(range->id);
	rlist_foreach_entry(r, &range->split_list, split_list) {
		vy_log_insert_range(index->index_def->opts.lsn, r->id,
				    r->begin, r->end, r->is_level_zero);
		if (!vy_run_is_empty(r->new_run))
			vy_log_insert_run(r->id, r->new_run->id);
	}
	if (vy_log_tx_commit() < 0)
		return -1;

	rlist_foreach_entry(r, &range->split_list, split_list) {
		if (vy_run_is_empty(r->new_run))
			vy_range_discard_new_run(r);
	}

	say_info("%s: completed splitting range %s",
		 index->name, vy_range_str(range));

	/* The iterator has been cleaned up in a worker thread. */
	vy_write_iterator_delete(task->wi);

	/*
	 * If range split completed successfully, all runs and mems of
	 * the original range were dumped and hence we don't need it any
	 * longer. So unlink new ranges from the original one and delete
	 * the latter.
	 */
	vy_index_unacct_range(index, range);
	rlist_foreach_entry_safe(r, &range->split_list, split_list, tmp) {
		/*
		 * Add the new run created by split to the list
		 * unless it's empty.
		 */
		if (r->new_run != NULL) {
			vy_range_add_run(r, r->new_run);
			r->new_run = NULL;
		}

		rlist_del(&r->split_list);
		assert(r->shadow == range);
		r->shadow = NULL;

		vy_index_acct_range(index, r);
		vy_scheduler_add_range(scheduler, r);
	}
	index->version++;

	/* Notify the scheduler that the range was dumped. */
	assert(range->mem == NULL); /* active mem was frozen */
	rlist_foreach_entry(mem, &range->frozen, in_frozen)
		vy_scheduler_mem_dumped(scheduler, mem);

	vy_range_delete(range);
	return 0;
}

static void
vy_task_split_abort(struct vy_task *task, bool in_shutdown)
{
	struct vy_index *index = task->index;
	struct vy_range *range = task->range;
	struct vy_range *r, *tmp;

	/* The iterator has been cleaned up in a worker thread. */
	vy_write_iterator_delete(task->wi);

	if (!in_shutdown && !index->is_dropped) {
		say_error("%s: failed to split range %s: %s",
			  index->name, vy_range_str(range),
			  diag_last_error(&task->diag)->errmsg);
		rlist_foreach_entry(r, &range->split_list, split_list)
			vy_range_discard_new_run(r);
		vy_scheduler_add_range(index->env->scheduler, range);
	}

	/*
	 * On split failure we delete new ranges, but leave their
	 * mems and runs linked to the old range so that statements
	 * inserted while split was in progress don't get lost.
	 */
	rlist_foreach_entry_safe(r, &range->split_list, split_list, tmp) {
		assert(r->run_count == 0);

		vy_range_freeze_mem(r);
		rlist_splice(&range->frozen, &r->frozen);
		if (range->used == 0)
			range->min_lsn = r->min_lsn;
		assert(range->min_lsn <= r->min_lsn);
		range->used += r->used;

		rlist_del(&r->split_list);
		assert(r->shadow == range);
		r->shadow = NULL;

		vy_index_remove_range(index, r);
		vy_range_delete(r);
	}
	vy_range_unfreeze_mem(range);

	/* Insert the range back into the tree. */
	vy_index_add_range(index, range);
	index->version++;
}

static int
vy_task_split_new(struct mempool *pool, struct vy_range *range,
		  const char *split_key, struct vy_task **p_task)
{
	struct vy_index *index = range->index;
	struct tx_manager *xm = index->env->xm;
	struct vy_scheduler *scheduler = index->env->scheduler;

	assert(rlist_empty(&range->split_list));

	static struct vy_task_ops split_ops = {
		.execute = vy_task_split_execute,
		.complete = vy_task_split_complete,
		.abort = vy_task_split_abort,
	};

	const char *keys[3];
	struct vy_range *parts[2] = {NULL, };
	const int n_parts = 2;

	struct vy_task *task = vy_task_new(pool, index, &split_ops);
	if (task == NULL)
		goto err_task;

	/* Determine new ranges' boundaries. */
	keys[0] = range->begin;
	keys[1] = split_key;
	keys[2] = range->end;

	/* Allocate new ranges. */
	for (int i = 0; i < n_parts; i++) {
		struct vy_range *r;

		r = parts[i] = vy_range_new(index, -1, keys[i], keys[i + 1]);
		if (r == NULL)
			goto err_parts;
		if (vy_range_prepare_new_run(r) != 0)
			goto err_parts;
	}

	vy_range_freeze_mem(range);

	/*
	 * See comment in vy_task_dump_new().
	 */
	vy_range_wait_pinned(range);
	int64_t dump_lsn = xm->lsn;

	struct vy_write_iterator *wi;
	wi = vy_range_get_compact_iterator(range, range->run_count,
					   tx_manager_vlsn(xm), dump_lsn, true,
					   &task->max_output_count);
	if (wi == NULL)
		goto err_wi;

	/* Replace the old range with the new ones. */
	vy_index_remove_range(index, range);
	for (int i = 0; i < n_parts; i++) {
		struct vy_range *r = parts[i];
		/*
		 * While range split is in progress, new statements are
		 * inserted to new ranges while read iterator walks over
		 * the old range (see vy_range_iterator_next()). To make
		 * new statements visible, link new ranges to the old
		 * range via ->split_list.
		 */
		rlist_add_tail(&range->split_list, &r->split_list);
		assert(r->shadow == NULL);
		r->shadow = range;
		vy_index_add_range(index, r);
	}

	range->version++;
	index->version++;

	task->range = range;
	task->wi = wi;
	task->dump_lsn = dump_lsn;
	task->bloom_fpr = index->env->conf->bloom_fpr;

	vy_scheduler_remove_range(scheduler, range);

	say_info("%s: started splitting range %s by key %s",
		 index->name, vy_range_str(range), vy_key_str(split_key));
	*p_task = task;
	return 0;
err_wi:
	vy_range_unfreeze_mem(range);
err_parts:
	for (int i = 0; i < n_parts; i++) {
		struct vy_range *r = parts[i];
		if (r == NULL)
			continue;
		if (r->new_run != NULL)
			vy_range_discard_new_run(r);
		vy_range_delete(r);
	}
	vy_task_delete(pool, task);
err_task:
	say_error("%s: can't start range splitting %s: %s", index->name,
		  vy_range_str(range), diag_last_error(diag_get())->errmsg);
	return -1;
}

static int
vy_task_compact_execute(struct vy_task *task)
{
	struct vy_range *range = task->range;
	struct vy_write_iterator *wi = task->wi;
	struct tuple *stmt;
	uint64_t unused;

	/* The range has been deleted from the scheduler queues. */
	assert(range->in_dump.pos == UINT32_MAX);
	assert(range->in_compact.pos == UINT32_MAX);

	/* Start iteration. */
	if (vy_write_iterator_next(wi, &stmt) != 0 ||
	    vy_range_write_run(range, wi, &stmt, &task->dump_size,
			       task->max_output_count, task->bloom_fpr,
			       &unused, NULL) != 0) {
		vy_write_iterator_cleanup(wi);
		return -1;
	}

	vy_write_iterator_cleanup(wi);
	return 0;
}

static int
vy_task_compact_complete(struct vy_task *task)
{
	struct vy_index *index = task->index;
	struct vy_range *range = task->range;
	struct vy_scheduler *scheduler = index->env->scheduler;
	struct vy_run *run, *tmp;
	int n;

	/*
	 * Log change in metadata.
	 */
	vy_log_tx_begin();
	n = task->run_count;
	rlist_foreach_entry(run, &range->runs, in_range) {
		vy_log_delete_run(run->id);
		if (--n == 0)
			break;
	}
	assert(n == 0);
	if (!vy_run_is_empty(range->new_run))
		vy_log_insert_run(range->id, range->new_run->id);
	if (vy_log_tx_commit() < 0)
		return -1;

	if (vy_run_is_empty(range->new_run))
		vy_range_discard_new_run(range);

	say_info("%s: completed compacting range %s",
		 index->name, vy_range_str(range));

	/* The iterator has been cleaned up in worker. */
	vy_write_iterator_delete(task->wi);

	/*
	 * Replace compacted mems and runs with the resulting run.
	 */
	vy_index_unacct_range(index, range);
	vy_range_dump_mems(range, scheduler, task->dump_lsn);
	n = task->run_count;
	rlist_foreach_entry_safe(run, &range->runs, in_range, tmp) {
		vy_range_remove_run(range, run);
		vy_run_unref(run);
		if (--n == 0)
			break;
	}
	assert(n == 0);
	if (range->new_run != NULL) {
		vy_range_add_run(range, range->new_run);
		range->new_run = NULL;
	}
	range->n_compactions++;
	range->version++;
	vy_index_acct_range(index, range);
	vy_scheduler_add_range(scheduler, range);
	return 0;
}

static void
vy_task_compact_abort(struct vy_task *task, bool in_shutdown)
{
	struct vy_index *index = task->index;
	struct vy_range *range = task->range;

	/* The iterator has been cleaned up in worker. */
	vy_write_iterator_delete(task->wi);
	/* Restore the max dump size and compact priority. */
	range->max_dump_size = MAX(task->saved_max_dump_size,
				   range->max_dump_size);
	vy_range_update_compact_priority(range);

	if (!in_shutdown && !index->is_dropped) {
		say_error("%s: failed to compact range %s: %s",
			  index->name, vy_range_str(range),
			  diag_last_error(&task->diag)->errmsg);
		vy_range_discard_new_run(range);
		vy_scheduler_add_range(index->env->scheduler, range);
	}

	/*
	 * No need to roll back anything if we failed to write a run.
	 * The range will carry on with a new shadow in-memory index.
	 */
}

static int
vy_task_compact_new(struct mempool *pool, struct vy_range *range,
		    struct vy_task **p_task)
{
	assert(range->compact_priority > 0);

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

	/* Consider splitting the range if it's too big. */
	const char *split_key;
	if (vy_range_needs_split(range, &split_key))
		return vy_task_split_new(pool, range, split_key, p_task);

	struct vy_task *task = vy_task_new(pool, index, &compact_ops);
	if (task == NULL)
		goto err_task;

	vy_range_maybe_coalesce(&range);

	if (vy_range_prepare_new_run(range) != 0)
		goto err_run;

	if (vy_range_rotate_mem(range) != 0)
		goto err_mem;

	/*
	 * See comment in vy_task_dump_new().
	 */
	vy_range_wait_pinned(range);
	int64_t dump_lsn = xm->lsn;

	struct vy_write_iterator *wi;
	bool is_last_level = range->compact_priority == range->run_count;
	wi = vy_range_get_compact_iterator(range, range->compact_priority,
					   tx_manager_vlsn(xm), dump_lsn,
					   is_last_level,
					   &task->max_output_count);
	if (wi == NULL)
		goto err_wi;

	task->range = range;
	task->wi = wi;
	task->dump_lsn = dump_lsn;
	task->bloom_fpr = index->env->conf->bloom_fpr;
	task->saved_max_dump_size = range->max_dump_size;
	task->run_count = range->compact_priority;
	range->max_dump_size = 0;
	range->compact_priority = 0;

	vy_scheduler_remove_range(scheduler, range);

	say_info("%s: started compacting range %s, runs %d/%d",
		 index->name, vy_range_str(range),
                 range->compact_priority, range->run_count);
	*p_task = task;
	return 0;
err_wi:
	/* Leave the new mem on the list in case of failure. */
err_mem:
	vy_range_discard_new_run(range);
err_run:
	vy_task_delete(pool, task);
err_task:
	say_error("%s: can't start range compacting %s: %s", index->name,
		  vy_range_str(range), diag_last_error(diag_get())->errmsg);
	return -1;
}

/* Scheduler Task }}} */

/* {{{ Scheduler */

#define HEAP_NAME vy_dump_heap

static bool
heap_dump_less(struct heap_node *a, struct heap_node *b)
{
	struct vy_range *left = container_of(a, struct vy_range, in_dump);
	struct vy_range *right = container_of(b, struct vy_range, in_dump);

	/* Older ranges are dumped first. */
	return left->min_lsn < right->min_lsn;
}

#define HEAP_LESS(h, l, r) heap_dump_less(l, r)

#include "salad/heap.h"

#undef HEAP_LESS
#undef HEAP_NAME

#define HEAP_NAME vy_compact_heap

static bool
heap_compact_less(struct heap_node *a, struct heap_node *b)
{
	struct vy_range *left = container_of(a, struct vy_range, in_compact);
	struct vy_range *right = container_of(b, struct vy_range, in_compact);
	/*
	 * Prefer ranges whose read amplification will be reduced
	 * most as a result of compaction.
	 */
	return left->compact_priority > right->compact_priority;
}

#define HEAP_LESS(h, l, r) heap_compact_less(l, r)

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

	/**
	 * List of all non-empty (in terms of allocated data) in-memory indexes.
	 * Older mems are closer to the tail of the list.
	 */
	struct rlist dirty_mems;
	/** Min LSN over all in-memory indexes. */
	int64_t mem_min_lsn;
	/**
	 * Snapshot signature if snapshot is in progress, otherwise -1.
	 * All in-memory indexes with min_lsn <= checkpoint_lsn must be
	 * dumped first.
	 */
	int64_t checkpoint_lsn;
	/** Signaled on checkpoint completion or failure. */
	struct ipc_cond checkpoint_cond;
};

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
vy_scheduler_quota_cb(enum vy_quota_event event, void *arg)
{
	struct vy_scheduler *scheduler = arg;

	switch (event) {
	case VY_QUOTA_EXCEEDED:
		ipc_cond_signal(&scheduler->scheduler_cond);
		break;
	case VY_QUOTA_THROTTLED:
		ipc_cond_wait(&scheduler->quota_cond);
		break;
	case VY_QUOTA_RELEASED:
		ipc_cond_broadcast(&scheduler->quota_cond);
		break;
	default:
		unreachable();
	}
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
	rlist_create(&scheduler->dirty_mems);
	scheduler->mem_min_lsn = INT64_MAX;
	scheduler->checkpoint_lsn = -1;
	ipc_cond_create(&scheduler->checkpoint_cond);
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
vy_scheduler_add_range(struct vy_scheduler *scheduler,
		       struct vy_range *range)
{
	vy_dump_heap_insert(&scheduler->dump_heap, &range->in_dump);
	vy_compact_heap_insert(&scheduler->compact_heap, &range->in_compact);
	assert(range->in_dump.pos != UINT32_MAX);
	assert(range->in_compact.pos != UINT32_MAX);
}

static void
vy_scheduler_update_range(struct vy_scheduler *scheduler,
			  struct vy_range *range)
{
	if (range->in_dump.pos == UINT32_MAX)
		return; /* range is being processed by a task */

	vy_dump_heap_update(&scheduler->dump_heap, &range->in_dump);
	assert(range->in_dump.pos != UINT32_MAX);
	assert(range->in_compact.pos != UINT32_MAX);
}

static void
vy_scheduler_remove_range(struct vy_scheduler *scheduler,
			  struct vy_range *range)
{
	vy_dump_heap_delete(&scheduler->dump_heap, &range->in_dump);
	vy_compact_heap_delete(&scheduler->compact_heap, &range->in_compact);
	range->in_dump.pos = UINT32_MAX;
	range->in_compact.pos = UINT32_MAX;
}

/**
 * Create a task for dumping a range. The new task is returned
 * in @ptask. If there's no range that needs to be dumped @ptask
 * is set to NULL.
 *
 * We only dump a range if it needs to be snapshotted or the quota
 * on memory usage is exceeded. In either case, the oldest range
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
	struct heap_node *pn = vy_dump_heap_top(&scheduler->dump_heap);
	if (pn == NULL)
		return 0; /* nothing to do */
	struct vy_range *range = container_of(pn, struct vy_range, in_dump);
	if (range->used == 0)
		return 0; /* nothing to do */
	int64_t dump_lsn = INT64_MAX;
	if (scheduler->checkpoint_lsn != -1) {
		/*
		 * Snapshot is in progress. To make a consistent
		 * snapshot, we only dump statements inserted before
		 * the WAL checkpoint.
		 */
		dump_lsn = scheduler->checkpoint_lsn;
		if (range->min_lsn > dump_lsn)
			return 0;
	} else {
		if (!vy_quota_is_exceeded(&scheduler->env->quota))
			return 0; /* nothing to do */
	}
	if (vy_task_dump_new(&scheduler->task_pool,
			     range, dump_lsn, ptask) != 0)
		return -1;
	if (*ptask == NULL)
		goto retry; /* index dropped */
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
	/* Do not schedule compaction until snapshot is complete. */
	if (scheduler->checkpoint_lsn != -1)
		return 0;
	struct heap_node *pn = vy_compact_heap_top(&scheduler->compact_heap);
	if (pn == NULL)
		return 0; /* nothing to do */
	struct vy_range *range = container_of(pn, struct vy_range, in_compact);
	if (range->compact_priority == 0)
		return 0; /* nothing to do */
	if (vy_task_compact_new(&scheduler->task_pool, range, ptask) != 0)
		return -1;
	if (*ptask == NULL)
		goto retry; /* index dropped */
	return 0; /* new task */
}

static int
vy_schedule(struct vy_scheduler *scheduler, struct vy_task **ptask)
{
	*ptask = NULL;
	if (rlist_empty(&scheduler->env->indexes))
		return 0;

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
				vy_stat_dump(env->stat, task->exec_time,
					     task->dump_size,
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
		ipc_cond_signal(&scheduler->checkpoint_cond);
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
		ERROR_INJECT_U64(ERRINJ_VINYL_SCHED_TIMEOUT,
				 errinj_getu64(ERRINJ_VINYL_SCHED_TIMEOUT) != 0,
				 {scheduler->timeout = 0.001 * errinj_getu64(ERRINJ_VINYL_SCHED_TIMEOUT);});
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
	coeio_enable();
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
		uint64_t start = ev_now(loop());
		task->status = task->ops->execute(task);
		task->exec_time = ev_now(loop()) - start;
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
	scheduler->worker_pool_size = cfg_geti("vinyl_threads");
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

static void
vy_scheduler_mem_dirtied(struct vy_scheduler *scheduler, struct vy_mem *mem)
{
	if (rlist_empty(&scheduler->dirty_mems))
		scheduler->mem_min_lsn = mem->min_lsn;
	assert(scheduler->mem_min_lsn <= mem->min_lsn);
	rlist_add_entry(&scheduler->dirty_mems, mem, in_dirty);
}

static void
vy_scheduler_mem_dumped(struct vy_scheduler *scheduler, struct vy_mem *mem)
{
	struct vy_env *env = scheduler->env;

	if (mem->used == 0)
		return;

	rlist_del_entry(mem, in_dirty);

	if (!rlist_empty(&scheduler->dirty_mems)) {
		struct vy_mem *oldest;
		oldest = rlist_last_entry(&scheduler->dirty_mems,
					  struct vy_mem, in_dirty);
		scheduler->mem_min_lsn = oldest->min_lsn;
	} else {
		scheduler->mem_min_lsn = INT64_MAX;
	}

	/* Free memory and release quota. */
	struct lsregion *allocator = &env->allocator;
	size_t mem_used_before = lsregion_used(allocator);
	lsregion_gc(allocator, scheduler->mem_min_lsn - 1);
	size_t mem_used_after = lsregion_used(allocator);
	assert(mem_used_after <= mem_used_before);
	vy_quota_release(&env->quota, mem_used_before - mem_used_after);

	if (scheduler->mem_min_lsn > scheduler->checkpoint_lsn) {
		/*
		 * All in-memory indexes have been checkpointed. Wake up
		 * the fiber waiting for checkpoint to complete.
		 */
		ipc_cond_signal(&scheduler->checkpoint_cond);
	}
}

/*
 * Schedule checkpoint. Please call vy_wait_checkpoint() after that.
 */
int
vy_checkpoint(struct vy_env *env, struct vclock *vclock)
{
	struct vy_scheduler *scheduler = env->scheduler;

	assert(env->status == VINYL_ONLINE);
	assert(scheduler->checkpoint_lsn == -1);

	scheduler->checkpoint_lsn = vclock_sum(vclock);
	if (scheduler->mem_min_lsn > scheduler->checkpoint_lsn)
		return 0; /* nothing to do */

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

	assert(scheduler->checkpoint_lsn != -1);

	while (!scheduler->is_throttled &&
	       scheduler->mem_min_lsn <= scheduler->checkpoint_lsn)
		ipc_cond_wait(&scheduler->checkpoint_cond);

	if (scheduler->mem_min_lsn <= scheduler->checkpoint_lsn) {
		assert(!diag_is_empty(&scheduler->diag));
		diag_add_error(diag_get(), diag_last_error(&scheduler->diag));
		goto error;
	}

	if (vy_log_rotate(vclock) != 0)
		goto error;

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
	 * Checkpoint blocks certain operations (e.g. compaction),
	 * so wake up the scheduler after we are done so that it
	 * can catch up.
	 */
	scheduler->checkpoint_lsn = -1;
	ipc_cond_signal(&scheduler->scheduler_cond);
}

/* Scheduler }}} */

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
	conf->bloom_fpr = cfg_getd("vinyl_bloom_fpr");

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

/** {{{ Introspection */

static void
vy_info_append_global(struct vy_env *env, struct info_handler *h)
{
	info_table_begin(h, "vinyl");
	info_append_str(h, "path", env->conf->path);
	info_append_str(h, "build", PACKAGE_VERSION);
	info_table_end(h);
}

static void
vy_info_append_memory(struct vy_env *env, struct info_handler *h)
{
	char buf[16];
	struct vy_quota *q = &env->quota;
	info_table_begin(h, "memory");
	info_append_u64(h, "used", q->used);
	info_append_u64(h, "limit", q->limit);
	info_append_u64(h, "watermark", q->watermark);
	snprintf(buf, sizeof(buf), "%d%%", (int)(100 * q->used / q->limit));
	info_append_str(h, "ratio", buf);
	info_append_u64(h, "min_lsn", env->scheduler->mem_min_lsn);
	info_table_end(h);
}

static int
vy_info_append_stat_rmean(const char *name, int rps, int64_t total, void *ctx)
{
	struct info_handler *h = ctx;
	info_table_begin(h, name);
	info_append_u32(h, "rps", rps);
	info_append_u64(h, "total", total);
	info_table_end(h);
	return 0;
}

static void
vy_info_append_stat_latency(struct info_handler *h,
			    const char *name, struct vy_latency *lat)
{
	info_table_begin(h, name);
	info_append_u64(h, "max", lat->max * 1000000000);
	info_append_u64(h, "avg", lat->count == 0 ? 0 :
			   lat->total / lat->count * 1000000000);
	info_table_end(h);
}

static void
vy_info_append_iterator_stat(struct info_handler *h, const char *name,
			     struct vy_iterator_stat *stat)
{
	info_table_begin(h, name);
	info_append_u64(h, "lookup_count", stat->lookup_count);
	info_append_u64(h, "step_count", stat->step_count);
	info_append_u64(h, "bloom_reflect_count", stat->bloom_reflections);
	info_table_end(h);
}

static void
vy_info_append_performance(struct vy_env *env, struct info_handler *h)
{
	struct vy_stat *stat = env->stat;

	info_table_begin(h, "performance");

	rmean_foreach(stat->rmean, vy_info_append_stat_rmean, h);

	info_append_u64(h, "write_count", stat->write_count);

	vy_info_append_stat_latency(h, "tx_latency", &stat->tx_latency);
	vy_info_append_stat_latency(h, "get_latency", &stat->get_latency);
	vy_info_append_stat_latency(h, "cursor_latency", &stat->cursor_latency);

	info_append_u64(h, "tx_rollback", stat->tx_rlb);
	info_append_u64(h, "tx_conflict", stat->tx_conflict);
	info_append_u32(h, "tx_active", env->xm->tx_count);

	info_append_u64(h, "dump_bandwidth", vy_stat_dump_bandwidth(stat));
	info_append_u64(h, "dump_total", stat->dump_total);
	info_append_u64(h, "dumped_statements", stat->dumped_statements);

	struct vy_cache_env *ce = &env->cache_env;
	info_table_begin(h, "cache");
	info_append_u64(h, "count", ce->cached_count);
	info_append_u64(h, "used", ce->quota.used);
	info_table_end(h);

	info_table_begin(h, "iterator");
	vy_info_append_iterator_stat(h, "txw", &stat->txw_stat);
	vy_info_append_iterator_stat(h, "cache", &stat->cache_stat);
	vy_info_append_iterator_stat(h, "mem", &stat->mem_stat);
	vy_info_append_iterator_stat(h, "run", &stat->run_stat);
	info_table_end(h);

	info_table_end(h);
}

static void
vy_info_append_metric(struct vy_env *env, struct info_handler *h)
{
	info_table_begin(h, "metric");
	info_append_u64(h, "lsn", env->xm->lsn);
	info_table_end(h);
}

void
vy_info(struct vy_env *env, struct info_handler *h)
{
	info_begin(h);
	vy_info_append_global(env, h);
	vy_info_append_memory(env, h);
	vy_info_append_metric(env, h);
	vy_info_append_performance(env, h);
	info_end(h);
}

void
vy_index_info(struct vy_index *index, struct info_handler *h)
{
	char buf[1024];
	info_begin(h);
	info_append_u64(h, "range_size", index->index_def->opts.range_size);
	info_append_u64(h, "page_size", index->index_def->opts.page_size);
	info_append_u64(h, "memory_used", index->used);
	info_append_u64(h, "size", index->size);
	info_append_u64(h, "count", index->stmt_count);
	info_append_u32(h, "page_count", index->page_count);
	info_append_u32(h, "range_count", index->range_count);
	info_append_u32(h, "run_count", index->run_count);
	info_append_u32(h, "run_avg", index->run_count / index->range_count);
	histogram_snprint(buf, sizeof(buf), index->run_hist);
	info_append_str(h, "run_histogram", buf);
	info_end(h);
}

/** }}} Introspection */

static int
vy_index_conf_create(struct vy_index *conf, struct index_def *index_def)
{
	char name[128];
	snprintf(name, sizeof(name), "%" PRIu32 "/%" PRIu32,
	         index_def->space_id, index_def->iid);
	conf->name = strdup(name);
	char path[PATH_MAX];
	vy_index_snprint_path(path, sizeof(path), conf->env->conf->path,
			      index_def->space_id, index_def->iid);
	conf->path = strdup(path);
	if (conf->name == NULL || conf->path == NULL) {
		if (conf->name)
			free(conf->name);
		if (conf->path)
			free(conf->path);
		conf->name = NULL;
		conf->path = NULL;
		diag_set(OutOfMemory, strlen(path) + 1, "strdup", "path");
		return -1;
	}
	return 0;
}

/**
 * Detect whether we already have non-garbage index files,
 * and open an existing index if that's the case. Otherwise,
 * create a new index. Take the current recovery status into
 * account.
 */
static int
vy_index_open_or_create(struct vy_index *index)
{
	/*
	 * TODO: don't drop/recreate index in local wal
	 * recovery mode if all operations already done.
	 */
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
		return vy_index_open_ex(index);
	default:
		unreachable();
	}
}

int
vy_index_open(struct vy_index *index)
{
	struct vy_env *env = index->env;

	if (vy_index_open_or_create(index) != 0)
		return -1;

	vy_index_ref(index);
	rlist_add(&env->indexes, &index->link);
	return 0;
}

static void
vy_index_ref(struct vy_index *index)
{
	index->refs++;
}

static void
vy_index_unref(struct vy_index *index)
{
	assert(index->refs > 0);
	if (--index->refs == 0)
		vy_index_delete(index);
}

void
vy_index_drop(struct vy_index *index)
{
	struct vy_env *env = index->env;
	int64_t index_id = index->index_def->opts.lsn;
	bool was_dropped = index->is_dropped;

	/* TODO:
	 * don't drop/recreate index in local wal recovery mode if all
	 * operations are already done.
	 */
	index->is_dropped = true;
	rlist_del(&index->link);
	index->space = NULL;
	vy_index_unref(index);

	/*
	 * We can't abort here, because the index drop request has
	 * already been written to WAL. So if we fail to write the
	 * change to the metadata log, we leave it in the log buffer,
	 * to be flushed along with the next transaction. If it is
	 * not flushed before the instance is shut down, we replay it
	 * on local recovery from WAL.
	 */
	if (env->status == VINYL_FINAL_RECOVERY_LOCAL && was_dropped)
		return;

	vy_log_tx_begin();
	vy_log_drop_index(index_id);
	if (vy_log_tx_try_commit() < 0)
		say_warn("failed to log drop index: %s",
			 diag_last_error(diag_get())->errmsg);
}

extern struct tuple_format_vtab vy_tuple_format_vtab;

/**
 * Create a tuple format with column mask of an update operation.
 * @sa vy_index.column_mask, vy_can_skip_update().
 * @param space_format A base tuple format.
 *
 * @retval not NULL Success.
 * @retval     NULL Memory or format register error.
 */
static inline struct tuple_format *
vy_tuple_format_new_with_colmask(struct tuple_format *space_format)
{
	struct tuple_format *format = tuple_format_dup(space_format);
	if (format == NULL)
		return NULL;
	/* + size of column mask. */
	assert(format->extra_size == 0);
	format->extra_size = sizeof(uint64_t);
	return format;
}

/**
 * Create a tuple format for UPSERT tuples. UPSERTs has an additional
 * extra byte before an offsets table, that stores the count
 * of squashed upserts @sa vy_squash.
 * @param space_format A base tuple format.
 *
 * @retval not NULL Success.
 * @retval     NULL Memory or format register error.
 */
static inline struct tuple_format *
vy_tuple_format_new_upsert(struct tuple_format *space_format)
{
	struct tuple_format *format = tuple_format_dup(space_format);
	if (format == NULL)
		return NULL;
	/* + size of n_upserts. */
	assert(format->extra_size == 0);
	format->extra_size = sizeof(uint8_t);
	return format;
}

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
		return NULL;
	}
	index->env = e;

	/* Original user defined index_def. */
	user_index_def = index_def_dup(user_index_def);
	if (user_index_def == NULL)
		goto fail_user_index_def;

	/*
	 * index_def that is used for extraction the key from a
	 * tuple.
	 */
	if (user_index_def->iid == 0) {
		index->index_def = user_index_def;
	} else {
		index->index_def = index_def_merge(user_index_def, pk->index_def);
		if (index->index_def == NULL)
			goto fail_index_def;
	}

	struct key_def *def = &index->index_def->key_def;

	index->surrogate_format = tuple_format_new(&vy_tuple_format_vtab,
						   &def, 1, 0);
	if (index->surrogate_format == NULL)
		goto fail_format;
	tuple_format_ref(index->surrogate_format, 1);

	if (user_index_def->iid == 0) {
		index->upsert_format =
			vy_tuple_format_new_upsert(space->format);
		if (index->upsert_format == NULL)
			goto fail_upsert_format;

		index->space_format_with_colmask =
			vy_tuple_format_new_with_colmask(space->format);
		if (index->space_format_with_colmask == NULL)
			goto fail_space_format_with_colmask;
	} else {
		index->space_format_with_colmask =
			pk->space_format_with_colmask;
		index->upsert_format = pk->upsert_format;
	}
	tuple_format_ref(index->space_format_with_colmask, 1);
	tuple_format_ref(index->upsert_format, 1);

	if (vy_index_conf_create(index, index->index_def))
		goto fail_conf;

	index->run_hist = histogram_new(run_buckets, lengthof(run_buckets));
	if (index->run_hist == NULL)
		goto fail_run_hist;

	if (user_index_def->iid > 0) {
		/**
		 * Calculate the bitmask of columns used in this
		 * index.
		 */
		for (uint32_t i = 0; i < user_index_def->key_def.part_count; ++i) {
			uint32_t fieldno = user_index_def->key_def.parts[i].fieldno;
			if (fieldno >= 64) {
				index->column_mask = UINT64_MAX;
				break;
			}
			index->column_mask |= ((uint64_t)1) << (63 - fieldno);
		}
	}

	index->cache = vy_cache_new(&e->cache_env, index->index_def);
	if (index->cache == NULL)
		goto fail_cache_init;

	vy_range_tree_new(&index->tree);
	index->version = 1;
	rlist_create(&index->link);
	read_set_new(&index->read_set);
	index->space = space;
	index->user_index_def = user_index_def;
	index->space_format = space->format;
	tuple_format_ref(index->space_format, 1);
	index->space_index_count = space->index_count;

	return index;

fail_cache_init:
	histogram_delete(index->run_hist);
fail_run_hist:
	free(index->name);
	free(index->path);
fail_conf:
	tuple_format_ref(index->space_format_with_colmask, -1);
fail_space_format_with_colmask:
	tuple_format_ref(index->upsert_format, -1);
fail_upsert_format:
	tuple_format_ref(index->surrogate_format, -1);
fail_format:
	if (user_index_def->iid > 0)
		index_def_delete(index->index_def);
fail_index_def:
	index_def_delete(user_index_def);
fail_user_index_def:
	free(index);
	return NULL;
}

int
vy_prepare_alter_space(struct space *old_space, struct space *new_space)
{
	if (old_space->index_count &&
	    old_space->index_count <= new_space->index_count) {
		struct vy_index *pk = vy_index(old_space->index[0]);
		if (pk->env->status == VINYL_ONLINE && pk->stmt_count != 0) {
			diag_set(ClientError, ER_UNSUPPORTED, "Vinyl",
				 "altering not empty space");
			return -1;
		}
	}
	return 0;
}

int
vy_commit_alter_space(struct space *old_space, struct space *new_space)
{
	(void) old_space;
	struct vy_index *pk = vy_index(new_space->index[0]);
	pk->space = new_space;

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
		index->space = new_space;
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
	struct vy_index *index = (struct vy_index *) arg;
	struct vy_scheduler *scheduler = index->env->scheduler;

	/*
	 * Exempt the range along with all its in-memory indexes
	 * from the scheduler.
	 */
	if (range->mem != NULL)
		vy_scheduler_mem_dumped(scheduler, range->mem);
	struct vy_mem *mem;
	rlist_foreach_entry(mem, &range->frozen, in_frozen)
		vy_scheduler_mem_dumped(scheduler, mem);
	if (range->in_dump.pos != UINT32_MAX) {
		/*
		 * The range could have already been removed
		 * by vy_schedule().
		 */
		vy_scheduler_remove_range(scheduler, range);
	}

	vy_range_delete(range);
	return NULL;
}

static void
vy_index_delete(struct vy_index *index)
{
	read_set_iter(&index->read_set, NULL, read_set_delete_cb, NULL);
	vy_range_tree_iter(&index->tree, NULL, vy_range_tree_free_cb, index);
	free(index->name);
	free(index->path);
	tuple_format_ref(index->surrogate_format, -1);
	tuple_format_ref(index->space_format_with_colmask, -1);
	tuple_format_ref(index->upsert_format, -1);
	if (index->index_def->iid > 0)
		index_def_delete(index->index_def);
	index_def_delete(index->user_index_def);
	histogram_delete(index->run_hist);
	vy_cache_delete(index->cache);
	tuple_format_ref(index->space_format, -1);
	TRASH(index);
	free(index);
}

size_t
vy_index_bsize(struct vy_index *index)
{
	return index->used;
}

/** {{{ Upsert */

static void *
vy_update_alloc(void *arg, size_t size)
{
	/* TODO: rewrite tuple_upsert_execute() without exceptions */
	struct region *region = (struct region *) arg;
	void *data = region_aligned_alloc(region, size, sizeof(uint64_t));
	if (data == NULL)
		diag_set(OutOfMemory, sizeof(struct vy_tx), "region",
			 "upsert");
	return data;
}

/**
 * vinyl wrapper of tuple_upsert_execute.
 * vibyl upsert opts are slightly different from tarantool ops,
 *  so they need some preparation before tuple_upsert_execute call.
 *  The function does this preparation.
 * On successfull upsert the result is placed into stmt and stmt_end args.
 * On fail the stmt and stmt_end args are not changed.
 * Possibly allocates new stmt via fiber region alloc,
 * so call fiber_gc() after usage
 */
static void
vy_apply_upsert_ops(struct region *region, const char **stmt,
		    const char **stmt_end, const char *ops,
		    const char *ops_end, bool suppress_error)
{
	if (ops == ops_end)
		return;

#ifndef NDEBUG
			const char *serie_end_must_be = ops;
			mp_next(&serie_end_must_be);
			assert(ops_end == serie_end_must_be);
#endif
		const char *result;
		uint32_t size;
		result = tuple_upsert_execute(vy_update_alloc, region,
					      ops, ops_end,
					      *stmt, *stmt_end,
					      &size, 0, suppress_error, NULL);
		if (result != NULL) {
			/* if failed, just skip it and leave stmt the same */
			*stmt = result;
			*stmt_end = result + size;
		}
}

const char *
space_name_by_id(uint32_t id);

/**
 * Try to squash two upsert series (msgspacked index_base + ops)
 * Try to create a tuple with squahed operations
 *
 * @retval 0 && *result_stmt != NULL : successful squash
 * @retval 0 && *result_stmt == NULL : unsquashable sources
 * @retval -1 - memory error
 */
static int
vy_upsert_try_to_squash(struct tuple_format *format, struct region *region,
			const char *key_mp, const char *key_mp_end,
			const char *old_serie, const char *old_serie_end,
			const char *new_serie, const char *new_serie_end,
			struct tuple **result_stmt)
{
	*result_stmt = NULL;

	size_t squashed_size;
	const char *squashed =
		tuple_upsert_squash(vy_update_alloc, region,
				    old_serie, old_serie_end,
				    new_serie, new_serie_end,
				    &squashed_size, 0);
	if (squashed == NULL)
		return 0;
	/* Successful squash! */
	struct iovec operations[1];
	operations[0].iov_base = (void *)squashed;
	operations[0].iov_len = squashed_size;

	*result_stmt = vy_stmt_new_upsert(format, key_mp, key_mp_end,
					  operations, 1);
	if (*result_stmt == NULL)
		return -1;
	return 0;
}

static struct tuple *
vy_apply_upsert(const struct tuple *new_stmt, const struct tuple *old_stmt,
		const struct index_def *index_def, struct tuple_format *format,
		struct tuple_format *upsert_format, bool suppress_error,
		struct vy_stat *stat)
{
	/*
	 * old_stmt - previous (old) version of stmt
	 * new_stmt - next (new) version of stmt
	 * result_stmt - the result of merging new and old
	 */
	assert(new_stmt != NULL);
	assert(new_stmt != old_stmt);
	assert(vy_stmt_type(new_stmt) == IPROTO_UPSERT);

	if (stat != NULL)
		rmean_collect(stat->rmean, VY_STAT_UPSERT_APPLIED, 1);

	if (old_stmt == NULL || vy_stmt_type(old_stmt) == IPROTO_DELETE) {
		/*
		 * INSERT case: return new stmt.
		 */
		return vy_stmt_replace_from_upsert(format, new_stmt);
	}

	/*
	 * Unpack UPSERT operation from the new stmt
	 */
	uint32_t mp_size;
	const char *new_ops;
	new_ops = vy_stmt_upsert_ops(new_stmt, &mp_size);
	const char *new_ops_end = new_ops + mp_size;

	/*
	 * Apply new operations to the old stmt
	 */
	const char *result_mp;
	if (vy_stmt_type(old_stmt) == IPROTO_REPLACE)
		result_mp = tuple_data_range(old_stmt, &mp_size);
	else
		result_mp = vy_upsert_data_range(old_stmt, &mp_size);
	const char *result_mp_end = result_mp + mp_size;
	struct tuple *result_stmt = NULL;
	struct region *region = &fiber()->gc;
	size_t region_svp = region_used(region);
	uint8_t old_type = vy_stmt_type(old_stmt);
	vy_apply_upsert_ops(region, &result_mp, &result_mp_end, new_ops,
			    new_ops_end, suppress_error);
	if (old_type != IPROTO_UPSERT) {
		assert(old_type == IPROTO_DELETE || old_type == IPROTO_REPLACE);
		/*
		 * UPDATE case: return the updated old stmt.
		 */
		result_stmt = vy_stmt_new_replace(format, result_mp,
						  result_mp_end);
		region_truncate(region, region_svp);
		if (result_stmt == NULL)
			return NULL; /* OOM */
		vy_stmt_set_lsn(result_stmt, vy_stmt_lsn(new_stmt));
		goto check_key;
	}

	/*
	 * Unpack UPSERT operation from the old stmt
	 */
	assert(old_stmt != NULL);
	const char *old_ops;
	old_ops = vy_stmt_upsert_ops(old_stmt, &mp_size);
	const char *old_ops_end = old_ops + mp_size;
	assert(old_ops_end > old_ops);

	/*
	 * UPSERT + UPSERT case: combine operations
	 */
	assert(old_ops_end - old_ops > 0);
	if (vy_upsert_try_to_squash(upsert_format, region,
				    result_mp, result_mp_end,
				    old_ops, old_ops_end,
				    new_ops, new_ops_end,
				    &result_stmt) != 0) {
		region_truncate(region, region_svp);
		return NULL;
	}
	if (result_stmt != NULL) {
		region_truncate(region, region_svp);
		vy_stmt_set_lsn(result_stmt, vy_stmt_lsn(new_stmt));
		goto check_key;
	}

	/* Failed to squash, simply add one upsert to another */
	int old_ops_cnt, new_ops_cnt;
	struct iovec operations[3];

	old_ops_cnt = mp_decode_array(&old_ops);
	operations[1].iov_base = (void *)old_ops;
	operations[1].iov_len = old_ops_end - old_ops;

	new_ops_cnt = mp_decode_array(&new_ops);
	operations[2].iov_base = (void *)new_ops;
	operations[2].iov_len = new_ops_end - new_ops;

	char ops_buf[16];
	char *header = mp_encode_array(ops_buf, old_ops_cnt + new_ops_cnt);
	operations[0].iov_base = (void *)ops_buf;
	operations[0].iov_len = header - ops_buf;

	result_stmt = vy_stmt_new_upsert(upsert_format, result_mp,
					 result_mp_end, operations, 3);
	region_truncate(region, region_svp);
	if (result_stmt == NULL)
		return NULL;
	vy_stmt_set_lsn(result_stmt, vy_stmt_lsn(new_stmt));

check_key:
	/*
	 * Check that key hasn't been changed after applying operations.
	 */
	if (index_def->iid == 0 &&
	    vy_tuple_compare(old_stmt, result_stmt, &index_def->key_def) != 0) {
		/*
		 * Key has been changed: ignore this UPSERT and
		 * @retval the old stmt.
		 */
		diag_set(ClientError, ER_CANT_UPDATE_PRIMARY_KEY,
			 index_def->name, space_name_by_id(index_def->space_id));
		error_log(diag_last_error(diag_get()));
		tuple_unref(result_stmt);
		result_stmt = vy_stmt_dup(old_stmt, old_type == IPROTO_UPSERT ?
						    upsert_format : format);
	}
	return result_stmt;
}

/* }}} Upsert */

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
			uint8_t old_type = vy_stmt_type(old->stmt);
			assert(old_type == IPROTO_UPSERT ||
			       old_type == IPROTO_REPLACE ||
			       old_type == IPROTO_DELETE);
			(void) old_type;

			stmt = vy_apply_upsert(stmt, old->stmt, index->index_def,
					       index->space_format,
					       index->upsert_format, true,
					       stat);
			if (stmt == NULL)
				return -1;
			assert(vy_stmt_type(stmt) != 0);
			rmean_collect(stat->rmean, VY_STAT_UPSERT_SQUASHED, 1);
		}
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
		stailq_add_tail_entry(&tx->log, v, next_in_log);
	}
	return 0;
}

/* {{{ Public API of transaction control: start/end transaction,
 * read, write data in the context of a transaction.
 */

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
	assert(part_count <= index->index_def->key_def.part_count);
	vykey = vy_stmt_new_select(e->key_format, key, part_count);
	if (vykey == NULL)
		return -1;
	ev_tstamp start  = ev_now(loop());
	int64_t vlsn = INT64_MAX;
	const int64_t *vlsn_ptr = &vlsn;
	if (tx != NULL)
		vlsn_ptr = &tx->vlsn;

	struct vy_read_iterator itr;
	vy_read_iterator_open(&itr, index, tx, ITER_EQ, vykey, vlsn_ptr, false);
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
 * @param index      Index in which to search.
 * @param key        MessagePack'ed data, the array without a
 *                   header.
 * @param part_count Part count of the key.
 *
 * @retval  0 Success, the key isn't found.
 * @retval -1 Memory error or the key is found.
 */
static inline int
vy_check_dup_key(struct vy_tx *tx, struct vy_index *idx, const char *key,
		 uint32_t part_count)
{
	struct tuple *found;
	(void) part_count;
	/*
	 * Expect a full tuple as input (secondary key || primary key)
	 * but use only  the secondary key fields (partial key look
	 * up) to check for duplicates.
         */
	assert(part_count == idx->index_def->key_def.part_count);
	if (vy_index_get(tx, idx, key, idx->user_index_def->key_def.part_count, &found))
		return -1;

	if (found) {
		tuple_unref(found);
		diag_set(ClientError, ER_TUPLE_FOUND, idx->user_index_def->name,
			 space_name(idx->space));
		return -1;
	}
	return 0;
}

/**
 * Insert a tuple in a primary index.
 * @param tx   Current transaction.
 * @param pk   Primary vinyl index.
 * @param stmt Tuple to insert.
 *
 * @retval  0 Success.
 * @retval -1 Memory error or duplicate key error.
 */
static inline int
vy_insert_primary(struct vy_tx *tx, struct vy_index *pk, struct tuple *stmt)
{
	assert(vy_stmt_type(stmt) == IPROTO_REPLACE);
	assert(tx != NULL && tx->state == VINYL_TX_READY);
	struct index_def *def = pk->index_def;
	const char *key;
	assert(def->iid == 0);
	key = tuple_extract_key(stmt, &def->key_def, NULL);
	if (key == NULL)
		return -1;
	/*
	 * A primary index is always unique and the new tuple must not
	 * conflict with existing tuples.
	 */
	uint32_t part_count = mp_decode_array(&key);
	if (vy_check_dup_key(tx, pk, key, part_count))
		return -1;
	return vy_tx_set(tx, pk, stmt);
}

/**
 * Insert a tuple in a secondary index.
 * @param tx        Current transaction.
 * @param index     Secondary index.
 * @param stmt      Tuple to replace.
 *
 * @retval  0 Success.
 * @retval -1 Memory error or duplicate key error.
 */
int
vy_insert_secondary(struct vy_tx *tx, struct vy_index *index,
		    struct tuple *stmt)
{
	assert(vy_stmt_type(stmt) == IPROTO_REPLACE);
	assert(tx != NULL && tx->state == VINYL_TX_READY);
	struct index_def *def = index->index_def;
	assert(def->iid > 0);
	/*
	 * If the index is unique then the new tuple must not
	 * conflict with existing tuples. If the index is not
	 * unique a conflict is impossible.
	 */
	if (index->user_index_def->opts.is_unique) {
		uint32_t key_len;
		const char *key = tuple_extract_key(stmt, &def->key_def,
						    &key_len);
		if (key == NULL)
			return -1;
		uint32_t part_count = mp_decode_array(&key);
		if (vy_check_dup_key(tx, index, key, part_count))
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
	struct index_def *def = pk->index_def;
	assert(def->iid == 0);
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
		key = tuple_extract_key(new_tuple, &def->key_def, NULL);
		if (key == NULL)                /* out of memory */
			return -1;
		uint32_t part_count = mp_decode_array(&key);
		if (vy_get(tx, pk, key, part_count, &stmt->old_tuple))
			return -1;
	}
	if (vy_tx_set(tx, pk, new_tuple)) {
		tuple_unref(new_tuple);
		return -1;
	}
	if (stmt != NULL)
		stmt->new_tuple = new_tuple;
	else
		tuple_unref(new_tuple);
	return 0;
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
	struct index_def *def = pk->index_def;
	assert(def->iid == 0);
	new_stmt = vy_stmt_new_replace(space->format, request->tuple,
				       request->tuple_end);
	if (new_stmt == NULL)
		return -1;
	const char *key = tuple_extract_key(new_stmt, &def->key_def, NULL);
	if (key == NULL) /* out of memory */
		goto error;
	uint32_t part_count = mp_decode_array(&key);

	/* Get full tuple from the primary index. */
	if (vy_index_get(tx, pk, key, part_count, &old_stmt) != 0)
		return -1;
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
		/*
		 * Delete goes first, so if old and new keys
		 * fully match, there is no look up beyond the
		 * transaction index.
		 */
		if (old_stmt != NULL) {
			if (vy_tx_set(tx, index, delete) != 0)
				goto error;
		}
		if (vy_insert_secondary(tx, index, new_stmt) != 0)
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
	struct index_def *def = index->index_def;
	assert(def->opts.is_unique);
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
	uint32_t original_part_count = index->user_index_def->key_def.part_count;
	if (original_part_count != part_count) {
		diag_set(ClientError, ER_EXACT_MATCH,
			 original_part_count, part_count);
		return -1;
	}
	return key_validate_parts(def, key, part_count);
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
	assert(index->index_def->iid > 0);
	/*
	 * Fetch the primary key from the secondary index tuple.
	 */
	struct index_def *to_pk = vy_index(index->space->index[0])->index_def;
	uint32_t size;
	const char *tuple = tuple_data_range(partial, &size);
	const char *tuple_end = tuple + size;
	const char *pkey = tuple_extract_key_raw(tuple, tuple_end,
						 &to_pk->key_def, NULL);
	if (pkey == NULL)
		return -1;
	/* Fetch the tuple from the primary index. */
	uint32_t part_count = mp_decode_array(&pkey);
	assert(part_count == to_pk->key_def.part_count);
	struct space *space = index->space;
	struct vy_index *pk = vy_index_find(space, 0);
	assert(pk != NULL);
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
	if (index->index_def->iid == 0 || found == NULL) {
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
vy_delete_impl(struct vy_tx *tx, struct space *space, const struct tuple *tuple)
{
	struct vy_index *pk = vy_index_find(space, 0);
	if (pk == NULL)
		return -1;
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
		assert(index->index_def->iid == 0);
		struct tuple *delete =
			vy_stmt_new_surrogate_delete_from_key(request->key,
					&pk->index_def->key_def, space->format);
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
 *
 * @retval  0 Success, the primary key is not modified in the new
 *            tuple.
 * @retval -1 Attempt to modify the primary key.
 */
static inline int
vy_check_update(const struct vy_index *pk, const struct tuple *old_tuple,
		const struct tuple *new_tuple)
{
	if (vy_tuple_compare(old_tuple, new_tuple, &pk->index_def->key_def)) {
		diag_set(ClientError, ER_CANT_UPDATE_PRIMARY_KEY,
			 pk->index_def->name,
			 space_name_by_id(pk->index_def->space_id));
		return -1;
	}
	return 0;
}

/**
 * Don't modify indexes whose fields were not changed by update.
 * If there is at least one bit in the column mask
 * (@sa update_read_ops in tuple_update.cc) set that corresponds
 * to one of the columns from index_def->parts, then the update
 * operation changes at least one indexed field and the
 * optimization is inapplicable. Otherwise, we can skip the
 * update.
 * @param idx         Secondary index which we try to update.
 * @param column_mask Maks of the update operations.
 */
static bool
vy_can_skip_update(const struct vy_index *idx, uint64_t column_mask)
{
	/*
	 * Update of the primary index can't be skipped, since it
	 * stores not indexes tuple fields besides indexed.
	 */
	assert(idx->index_def->iid > 0);
	return (column_mask & idx->column_mask) == 0;
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
		if (vy_can_skip_update(index, column_mask))
			return false;
	}
	return true;
}

int
vy_update(struct vy_tx *tx, struct txn_stmt *stmt, struct space *space,
	  struct request *request)
{
	assert(tx != NULL && tx->state == VINYL_TX_READY);
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
	assert(pk->index_def->iid == 0);
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
	if (vy_check_update(pk, stmt->old_tuple, stmt->new_tuple))
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
		if (vy_tx_set(tx, index, delete) != 0)
			goto error;
		if (vy_insert_secondary(tx, index, stmt->new_tuple))
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
	assert(pk->index_def->iid == 0);
	if (vy_tx_set(tx, pk, stmt) != 0)
		return -1;
	struct vy_index *index;
	for (uint32_t i = 1; i < space->index_count; ++i) {
		index = vy_index(space->index[i]);
		if (vy_insert_secondary(tx, index, stmt) != 0)
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
int
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
	if (tuple_validate_raw(space->format, tuple))
		return -1;

	if (space->index_count == 1 && rlist_empty(&space->on_replace))
		return vy_index_upsert(tx, pk, tuple, tuple_end, ops, ops_end);

	const char *old_tuple, *old_tuple_end;
	const char *new_tuple, *new_tuple_end;
	uint32_t new_size;
	const char *key;
	uint32_t part_count;
	struct index_def *pk_def = pk->index_def;
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
	key = tuple_extract_key_raw(tuple, tuple_end, &pk_def->key_def, NULL);
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
	if (vy_check_update(pk, stmt->old_tuple, stmt->new_tuple)) {
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
		if (vy_tx_set(tx, index, delete) != 0)
			goto error;
		if (vy_insert_secondary(tx, index, stmt->new_tuple) != 0)
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
	assert(pk->index_def->iid == 0);
	/* First insert into the primary index. */
	stmt->new_tuple =
		vy_stmt_new_replace(space->format, request->tuple,
				    request->tuple_end);
	if (stmt->new_tuple == NULL)
		return -1;
	if (vy_insert_primary(tx, pk, stmt->new_tuple) != 0)
		return -1;

	for (uint32_t iid = 1; iid < space->index_count; ++iid) {
		struct vy_index *index = vy_index(space->index[iid]);
		if (vy_insert_secondary(tx, index, stmt->new_tuple) != 0)
			return -1;
	}
	return 0;
}

int
vy_replace(struct vy_tx *tx, struct txn_stmt *stmt, struct space *space,
	   struct request *request)
{
	struct vy_env *env = tx->manager->env;
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
	tx->start = ev_now(loop());
	tx->manager = xm;
	tx->state = VINYL_TX_READY;
	tx->is_in_read_view = false;
	rlist_create(&tx->in_read_views);
	rlist_create(&tx->cursors);

	/* possible read-write tx reads latest changes */
	tx->vlsn = INT64_MAX;
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
	struct tx_manager *xm = tx->manager;
	rlist_del_entry(tx, in_read_views);

	/** Abort all open cursors. */
	vy_tx_abort_cursors(tx);

	/* Remove from the conflict manager index */
	struct txv *v;
	stailq_foreach_entry(v, &tx->log, next_in_log)
		if (v->is_read)
			read_set_remove(&v->index->read_set, v);

	xm->tx_count--;
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
	if (tx->is_in_read_view)
		return 0; /* no reason to track reads */
	uint32_t part_count = tuple_field_count(key);
	if (part_count >= index->index_def->key_def.part_count) {
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
static void
vy_tx_send_to_read_view(struct vy_tx *tx, struct txv *v)
{
	struct tx_manager *xm = tx->manager;
	read_set_t *tree = &v->index->read_set;
	struct key_def *key_def = &v->index->index_def->key_def;
	struct read_set_key key;
	key.stmt = v->stmt;
	key.tx = NULL;
	/** Find the first value equal to or greater than key */
	for (struct txv *abort = read_set_nsearch(tree, &key);
	     abort != NULL; abort = read_set_next(tree, abort)) {
		/* Check if we're still looking at the matching key. */
		if (vy_stmt_compare(key.stmt, abort->stmt, key_def))
			break;
		/* Don't abort self. */
		if (abort->tx == tx)
			continue;
		/* Delete of nothing does not cause a conflict */
		if (abort->is_gap && vy_stmt_type(v->stmt) == IPROTO_DELETE)
			continue;
		/* already in (earlier) read view */
		if (abort->tx->is_in_read_view)
			continue;

		/* the found tx can only be commited as read-only */
		abort->tx->is_in_read_view = true;
		abort->tx->vlsn = xm->lsn;
		rlist_add_tail_entry(&xm->read_views, abort->tx, in_read_views);
		assert(abort->tx->vlsn <= abort->tx->manager->lsn);
	}
}

int
vy_prepare(struct vy_tx *tx)
{
	/* prepare transaction */
	assert(tx->state == VINYL_TX_READY);
	struct vy_env *e = tx->manager->env;
	int rc = 0;

	/* proceed read-only transactions */
	if (!vy_tx_is_ro(tx) && tx->is_in_read_view) {
		tx->state = VINYL_TX_ROLLBACK;
		e->stat->tx_conflict++;
		diag_set(ClientError, ER_TRANSACTION_CONFLICT);
		rc = -1;
	} else {
		tx->state = VINYL_TX_COMMIT;
		for (struct txv *v = write_set_first(&tx->write_set);
		     v != NULL; v = write_set_next(&tx->write_set, v)) {
			if (vy_tx_write_prepare(v) != 0)
				rc = -1;
			/* Abort read/write intersection. */
			vy_tx_send_to_read_view(tx, v);
		}
	}

	vy_tx_destroy(tx);

	/*
	 * A half committed transaction is no longer
	 * being part of concurrent index, but still can be
	 * committed or rolled back.
	 * Yet, it is important to maintain external
	 * serial commit order.
	 */
	return rc;
}

int
vy_commit(struct vy_tx *tx, int64_t lsn)
{
	struct vy_env *e = tx->manager->env;
	assert(tx->state == VINYL_TX_COMMIT);
	if (lsn > e->xm->lsn)
		e->xm->lsn = lsn;

	struct txv *v, *tmp;
	struct vy_quota *quota = &e->quota;
	struct lsregion *allocator = &e->allocator;
	size_t mem_used_before = lsregion_used(allocator);
	/*
	 * Flush transactional changes to the index.
	 * Sic: the loop below must not yield after recovery.
	 */
	uint64_t write_count = 0;
	const struct tuple *delete = NULL, *replace = NULL;
	enum vy_status status = e->status;
	MAYBE_UNUSED uint32_t current_space_id = 0;
	stailq_foreach_entry(v, &tx->log, next_in_log) {
		/*
		 * The tx log contains both reads and writes, but
		 * we must save only writes.
		 */
		if (v->is_read)
			continue;
		struct vy_index *index = v->index;
		struct tuple *stmt = v->stmt;
		vy_stmt_set_lsn(stmt, lsn);
		enum iproto_type type = vy_stmt_type(stmt);
		if (index->index_def->iid == 0) {
			/*
			 * The beginning of the new txn_stmt is met.
			 */
			current_space_id = index->space->def.id;
			replace = NULL;
			delete = NULL;
		}
		assert(index->space->def.id == current_space_id);
		/*
		 * In secondary indexes only REPLACE and DELETE
		 * can be wrote.
		 */
		const struct tuple **region_stmt =
			(type == IPROTO_DELETE) ? &delete : &replace;
		if (vy_tx_write(index, v->mem, stmt, region_stmt, status) != 0)
			return -1;
		write_count++;
	}

	uint32_t count = 0;
	stailq_foreach_entry_safe(v, tmp, &tx->log, next_in_log) {
		count++;
		txv_delete(v);
	}
	size_t mem_used_after = lsregion_used(allocator);
	assert(mem_used_after >= mem_used_before);
	size_t write_size = mem_used_after - mem_used_before;
	vy_stat_tx(e->stat, tx->start, count, write_count, write_size);

	TRASH(tx);
	free(tx);

	vy_quota_use(quota, write_size);
	return 0;
}

static void
vy_tx_rollback(struct vy_tx *tx)
{
	if (tx->state == VINYL_TX_READY) {
		/** freewill rollback, vy_prepare have not been called yet */
		vy_tx_destroy(tx);
	}
	struct txv *v, *tmp;
	stailq_foreach_entry_safe(v, tmp, &tx->log, next_in_log)
		txv_delete(v);
	tx->manager->env->stat->tx_rlb++;
}

struct vy_tx *
vy_begin(struct vy_env *e)
{
	struct vy_tx *tx;
	tx = malloc(sizeof(struct vy_tx));
	if (unlikely(tx == NULL)) {
		diag_set(OutOfMemory, sizeof(struct vy_tx), "malloc",
			 "struct vy_tx");
		return NULL;
	}
	vy_tx_create(e->xm, tx);
	return tx;
}

void
vy_rollback(struct vy_tx *tx)
{
	for (struct txv *v = write_set_first(&tx->write_set);
	     v != NULL; v = write_set_next(&tx->write_set, v)) {
		if (v->mem != NULL)
			vy_mem_unpin(v->mem);
	}
	vy_tx_rollback(tx);
	TRASH(tx);
	free(tx);
}


void *
vy_savepoint(struct vy_tx *tx)
{
	return stailq_last(&tx->log);
}

void
vy_rollback_to_savepoint(struct vy_tx *tx, void *svp)
{
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
	assert(part_count <= index->index_def->key_def.part_count);
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

	size_t max_range_size = 0;
	struct heap_iterator it;
	vy_dump_heap_iterator_init(&e->scheduler->dump_heap, &it);
	struct heap_node *pn = vy_dump_heap_iterator_next(&it);
	if (pn != NULL) {
		struct vy_range *range = container_of(pn, struct vy_range,
						      in_dump);
		max_range_size = range->used;
	}

	vy_quota_update_watermark(&e->quota, max_range_size,
				  tx_write_rate, dump_bandwidth);
}

/** Destructor for env->zdctx_key thread-local variable */
static void
vy_free_zdctx(void *arg)
{
	assert(arg != NULL);
	ZSTD_freeDStream(arg);
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
	rlist_create(&e->indexes);
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
	mempool_create(&e->read_task_pool, slab_cache,
		       sizeof(struct vy_page_read_task));
	lsregion_create(&e->allocator, slab_cache->arena);
	tt_pthread_key_create(&e->zdctx_key, vy_free_zdctx);

	vy_quota_init(&e->quota, vy_scheduler_quota_cb, e->scheduler);
	ev_timer_init(&e->quota_timer, vy_env_quota_timer_cb, 0, 1.);
	e->quota_timer.data = e;
	ev_timer_start(loop(), &e->quota_timer);
	vy_cache_env_create(&e->cache_env, slab_cache,
			    e->conf->cache);
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
	struct vy_index *index, *tmp;
	rlist_foreach_entry_safe(index, &e->indexes, link, tmp)
		vy_index_unref(index);
	ev_timer_stop(loop(), &e->quota_timer);
	vy_squash_queue_delete(e->squash_queue);
	vy_scheduler_delete(e->scheduler);
	tx_manager_delete(e->xm);
	vy_conf_delete(e->conf);
	vy_stat_delete(e->stat);
	tuple_format_ref(e->key_format, -1);
	mempool_destroy(&e->cursor_pool);
	mempool_destroy(&e->read_task_pool);
	lsregion_destroy(&e->allocator);
	tt_pthread_key_delete(e->zdctx_key);
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
vy_begin_initial_recovery(struct vy_env *e, struct vclock *vclock)
{
	assert(e->status == VINYL_OFFLINE);
	if (vclock != NULL) {
		e->xm->lsn = vclock_sum(vclock);
		e->status = VINYL_INITIAL_RECOVERY_LOCAL;
		e->recovery = vy_log_begin_recovery(vclock);
		if (e->recovery == NULL)
			return -1;
	} else {
		e->xm->lsn = 0;
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

/**
 * Callback passed to vy_recovery_iterate() to delete files
 * left from incomplete runs.
 *
 * If the instance is shut down while a dump/compaction task
 * is in progress, we'll get an unfinished run file on disk,
 * i.e. a run file which was either not written to the end
 * or not inserted into a range. We need to delete such runs
 * on recovery.
 */
static int
vy_end_recovery_cb(const struct vy_log_record *record, void *cb_arg)
{
	struct vy_env *env = cb_arg;

	if (record->type == VY_LOG_PREPARE_RUN)
		vy_run_record_gc(env->conf->path, record);
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

		vy_recovery_iterate(e->recovery, true,
				    vy_end_recovery_cb, e);
		vy_recovery_delete(e->recovery);
		e->recovery = NULL;
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

/* {{{ vy_run_itr API forward declaration */
/* TODO: move to header (with struct vy_run_itr) and remove static keyword */

/** Position of a particular stmt in vy_run. */
struct vy_run_iterator_pos {
	uint32_t page_no;
	uint32_t pos_in_page;
};

/**
 * Return statements from vy_run based on initial search key,
 * iteration order and view lsn.
 *
 * All statements with lsn > vlsn are skipped.
 * The API allows to traverse over resulting statements within two
 * dimensions - key and lsn. next_key() switches to the youngest
 * statement of the next key, according to the iteration order,
 * and next_lsn() switches to an older statement for the same
 * key.
 */
struct vy_run_iterator {
	/** Parent class, must be the first member */
	struct vy_stmt_iterator base;
	/** Usage statistics */
	struct vy_iterator_stat *stat;

	/* Members needed for memory allocation and disk access */
	/* index */
	struct vy_index *index;
	/**
	 * Format ot allocate REPLACE and DELETE tuples read from
	 * pages.
	 */
	struct tuple_format *format;
	/** Same as format, but for UPSERT tuples. */
	struct tuple_format *upsert_format;
	/* run */
	struct vy_run *run;

	/* Search options */
	/**
	 * Iterator type, that specifies direction, start position and stop
	 * criteria if the key is not specified, GT and EQ are changed to
	 * GE, LT to LE for beauty.
	 */
	enum iterator_type iterator_type;
	/** Key to search. */
	const struct tuple *key;
	/* LSN visibility, iterator shows values with lsn <= vlsn */
	const int64_t *vlsn;

	/* State of the iterator */
	/** Position of the current record */
	struct vy_run_iterator_pos curr_pos;
	/**
	 * Last stmt returned by vy_run_iterator_get.
	 * The iterator holds this stmt until the next call to
	 * vy_run_iterator_get, when it's dereferenced.
	 */
	struct tuple *curr_stmt;
	/** Position of record that spawned curr_stmt */
	struct vy_run_iterator_pos curr_stmt_pos;
	/** LRU cache of two active pages (two pages is enough). */
	struct vy_page *curr_page;
	struct vy_page *prev_page;
	/** Is false until first .._get or .._next_.. method is called */
	bool search_started;
	/** Search is finished, you will not get more values from iterator */
	bool search_ended;
};

static void
vy_run_iterator_open(struct vy_run_iterator *itr, struct vy_iterator_stat *stat,
		     struct vy_index *index, struct vy_run *run,
		     enum iterator_type iterator_type,
		     const struct tuple *key, const int64_t *vlsn,
		     struct tuple_format *format,
		     struct tuple_format *upsert_format);

/* }}} vy_run_iterator API forward declaration */

/* {{{ vy_run_iterator vy_run_iterator support functions */
/* TODO: move to appropriate c file and remove */

/**
 * Page
 */
struct vy_page {
	/** Page position in the run file (used by run_iterator->page_cache */
	uint32_t page_no;
	/** The number of statements */
	uint32_t count;
	/** Page data size */
	uint32_t unpacked_size;
	/** Array with row offsets in page data */
	uint32_t *page_index;
	/** Page data */
	char *data;
};

static struct vy_page *
vy_page_new(const struct vy_page_info *page_info)
{
	struct vy_page *page = malloc(sizeof(*page));
	if (page == NULL) {
		diag_set(OutOfMemory, sizeof(*page),
			"load_page", "page cache");
		return NULL;
	}
	page->count = page_info->count;
	page->unpacked_size = page_info->unpacked_size;
	page->page_index = calloc(page_info->count, sizeof(uint32_t));
	if (page->page_index == NULL) {
		diag_set(OutOfMemory, page_info->count * sizeof(uint32_t),
			 "malloc", "page->page_index");
		free(page);
		return NULL;
	}

	page->data = (char *)malloc(page_info->unpacked_size);
	if (page->data == NULL) {
		diag_set(OutOfMemory, page_info->unpacked_size,
			 "malloc", "page->data");
		free(page->page_index);
		free(page);
		return NULL;
	}
	return page;
}

static void
vy_page_delete(struct vy_page *page)
{
	uint32_t *page_index = page->page_index;
	char *data = page->data;
#if !defined(NDEBUG)
	memset(page->page_index, '#', sizeof(uint32_t) * page->count);
	memset(page->data, '#', page->unpacked_size);
	memset(page, '#', sizeof(*page));
#endif /* !defined(NDEBUG) */
	free(page_index);
	free(data);
	free(page);
}

static int
vy_page_xrow(struct vy_page *page, uint32_t stmt_no,
	     struct xrow_header *xrow)
{
	assert(stmt_no < page->count);
	const char *data = page->data + page->page_index[stmt_no];
	const char *data_end = stmt_no + 1 < page->count ?
		page->data + page->page_index[stmt_no + 1] :
		page->data + page->unpacked_size;
	return xrow_header_decode(xrow, &data, data_end);
}

/**
 * Read raw stmt data from the page
 * @param page          Page.
 * @param stmt_no       Statement position in the page.
 * @param format        Format for REPLACE/DELETE tuples.
 * @param upsert_format Format for UPSERT tuples.
 * @param index_def       Key definition of an index.
 *
 * @retval not NULL Statement read from page.
 * @retval     NULL Memory error.
 */
static struct tuple *
vy_page_stmt(struct vy_page *page, uint32_t stmt_no,
	     struct tuple_format *format, struct tuple_format *upsert_format,
	     struct index_def *index_def)
{
	struct xrow_header xrow;
	if (vy_page_xrow(page, stmt_no, &xrow) != 0)
		return NULL;
	struct tuple_format *format_to_use = (xrow.type == IPROTO_UPSERT)
		? upsert_format : format;
	return vy_stmt_decode(&xrow, &index_def->key_def, format_to_use,
			      index_def->iid == 0);
}

/**
 * Get page from LRU cache
 * @retval page if found
 * @retval NULL otherwise
 */
static struct vy_page *
vy_run_iterator_cache_get(struct vy_run_iterator *itr, uint32_t page_no)
{
	if (itr->curr_page != NULL) {
		if (itr->curr_page->page_no == page_no)
			return itr->curr_page;
		if (itr->prev_page != NULL &&
		    itr->prev_page->page_no == page_no) {
			struct vy_page *result = itr->prev_page;
			itr->prev_page = itr->curr_page;
			itr->curr_page = result;
			return result;
		}
	}
	return NULL;
}

/**
 * Touch page in LRU cache.
 * The cache is at least two pages. Ensure that subsequent read keeps
 * the page_no in the cache by moving it to the start of LRU list.
 * @pre page must be in the cache
 */
static void
vy_run_iterator_cache_touch(struct vy_run_iterator *itr, uint32_t page_no)
{
	struct vy_page *page = vy_run_iterator_cache_get(itr, page_no);
	assert(page != NULL);
	(void) page;
}

/**
 * Put page to LRU cache
 */
static void
vy_run_iterator_cache_put(struct vy_run_iterator *itr, struct vy_page *page,
			  uint32_t page_no)
{
	if (itr->prev_page != NULL)
		vy_page_delete(itr->prev_page);
	itr->prev_page = itr->curr_page;
	itr->curr_page = page;
	page->page_no = page_no;
}

/**
 * Clear LRU cache
 */
static void
vy_run_iterator_cache_clean(struct vy_run_iterator *itr)
{
	if (itr->curr_stmt != NULL) {
		tuple_unref(itr->curr_stmt);
		itr->curr_stmt = NULL;
		itr->curr_stmt_pos.page_no = UINT32_MAX;
	}
	if (itr->curr_page != NULL) {
		vy_page_delete(itr->curr_page);
		if (itr->prev_page != NULL)
			vy_page_delete(itr->prev_page);
		itr->curr_page = itr->prev_page = NULL;
	}
}

static int
vy_page_index_decode(uint32_t *page_index, uint32_t count,
		    struct xrow_header *xrow)
{
	assert(xrow->type == VY_RUN_PAGE_INDEX);
	const char *pos = xrow->body->iov_base;
	uint32_t map_size = mp_decode_map(&pos);
	uint32_t map_item;
	uint32_t size = 0;
	for (map_item = 0; map_item < map_size; ++map_item) {
		uint32_t key = mp_decode_uint(&pos);
		switch (key) {
		case VY_PAGE_INDEX_INDEX:
			size = mp_decode_binl(&pos);
			break;
		}
	}
	if (size != sizeof(uint32_t) * count) {
		diag_set(ClientError, ER_VINYL, "Invalid page index size");
		return -1;
	}
	for (uint32_t i = 0; i < count; ++i) {
		page_index[i] = mp_load_u32(&pos);
	}
	assert(pos == xrow->body->iov_base + xrow->body->iov_len);
	return 0;
}

/**
 * Read a page requests from vinyl xlog data file.
 *
 * @retval 0 on success
 * @retval -1 on error, check diag
 */
static int
vy_page_read(struct vy_page *page, const struct vy_page_info *page_info, int fd,
	     ZSTD_DStream *zdctx)
{
	/* read xlog tx from xlog file */
	size_t region_svp = region_used(&fiber()->gc);
	char *data = (char *)region_alloc(&fiber()->gc, page_info->size);
	if (data == NULL) {
		diag_set(OutOfMemory, page_info->size, "region gc", "page");
		return -1;
	}
	ssize_t readen = fio_pread(fd, data, page_info->size,
				   page_info->offset);
	if (readen < 0) {
		/* TODO: report filename */
		diag_set(SystemError, "failed to read from file");
		goto error;
	}
	if (readen != (ssize_t)page_info->size) {
		/* TODO: replace with XlogError, report filename */
		diag_set(ClientError, ER_VINYL, "Unexpected end of file");
		goto error;
	}
	ERROR_INJECT(ERRINJ_VY_READ_PAGE_TIMEOUT, {usleep(50000);});

	/* decode xlog tx */
	const char *data_pos = data;
	const char *data_end = data + readen;
	char *rows = page->data;
	char *rows_end = rows + page_info->unpacked_size;
	if (xlog_tx_decode(data, data_end, rows, rows_end, zdctx) != 0)
		goto error;

	struct xrow_header xrow;
	data_pos = page->data + page_info->page_index_offset;
	data_end = page->data + page_info->unpacked_size;
	if (xrow_header_decode(&xrow, &data_pos, data_end) == -1)
		goto error;
	if (xrow.type != VY_RUN_PAGE_INDEX) {
		diag_set(ClientError, ER_VINYL, "Invalid page index type");
		goto error;
	}
	if (vy_page_index_decode(page->page_index, page->count, &xrow) != 0)
		goto error;
	region_truncate(&fiber()->gc, region_svp);
	ERROR_INJECT(ERRINJ_VY_READ_PAGE, {
		diag_set(ClientError, ER_VINYL, "page read injection");
		return -1;});
	return 0;
error:
	region_truncate(&fiber()->gc, region_svp);
	return -1;
}

/**
 * Get thread local zstd decompression context
 */
static ZSTD_DStream *
vy_env_get_zdctx(struct vy_env *env)
{
	ZSTD_DStream *zdctx = tt_pthread_getspecific(env->zdctx_key);
	if (zdctx == NULL) {
		zdctx = ZSTD_createDStream();
		if (zdctx == NULL) {
			diag_set(OutOfMemory, sizeof(zdctx), "malloc",
				 "zstd context");
			return NULL;
		}
		tt_pthread_setspecific(env->zdctx_key, zdctx);
	}
	return zdctx;
}

/**
 * vinyl read task callback
 */
static int
vy_page_read_cb(struct coio_task *base)
{
	struct vy_page_read_task *task = (struct vy_page_read_task *)base;
	ZSTD_DStream *zdctx = vy_env_get_zdctx(task->env);
	if (zdctx == NULL)
		return -1;
	task->rc = vy_page_read(task->page, &task->page_info,
				task->run->fd, zdctx);
	return task->rc;
}

/**
 * vinyl read task cleanup callback
 */
static int
vy_page_read_cb_free(struct coio_task *base)
{
	struct vy_page_read_task *task = (struct vy_page_read_task *)base;
	vy_page_delete(task->page);
	vy_run_unref(task->run);
	coio_task_destroy(&task->base);
	mempool_free(&task->env->read_task_pool, task);
	return 0;
}

/**
 * Get a page by the given number the cache or load it from the disk.
 *
 * @retval 0 success
 * @retval -1 critical error
 * @retval -2 invalid iterator
 */
static NODISCARD int
vy_run_iterator_load_page(struct vy_run_iterator *itr, uint32_t page_no,
			  struct vy_page **result)
{
	struct vy_index *index = itr->index;
	const struct vy_env *env = index->env;

	/* Check cache */
	*result = vy_run_iterator_cache_get(itr, page_no);
	if (*result != NULL)
		return 0;

	/* Allocate buffers */
	struct vy_page_info *page_info = vy_run_page_info(itr->run, page_no);
	struct vy_page *page = vy_page_new(page_info);
	if (page == NULL)
		return -1;

	/* Read page data from the disk */
	int rc;
	if (cord_is_main() && env->status == VINYL_ONLINE) {
		/*
		 * Use coeio for TX thread **after recovery**.
		 * Please note that vy_run can go away after yield.
		 * In this case vy_run_iterator is no more valid and
		 * rc = -2 is returned to the caller.
		 */

		/* Allocate a coio task */
		struct vy_page_read_task *task =
			(struct vy_page_read_task *)mempool_alloc(&itr->index->env->read_task_pool);
		if (task == NULL) {
			diag_set(OutOfMemory, sizeof(*task), "malloc",
				 "vy_page_read_task");
			return -1;
		}
		coio_task_create(&task->base, vy_page_read_cb,
				  vy_page_read_cb_free);

		/*
		 * Make sure the run file descriptor won't be closed
		 * (even worse, reopened) while a coeio thread is
		 * reading it.
		 */
		task->run = itr->run;
		vy_run_ref(task->run);
		task->page_info = *page_info;
		task->env = index->env;
		task->page = page;

		/* Post task to coeio */
		rc = coio_task_post(&task->base, TIMEOUT_INFINITY);
		if (rc < 0)
			return -1; /* timed out or cancelled */

		if (task->rc != 0) {
			/* posted, but failed */
			diag_move(&task->base.diag, &fiber()->diag);
			vy_page_read_cb_free(&task->base);
			return -1;
		}

		coio_task_destroy(&task->base);
		mempool_free(&task->env->read_task_pool, task);

		if (vy_run_unref(itr->run)) {
			/*
			 * The run's gone so the iterator isn't
			 * valid anymore.
			 */
			itr->index = NULL;
			itr->run = NULL;
			vy_page_delete(page);
			return -2;
		}
	} else {
		/*
		 * Optimization: use blocked I/O for non-TX threads or
		 * during WAL recovery (env->status != VINYL_ONLINE).
		 */
		ZSTD_DStream *zdctx = vy_env_get_zdctx(itr->index->env);
		if (zdctx == NULL) {
			vy_page_delete(page);
			return -1;
		}
		if (vy_page_read(page, page_info, itr->run->fd, zdctx) != 0) {
			vy_page_delete(page);
			return -1;
		}
	}

	/* Iterator is never used from multiple fibers */
	assert(vy_run_iterator_cache_get(itr, page_no) == NULL);

	/* Update cache */
	vy_run_iterator_cache_put(itr, page, page_no);

	*result = page;
	return 0;
}

/**
 * Read key and lsn by a given wide position.
 * For the first record in a page reads the result from the page
 * index instead of fetching it from disk.
 *
 * @retval 0 success
 * @retval -1 read error or out of memory.
 * @retval -2 invalid iterator
 */
static NODISCARD int
vy_run_iterator_read(struct vy_run_iterator *itr,
		     struct vy_run_iterator_pos pos,
		     struct tuple **stmt)
{
	struct vy_page *page;
	int rc = vy_run_iterator_load_page(itr, pos.page_no, &page);
	if (rc != 0)
		return rc;
	*stmt = vy_page_stmt(page, pos.pos_in_page, itr->format,
			     itr->upsert_format, itr->index->index_def);
	if (*stmt == NULL)
		return -1;
	return 0;
}

/**
 * Binary search in page index
 * In terms of STL, makes lower_bound for EQ,GE,LT and upper_bound for GT,LE
 * Additionally *equal_key argument is set to true if the found value is
 * equal to given key (untouched otherwise)
 * @retval page number
 */
static uint32_t
vy_run_iterator_search_page(struct vy_run_iterator *itr,
			    const struct tuple *key, bool *equal_key)
{
	uint32_t beg = 0;
	uint32_t end = itr->run->info.count;
	/* for upper bound we change zero comparison result to -1 */
	int zero_cmp = itr->iterator_type == ITER_GT ||
		       itr->iterator_type == ITER_LE ? -1 : 0;
	struct vy_index *idx = itr->index;
	while (beg != end) {
		uint32_t mid = beg + (end - beg) / 2;
		struct vy_page_info *page_info;
		page_info = vy_run_page_info(itr->run, mid);
		int cmp;
		cmp = -vy_stmt_compare_with_raw_key(key, page_info->min_key,
						    &idx->index_def->key_def);
		cmp = cmp ? cmp : zero_cmp;
		*equal_key = *equal_key || cmp == 0;
		if (cmp < 0)
			beg = mid + 1;
		else
			end = mid;
	}
	return end;
}

/**
 * Binary search in page
 * In terms of STL, makes lower_bound for EQ,GE,LT and upper_bound for GT,LE
 * Additionally *equal_key argument is set to true if the found value is
 * equal to given key (untouched otherwise)
 * @retval position in the page
 */
static uint32_t
vy_run_iterator_search_in_page(struct vy_run_iterator *itr,
			       const struct tuple *key, struct vy_page *page,
			       bool *equal_key)
{
	uint32_t beg = 0;
	uint32_t end = page->count;
	/* for upper bound we change zero comparison result to -1 */
	int zero_cmp = itr->iterator_type == ITER_GT ||
		       itr->iterator_type == ITER_LE ? -1 : 0;
	struct vy_index *idx = itr->index;
	while (beg != end) {
		uint32_t mid = beg + (end - beg) / 2;
		struct tuple *fnd_key = vy_page_stmt(page, mid, itr->format,
						     itr->upsert_format,
						     itr->index->index_def);
		if (fnd_key == NULL)
			return end;
		int cmp = vy_stmt_compare(fnd_key, key, &idx->index_def->key_def);
		cmp = cmp ? cmp : zero_cmp;
		*equal_key = *equal_key || cmp == 0;
		if (cmp < 0)
			beg = mid + 1;
		else
			end = mid;
		tuple_unref(fnd_key);
	}
	return end;
}

/**
 * Binary search in a run for the given key.
 * In terms of STL, makes lower_bound for EQ,GE,LT and upper_bound for GT,LE
 * Resulting wide position is stored it *pos argument
 * Additionally *equal_key argument is set to true if the found value is
 * equal to given key (untouched otherwise)
 *
 * @retval 0 success
 * @retval -1 read or memory error
 * @retval -2 invalid iterator
 */
static NODISCARD int
vy_run_iterator_search(struct vy_run_iterator *itr, const struct tuple *key,
		       struct vy_run_iterator_pos *pos, bool *equal_key)
{
	pos->page_no = vy_run_iterator_search_page(itr, key, equal_key);
	if (pos->page_no == 0) {
		pos->pos_in_page = 0;
		return 0;
	}
	pos->page_no--;
	struct vy_page *page;
	int rc = vy_run_iterator_load_page(itr, pos->page_no, &page);
	if (rc != 0)
		return rc;
	bool equal_in_page = false;
	pos->pos_in_page = vy_run_iterator_search_in_page(itr, key, page,
							  &equal_in_page);
	if (pos->pos_in_page == page->count) {
		pos->page_no++;
		pos->pos_in_page = 0;
	} else {
		*equal_key = equal_in_page;
	}
	return 0;
}

/**
 * Increment (or decrement, depending on the order) the current
 * wide position.
 * @retval 0 success, set *pos to new value
 * @retval 1 EOF
 * Affects: curr_loaded_page
 */
static NODISCARD int
vy_run_iterator_next_pos(struct vy_run_iterator *itr,
			 enum iterator_type iterator_type,
			 struct vy_run_iterator_pos *pos)
{
	itr->stat->step_count++;
	*pos = itr->curr_pos;
	assert(pos->page_no < itr->run->info.count);
	if (iterator_type == ITER_LE || iterator_type == ITER_LT) {
		if (pos->pos_in_page > 0) {
			pos->pos_in_page--;
		} else {
			if (pos->page_no == 0)
				return 1;
			pos->page_no--;
			struct vy_page_info *page_info =
				vy_run_page_info(itr->run, pos->page_no);
			assert(page_info->count > 0);
			pos->pos_in_page = page_info->count - 1;
		}
	} else {
		assert(iterator_type == ITER_GE || iterator_type == ITER_GT ||
		       iterator_type == ITER_EQ);
		struct vy_page_info *page_info =
			vy_run_page_info(itr->run, pos->page_no);
		assert(page_info->count > 0);
		pos->pos_in_page++;
		if (pos->pos_in_page >= page_info->count) {
			pos->page_no++;
			pos->pos_in_page = 0;
			if (pos->page_no == itr->run->info.count)
				return 1;
		}
	}
	return 0;
}

static NODISCARD int
vy_run_iterator_get(struct vy_run_iterator *itr, struct tuple **result);

/**
 * Find the next record with lsn <= itr->lsn record.
 * The current position must be at the beginning of a series of
 * records with the same key it terms of direction of iterator
 * (i.e. left for GE, right for LE).
 * @retval 0 success or EOF (*ret == NULL)
 * @retval -1 read or memory error
 * @retval -2 invalid iterator
 * Affects: curr_loaded_page, curr_pos, search_ended
 */
static NODISCARD int
vy_run_iterator_find_lsn(struct vy_run_iterator *itr, struct tuple **ret)
{
	assert(itr->curr_pos.page_no < itr->run->info.count);
	struct tuple *stmt;
	struct index_def *index_def = itr->index->index_def;
	const struct tuple *key = itr->key;
	enum iterator_type iterator_type = itr->iterator_type;
	*ret = NULL;
	int rc = vy_run_iterator_read(itr, itr->curr_pos, &stmt);
	if (rc != 0)
		return rc;
	while (vy_stmt_lsn(stmt) > *itr->vlsn) {
		tuple_unref(stmt);
		stmt = NULL;
		rc = vy_run_iterator_next_pos(itr, iterator_type,
					      &itr->curr_pos);
		if (rc > 0) {
			vy_run_iterator_cache_clean(itr);
			itr->search_ended = true;
			return 0;
		}
		assert(rc == 0);
		rc = vy_run_iterator_read(itr, itr->curr_pos, &stmt);
		if (rc != 0)
			return rc;
		if (iterator_type == ITER_EQ &&
		    vy_stmt_compare(stmt, key, &index_def->key_def)) {
			tuple_unref(stmt);
			stmt = NULL;
			vy_run_iterator_cache_clean(itr);
			itr->search_ended = true;
			return 0;
		}
	}
	if (iterator_type == ITER_LE || iterator_type == ITER_LT) {
		/* Remember the page_no of stmt */
		uint32_t cur_key_page_no = itr->curr_pos.page_no;

		struct vy_run_iterator_pos test_pos;
		rc = vy_run_iterator_next_pos(itr, iterator_type, &test_pos);
		while (rc == 0) {
			/*
			 * The cache is at least two pages. Ensure that
			 * subsequent read keeps the stmt in the cache
			 * by moving its page to the start of LRU list.
			 */
			vy_run_iterator_cache_touch(itr, cur_key_page_no);

			struct tuple *test_stmt;
			rc = vy_run_iterator_read(itr, test_pos, &test_stmt);
			if (rc != 0)
				return rc;
			if (vy_stmt_lsn(test_stmt) > *itr->vlsn ||
			    vy_tuple_compare(stmt, test_stmt,
					     &index_def->key_def) != 0) {
				tuple_unref(test_stmt);
				test_stmt = NULL;
				break;
			}
			tuple_unref(test_stmt);
			test_stmt = NULL;
			itr->curr_pos = test_pos;

			/* See above */
			vy_run_iterator_cache_touch(itr, cur_key_page_no);

			rc = vy_run_iterator_next_pos(itr, iterator_type,
						      &test_pos);
		}

		rc = rc > 0 ? 0 : rc;
	}
	tuple_unref(stmt);
	if (!rc) /* If next_pos() found something then get it. */
		rc = vy_run_iterator_get(itr, ret);
	return rc;
}

/*
 * FIXME: vy_run_iterator_next_key() calls vy_run_iterator_start() which
 * recursivly calls vy_run_iterator_next_key().
 */
static NODISCARD int
vy_run_iterator_next_key(struct vy_stmt_iterator *vitr, struct tuple **ret,
			 bool *stop);
/**
 * Find next (lower, older) record with the same key as current
 * Return true if the record was found
 * Return false if no value was found (or EOF) or there is a read error
 * @retval 0 success or EOF (*ret == NULL)
 * @retval -1 read or memory error
 * @retval -2 invalid iterator
 * Affects: curr_loaded_page, curr_pos, search_ended
 */
static NODISCARD int
vy_run_iterator_start(struct vy_run_iterator *itr, struct tuple **ret)
{
	assert(!itr->search_started);
	itr->search_started = true;
	*ret = NULL;

	struct index_def *user_index_def = itr->index->user_index_def;
	if (itr->run->info.has_bloom && itr->iterator_type == ITER_EQ &&
	    tuple_field_count(itr->key) >= user_index_def->key_def.part_count) {
		uint32_t hash;
		if (vy_stmt_type(itr->key) == IPROTO_SELECT) {
			const char *data = tuple_data(itr->key);
			mp_decode_array(&data);
			hash = key_hash(data, &user_index_def->key_def);
		} else {
			hash = tuple_hash(itr->key, &user_index_def->key_def);
		}
		if (!bloom_possible_has(&itr->run->info.bloom, hash)) {
			itr->search_ended = true;
			itr->stat->bloom_reflections++;
			return 0;
		}
	}

	itr->stat->lookup_count++;

	if (itr->run->info.count == 1) {
		/* there can be a stupid bootstrap run in which it's EOF */
		struct vy_page_info *page_info = itr->run->info.page_infos;

		if (!page_info->count) {
			vy_run_iterator_cache_clean(itr);
			itr->search_ended = true;
			return 0;
		}
		struct vy_page *page;
		int rc = vy_run_iterator_load_page(itr, 0, &page);
		if (rc != 0)
			return rc;
	} else if (itr->run->info.count == 0) {
		vy_run_iterator_cache_clean(itr);
		itr->search_ended = true;
		return 0;
	}

	struct vy_run_iterator_pos end_pos = {itr->run->info.count, 0};
	bool equal_found = false;
	int rc;
	if (tuple_field_count(itr->key) > 0) {
		rc = vy_run_iterator_search(itr, itr->key, &itr->curr_pos,
					    &equal_found);
		if (rc != 0)
			return rc;
	} else if (itr->iterator_type == ITER_LE) {
		itr->curr_pos = end_pos;
	} else {
		assert(itr->iterator_type == ITER_GE);
		itr->curr_pos.page_no = 0;
		itr->curr_pos.pos_in_page = 0;
	}
	if (itr->iterator_type == ITER_EQ && !equal_found) {
		vy_run_iterator_cache_clean(itr);
		itr->search_ended = true;
		return 0;
	}
	if ((itr->iterator_type == ITER_GE || itr->iterator_type == ITER_GT) &&
	    itr->curr_pos.page_no == end_pos.page_no) {
		vy_run_iterator_cache_clean(itr);
		itr->search_ended = true;
		return 0;
	}
	if (itr->iterator_type == ITER_LT || itr->iterator_type == ITER_LE) {
		/**
		 * 1) in case of ITER_LT we now positioned on the value >= than
		 * given, so we need to make a step on previous key
		 * 2) in case if ITER_LE we now positioned on the value > than
		 * given (special branch of code in vy_run_iterator_search),
		 * so we need to make a step on previous key
		 */
		return vy_run_iterator_next_key(&itr->base, ret, NULL);
	} else {
		assert(itr->iterator_type == ITER_GE ||
		       itr->iterator_type == ITER_GT ||
		       itr->iterator_type == ITER_EQ);
		/**
		 * 1) in case of ITER_GT we now positioned on the value > than
		 * given (special branch of code in vy_run_iterator_search),
		 * so we need just to find proper lsn
		 * 2) in case if ITER_GE or ITER_EQ we now positioned on the
		 * value >= given, so we need just to find proper lsn
		 */
		return vy_run_iterator_find_lsn(itr, ret);
	}
}

/* }}} vy_run_iterator vy_run_iterator support functions */

/* {{{ vy_run_iterator API implementation */
/* TODO: move to c file and remove static keyword */

/** Vtable for vy_stmt_iterator - declared below */
static struct vy_stmt_iterator_iface vy_run_iterator_iface;

/**
 * Open the iterator.
 */
static void
vy_run_iterator_open(struct vy_run_iterator *itr, struct vy_iterator_stat *stat,
		     struct vy_index *index, struct vy_run *run,
		     enum iterator_type iterator_type,
		     const struct tuple *key, const int64_t *vlsn,
		     struct tuple_format *format,
		     struct tuple_format *upsert_format)
{
	itr->base.iface = &vy_run_iterator_iface;
	itr->stat = stat;
	itr->format = format;
	itr->upsert_format = upsert_format;
	itr->index = index;
	itr->run = run;

	itr->iterator_type = iterator_type;
	itr->key = key;
	itr->vlsn = vlsn;
	if (tuple_field_count(key) == 0) {
		/* NULL key. change itr->iterator_type for simplification */
		itr->iterator_type = iterator_type == ITER_LT ||
				     iterator_type == ITER_LE ?
				     ITER_LE : ITER_GE;
	}

	itr->curr_stmt = NULL;
	itr->curr_pos.page_no = itr->run->info.count;
	itr->curr_stmt_pos.page_no = UINT32_MAX;
	itr->curr_page = NULL;
	itr->prev_page = NULL;

	itr->search_started = false;
	itr->search_ended = false;
}

/**
 * Create a stmt object from a its impression on a run page.
 * Uses the current iterator position in the page.
 *
 * @retval 0 success or EOF (*result == NULL)
 * @retval -1 memory or read error
 * @retval -2 invalid iterator
 */
static NODISCARD int
vy_run_iterator_get(struct vy_run_iterator *itr, struct tuple **result)
{
	assert(itr->search_started);
	*result = NULL;
	if (itr->search_ended)
		return 0;
	if (itr->curr_stmt != NULL) {
		if (itr->curr_stmt_pos.page_no == itr->curr_pos.page_no &&
		    itr->curr_stmt_pos.pos_in_page == itr->curr_pos.pos_in_page) {
			*result = itr->curr_stmt;
			return 0;
		}
		tuple_unref(itr->curr_stmt);
		itr->curr_stmt = NULL;
		itr->curr_stmt_pos.page_no = UINT32_MAX;
	}
	int rc = vy_run_iterator_read(itr, itr->curr_pos, result);
	if (rc == 0) {
		itr->curr_stmt_pos = itr->curr_pos;
		itr->curr_stmt = *result;
	}
	return rc;
}

/**
 * Find the next stmt in a page, i.e. a stmt with a different key
 * and fresh enough LSN (i.e. skipping the keys
 * too old for the current transaction).
 *
 * @retval 0 success or EOF (*ret == NULL)
 * @retval -1 memory or read error
 * @retval -2 invalid iterator
 */
static NODISCARD int
vy_run_iterator_next_key(struct vy_stmt_iterator *vitr, struct tuple **ret,
			 bool *stop)
{
	(void)stop;
	assert(vitr->iface->next_key == vy_run_iterator_next_key);
	struct vy_run_iterator *itr = (struct vy_run_iterator *) vitr;
	*ret = NULL;
	int rc;

	if (itr->search_ended)
		return 0;
	if (!itr->search_started)
		return vy_run_iterator_start(itr, ret);
	uint32_t end_page = itr->run->info.count;
	assert(itr->curr_pos.page_no <= end_page);
	struct index_def *index_def = itr->index->index_def;
	if (itr->iterator_type == ITER_LE || itr->iterator_type == ITER_LT) {
		if (itr->curr_pos.page_no == 0 &&
		    itr->curr_pos.pos_in_page == 0) {
			vy_run_iterator_cache_clean(itr);
			itr->search_ended = true;
			return 0;
		}
		if (itr->curr_pos.page_no == end_page) {
			/* A special case for reverse iterators */
			uint32_t page_no = end_page - 1;
			struct vy_page *page;
			int rc = vy_run_iterator_load_page(itr, page_no, &page);
			if (rc != 0)
				return rc;
			if (page->count == 0) {
				vy_run_iterator_cache_clean(itr);
				itr->search_ended = true;
				return 0;
			}
			itr->curr_pos.page_no = page_no;
			itr->curr_pos.pos_in_page = page->count - 1;
			return vy_run_iterator_find_lsn(itr, ret);
		}
	}
	assert(itr->curr_pos.page_no < end_page);

	struct tuple *cur_key;
	rc = vy_run_iterator_read(itr, itr->curr_pos, &cur_key);
	if (rc != 0)
		return rc;
	uint32_t cur_key_page_no = itr->curr_pos.page_no;

	struct tuple *next_key = NULL;
	do {
		if (next_key != NULL)
			tuple_unref(next_key);
		next_key = NULL;
		int rc = vy_run_iterator_next_pos(itr, itr->iterator_type,
						  &itr->curr_pos);
		if (rc > 0) {
			vy_run_iterator_cache_clean(itr);
			itr->search_ended = true;
			tuple_unref(cur_key);
			cur_key = NULL;
			return 0;
		}

		/*
		 * The cache is at least two pages. Ensure that
		 * subsequent read keeps the cur_key in the cache
		 * by moving its page to the start of LRU list.
		 */
		vy_run_iterator_cache_touch(itr, cur_key_page_no);

		rc = vy_run_iterator_read(itr, itr->curr_pos, &next_key);
		if (rc != 0) {
			tuple_unref(cur_key);
			cur_key = NULL;
			return rc;
		}

		/* See above */
		vy_run_iterator_cache_touch(itr, cur_key_page_no);
	} while (vy_tuple_compare(cur_key, next_key, &index_def->key_def) == 0);
	tuple_unref(cur_key);
	cur_key = NULL;
	if (itr->iterator_type == ITER_EQ &&
	    vy_stmt_compare(next_key, itr->key, &index_def->key_def) != 0) {
		vy_run_iterator_cache_clean(itr);
		itr->search_ended = true;
		tuple_unref(next_key);
		next_key = NULL;
		return 0;
	}
	tuple_unref(next_key);
	next_key = NULL;
	return vy_run_iterator_find_lsn(itr, ret);
}

/**
 * Find next (lower, older) record with the same key as current
 * @retval 0 success or EOF (*ret == NULL)
 * @retval -1 memory or read error
 * @retval -2 invalid iterator
 */
static NODISCARD int
vy_run_iterator_next_lsn(struct vy_stmt_iterator *vitr, struct tuple **ret)
{
	assert(vitr->iface->next_lsn == vy_run_iterator_next_lsn);
	struct vy_run_iterator *itr = (struct vy_run_iterator *) vitr;
	*ret = NULL;
	int rc;

	if (itr->search_ended)
		return 0;
	if (!itr->search_started)
		return vy_run_iterator_start(itr, ret);
	assert(itr->curr_pos.page_no < itr->run->info.count);

	struct vy_run_iterator_pos next_pos;
	rc = vy_run_iterator_next_pos(itr, ITER_GE, &next_pos);
	if (rc > 0)
		return 0;

	struct tuple *cur_key;
	rc = vy_run_iterator_read(itr, itr->curr_pos, &cur_key);
	if (rc != 0)
		return rc;

	struct tuple *next_key;
	rc = vy_run_iterator_read(itr, next_pos, &next_key);
	if (rc != 0) {
		tuple_unref(cur_key);
		return rc;
	}

	/**
	 * One can think that we had to lock page of itr->curr_pos,
	 *  to prevent freeing cur_key with entire page and avoid
	 *  segmentation fault in vy_stmt_compare_raw.
	 * But in fact the only case when curr_pos and next_pos
	 *  point to different pages is the case when next_pos points
	 *  to the beginning of the next page, and in this case
	 *  vy_run_iterator_read will read data from page index, not the page.
	 *  So in the case no page will be unloaded and we don't need
	 *  page lock
	 */
	struct index_def *index_def = itr->index->index_def;
	int cmp = vy_tuple_compare(cur_key, next_key, &index_def->key_def);
	tuple_unref(cur_key);
	cur_key = NULL;
	tuple_unref(next_key);
	next_key = NULL;
	itr->curr_pos = cmp == 0 ? next_pos : itr->curr_pos;
	rc = cmp != 0;
	if (rc != 0)
		return 0;
	return vy_run_iterator_get(itr, ret);
}

/**
 * Restore the current position (if necessary) after a change in the set of
 * runs or ranges and check if the position was changed.
 * @sa struct vy_stmt_iterator comments.
 *
 * @pre the iterator is not started
 *
 * @param last_stmt the last key on which the iterator was
 *		      positioned
 *
 * @retval 0	if position did not change (iterator started)
 * @retval 1	if position changed
 * @retval -1	a read or memory error
 */
static NODISCARD int
vy_run_iterator_restore(struct vy_stmt_iterator *vitr,
			const struct tuple *last_stmt, struct tuple **ret,

			bool *stop)
{
	(void)stop;
	assert(vitr->iface->restore == vy_run_iterator_restore);
	struct vy_run_iterator *itr = (struct vy_run_iterator *) vitr;
	*ret = NULL;
	int rc;

	if (itr->search_started || last_stmt == NULL) {
		if (!itr->search_started) {
			rc = vy_run_iterator_start(itr, ret);
		} else {
			rc = vy_run_iterator_get(itr, ret);
		}
		if (rc < 0)
			return rc;
		return 0;
	}
	/* Restoration is very similar to first search so we'll use that */
	enum iterator_type save_type = itr->iterator_type;
	const struct tuple *save_key = itr->key;
	if (itr->iterator_type == ITER_GT || itr->iterator_type == ITER_EQ)
		itr->iterator_type = ITER_GE;
	else if (itr->iterator_type == ITER_LT)
		itr->iterator_type = ITER_LE;
	itr->key = last_stmt;
	struct tuple *next;
	rc = vy_run_iterator_start(itr, &next);
	itr->iterator_type = save_type;
	itr->key = save_key;
	if (rc != 0)
		return rc;
	else if (next == NULL)
		return 0;
	struct key_def *def = &itr->index->index_def->key_def;
	bool position_changed = true;
	if (vy_stmt_compare(next, last_stmt, def) == 0) {
		position_changed = false;
		if (vy_stmt_lsn(next) >= vy_stmt_lsn(last_stmt)) {
			/* skip the same stmt to next stmt or older version */
			do {
				rc = vy_run_iterator_next_lsn(vitr, &next);
				if (rc != 0)
					return rc;
				if (next == NULL) {
					rc = vy_run_iterator_next_key(vitr,
								      &next,
								      NULL);
					if (rc != 0)
						return rc;
					break;
				}
			} while (vy_stmt_lsn(next) >= vy_stmt_lsn(last_stmt));
			if (next != NULL)
				position_changed = true;
		}
	} else if (itr->iterator_type == ITER_EQ &&
		   vy_stmt_compare(itr->key, next, def) != 0) {

		itr->search_ended = true;
		vy_run_iterator_cache_clean(itr);
		return position_changed;
	}
	*ret = next;
	return position_changed;
}

/**
 * Free all allocated resources in a worker thread.
 */
static void
vy_run_iterator_cleanup(struct vy_stmt_iterator *vitr)
{
	assert(vitr->iface->cleanup == vy_run_iterator_cleanup);
	vy_run_iterator_cache_clean((struct vy_run_iterator *) vitr);
}

/**
 * Close the iterator and free resources.
 * Can be called only after cleanup().
 */
static void
vy_run_iterator_close(struct vy_stmt_iterator *vitr)
{
	assert(vitr->iface->close == vy_run_iterator_close);
	struct vy_run_iterator *itr = (struct vy_run_iterator *) vitr;
	/* cleanup() must be called before */
	assert(itr->curr_stmt == NULL && itr->curr_page == NULL);
	TRASH(itr);
	(void) itr;
}

static struct vy_stmt_iterator_iface vy_run_iterator_iface = {
	.next_key = vy_run_iterator_next_key,
	.next_lsn = vy_run_iterator_next_lsn,
	.restore = vy_run_iterator_restore,
	.cleanup = vy_run_iterator_cleanup,
	.close = vy_run_iterator_close,
};

/* }}} vy_run_iterator API implementation */


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
	struct txv *txv;
	struct index_def *index_def = itr->index->index_def;
	if (tuple_field_count(itr->key) > 0) {
		struct write_set_key key = { itr->index, itr->key };
		if (itr->iterator_type == ITER_EQ)
			txv = write_set_search(&itr->tx->write_set, &key);
		else if (itr->iterator_type == ITER_GE ||
			 itr->iterator_type == ITER_GT)
			txv = write_set_nsearch(&itr->tx->write_set, &key);
		else
			txv = write_set_psearch(&itr->tx->write_set, &key);
		if (txv == NULL || txv->index != itr->index)
			return;
		if (vy_stmt_compare(itr->key, txv->stmt, &index_def->key_def) == 0) {
			while (true) {
				struct txv *next;
				if (itr->iterator_type == ITER_LE ||
				    itr->iterator_type == ITER_GT)
					next = write_set_next(&itr->tx->write_set, txv);
				else
					next = write_set_prev(&itr->tx->write_set, txv);
				if (next == NULL || next->index != itr->index)
					break;
				if (vy_stmt_compare(itr->key, next->stmt,
						    &index_def->key_def) != 0)
					break;
				txv = next;
			}
			if (itr->iterator_type == ITER_GT)
				txv = write_set_next(&itr->tx->write_set, txv);
			else if (itr->iterator_type == ITER_LT)
				txv = write_set_prev(&itr->tx->write_set, txv);
		}
	} else if (itr->iterator_type == ITER_LE) {
		txv = write_set_last(&itr->tx->write_set);
	} else {
		assert(itr->iterator_type == ITER_GE);
		txv = write_set_first(&itr->tx->write_set);
	}
	if (txv == NULL || txv->index != itr->index)
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
			    &itr->index->index_def->key_def) != 0)
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
	struct key_def *def = &itr->index->index_def->key_def;
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
vy_merge_iterator_open(struct vy_merge_iterator *itr, struct vy_index *index,
		       enum iterator_type iterator_type,
		       const struct tuple *key, struct tuple_format *format,
		       struct tuple_format *upsert_format)
{
	assert(key != NULL);
	itr->index = index;
	itr->format = format;
	itr->upsert_format = upsert_format;
	itr->index_version = 0;
	itr->curr_range = NULL;
	itr->range_version = 0;
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
		tuple_field_count(key) >= index->index_def->key_def.part_count;
	itr->unique_optimization =
		(iterator_type == ITER_EQ || iterator_type == ITER_GE ||
		 iterator_type == ITER_LE) &&
		tuple_field_count(key) >= index->index_def->key_def.part_count;
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
	itr->curr_range = NULL;
	itr->range_version = 0;
	itr->index = NULL;
	itr->index_version = 0;
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
			      struct vy_range *range)
{
	itr->curr_range = range;
	itr->range_version = range != NULL ? range->version : 0;
	itr->index_version = itr->index->version;
}

/*
 * Try to restore position of merge iterator
 * @retval 0	if position did not change (iterator started)
 * @retval -2	iterator is no more valid
 */
static NODISCARD int
vy_merge_iterator_check_version(struct vy_merge_iterator *itr)
{
	if (!itr->index_version)
		return 0; /* version checking is off */

	assert(itr->curr_range != NULL);
	if (itr->index_version == itr->index->version &&
	    itr->curr_range->version == itr->range_version)
		return 0;

	return -2; /* iterator is not valid anymore */
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
	struct key_def *def = &itr->index->index_def->key_def;
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
		} else  {
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
	struct key_def *def = &itr->index->index_def->key_def;
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
	struct index_def *def = itr->index->index_def;
	if (t == NULL)
		return 0;
	/* Upserts enabled only in the primary index. */
	assert(vy_stmt_type(t) != IPROTO_UPSERT || def->iid == 0);
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
		applied = vy_apply_upsert(t, next, def, itr->format,
					  itr->upsert_format, suppress_error,
					  stat);
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
		result = result || rc;
	}
	itr->skipped_start = itr->src_count;
	return result;
}

/* }}} Merge iterator */

/* {{{ Write iterator */

/**
 * Iterate over an in-memory index when writing it to disk (dump)
 * or over a series of sorted runs on disk to create a new sorted
 * run (compaction).
 *
 * Use merge iterator to order the output and filter out
 * too old statements (older than the oldest active read view).
 *
 * Squash multiple UPSERT statements over the same key into one,
 * if possible.
 *
 * Background
 * ----------
 * Vinyl provides support for consistent read views. The oldest
 * active read view is maintained in the transaction manager.
 * To support it, when dumping or compacting statements on disk,
 * older versions need to be preserved, and versions outside
 * any active read view garbage collected. This task is handled
 * by the write iterator.
 *
 * Filtering
 * ---------
 * Let's call each transaction consistent read view LSN vlsn.
 *
 *	oldest_vlsn = MIN(vlsn) over all active transactions
 *
 * Thus to preserve relevant data for every key and purge old
 * versions, the iterator works as follows:
 *
 *      If statement lsn is greater than oldest vlsn, the
 *      statement is preserved.
 *
 *      Otherwise, if statement type is REPLACE/DELETE, then
 *      it's returned, and the iterator can proceed to the
 *      next key: the readers do not need the history.
 *
 *      Otherwise, the statement is UPSERT, and in order
 *      to restore the original tuple from UPSERT the reader
 *      does need the history: they need to look for an older
 *      statement to which the UPSERT can be applied to get
 *      a tuple. This older statement can be UPSERT as well,
 *      and so on.
 *	In other words, of statement type is UPSERT, the reader
 *	needs a range of statements from the youngest statement
 *	with lsn <= vlsn to the youngest non-UPSERT statement
 *	with lsn <= vlsn, borders included.
 *
 *	All other versions of this key can be skipped, and hence
 *	garbage collected.
 *
 * Squashing and garbage collection
 * --------------------------------
 * Filtering and garbage collection, performed by write iterator,
 * must have no effect on read views of active transactions:
 * they should read the same data as before.
 *
 * On the other hand, old version should be deleted as soon as possible;
 * multiple UPSERTs could be merged together to take up less
 * space, or substituted with REPLACE.
 *
 * Here's how it's done:
 *
 *
 *	1) Every statement with lsn greater than oldest vlsn is preserved
 *	in the output, since there could be an active transaction
 *	that needs it.
 *
 *	2) For all statements with lsn <= oldest_vlsn, only a single
 *	resultant statement is returned. Here's how.
 *
 *	2.1) If the youngest statement with lsn <= oldest _vlsn is a
 *	REPLACE/DELETE, it becomes the resultant statement.
 *
 *	2.2) Otherwise, it as an UPSERT. Then we must iterate over
 *	all older LSNs for this key until we find a REPLACE/DELETE
 *	or exhaust all input streams for this key.
 *
 *	If the older lsn is a yet another UPSERT, two upserts are
 *	squashed together into one. Otherwise we found an
 *	REPLACE/DELETE, so apply all preceding UPSERTs to it and
 *	get the resultant statement.
 *
 * There is an extra twist to this algorithm, used when performing
 * compaction of the last LSM level (i.e. merging all existing
 * runs into one). The last level does not need to store DELETEs.
 * Thus we can:
 * 1) Completely skip the resultant statement from output if it's
 *    a DELETE.
 *     ┃      ...      ┃       ┃     ...      ┃
 *     ┃               ┃       ┃              ┃    ↑
 *     ┣━ oldest vlsn ━┫   =   ┣━ oldest lsn ━┫    ↑ lsn
 *     ┃               ┃       ┗━━━━━━━━━━━━━━┛    ↑
 *     ┃    DELETE     ┃
 *     ┃      ...      ┃
 * 2) Replace an accumulated resultant UPSERT with an appropriate
 *    REPLACE.
 *     ┃      ...      ┃       ┃     ...      ┃
 *     ┃     UPSERT    ┃       ┃   REPLACE    ┃    ↑
 *     ┃               ┃       ┃              ┃    ↑
 *     ┣━ oldest vlsn ━┫   =   ┣━ oldest lsn ━┫    ↑ lsn
 *     ┃               ┃       ┗━━━━━━━━━━━━━━┛    ↑
 *     ┃    DELETE     ┃
 *     ┃      ...      ┃
 */
struct vy_write_iterator {
	struct vy_index *index;
	/**
	 * Format to allocate new REPLACE and DELETE tuples from
	 * vy_run. We have to store the reference to the format
	 * separate from index, because the index can be altered
	 * during work of the iterator.
	 */
	struct tuple_format *surrogate_format;
	/** Same as surrogate_format, but for UPSERT tuples. */
	struct tuple_format *upsert_format;
	/* The minimal VLSN among all active transactions */
	int64_t oldest_vlsn;
	/* There are is no level older than the one we're writing to. */
	bool is_last_level;
	/* On the next iteration we must move to the next key */
	bool goto_next_key;
	struct tuple *key;
	struct tuple *tmp_stmt;
	struct vy_merge_iterator mi;
	/* Usage statistics of mem iterators */
	struct vy_iterator_stat mem_iterator_stat;
	/* Usage statistics of run iterators */
	struct vy_iterator_stat run_iterator_stat;
};

/*
 * Open an empty write iterator. To add sources to the iterator
 * use vy_write_iterator_add_* functions
 */
static int
vy_write_iterator_open(struct vy_write_iterator *wi, struct vy_index *index,
		       bool is_last_level, int64_t oldest_vlsn)
{
	struct vy_env *env = index->env;
	wi->index = index;
	wi->oldest_vlsn = oldest_vlsn;
	wi->is_last_level = is_last_level;
	wi->goto_next_key = false;

	wi->key = vy_stmt_new_select(env->key_format, NULL, 0);
	if (wi->key == NULL)
		return -1;
	wi->surrogate_format = index->surrogate_format;
	wi->upsert_format = index->upsert_format;
	tuple_format_ref(wi->surrogate_format, 1);
	tuple_format_ref(wi->upsert_format, 1);
	vy_merge_iterator_open(&wi->mi, index, ITER_GE, wi->key,
			       wi->surrogate_format, wi->upsert_format);
	return 0;
}

static struct vy_write_iterator *
vy_write_iterator_new(struct vy_index *index, bool is_last_level,
		      int64_t oldest_vlsn)
{
	struct vy_write_iterator *wi = calloc(1, sizeof(*wi));
	if (wi == NULL) {
		diag_set(OutOfMemory, sizeof(*wi), "calloc", "wi");
		return NULL;
	}
	if (vy_write_iterator_open(wi, index, is_last_level,
				   oldest_vlsn) != 0) {
		free(wi);
		return NULL;
	}
	return wi;
}

static NODISCARD int
vy_write_iterator_add_run(struct vy_write_iterator *wi, struct vy_run *run)
{
	struct vy_merge_src *src;
	src = vy_merge_iterator_add(&wi->mi, false, false);
	if (src == NULL)
		return -1;
	static const int64_t vlsn = INT64_MAX;
	vy_run_iterator_open(&src->run_iterator, &wi->run_iterator_stat,
			     wi->index, run, ITER_GE, wi->key, &vlsn,
			     wi->surrogate_format, wi->upsert_format);
	return 0;
}

static NODISCARD int
vy_write_iterator_add_mem(struct vy_write_iterator *wi, struct vy_mem *mem)
{
	struct vy_merge_src *src;
	src = vy_merge_iterator_add(&wi->mi, false, false);
	if (src == NULL)
		return -1;
	static const int64_t vlsn = INT64_MAX;
	vy_mem_iterator_open(&src->mem_iterator, &wi->mem_iterator_stat,
			     mem, ITER_GE, wi->key, &vlsn);
	return 0;
}

/**
 * The write iterator can return multiple LSNs for the same
 * key, thus next() will automatically switch to the next
 * key when it's appropriate.
 *
 * The user of the write iterator simply expects a stream
 * of statements to write to the output.
 */
static NODISCARD int
vy_write_iterator_next(struct vy_write_iterator *wi, struct tuple **ret)
{
	/*
	 * Nullify the result stmt. If the next stmt is not
	 * found, this is a marker of the end of the stream.
	 */
	*ret = NULL;
	/*
	 * The write iterator guarantees that the returned stmt
	 * is alive until the next invocation of next(). If the
	 * returned stmt is obtained from the merge iterator,
	 * this guarantee is fulfilled by the merge iterator
	 * itself. If the write iterator creates the returned
	 * stmt, e.g. by squashing a bunch of upserts, then
	 * it must dereference the created stmt here.
	 */
	if (wi->tmp_stmt)
		tuple_unref(wi->tmp_stmt);
	wi->tmp_stmt = NULL;
	struct vy_merge_iterator *mi = &wi->mi;
	struct tuple *stmt = NULL;
	struct vy_index *index = wi->index;
	struct index_def *def = index->index_def;
	/* @sa vy_write_iterator declaration for the algorithm description. */
	while (true) {
		if (wi->goto_next_key) {
			wi->goto_next_key = false;
			if (vy_merge_iterator_next_key(mi, &stmt))
				return -1;
		} else {
			if (vy_merge_iterator_next_lsn(mi, &stmt))
				return -1;
			if (stmt == NULL &&
			    vy_merge_iterator_next_key(mi, &stmt))
				return -1;
		}
		if (stmt == NULL)
			return 0;
		if (vy_stmt_lsn(stmt) > wi->oldest_vlsn)
			break; /* Save the current stmt as the result. */
		wi->goto_next_key = true;
		if (vy_stmt_type(stmt) == IPROTO_DELETE && wi->is_last_level)
			continue; /* Skip unnecessary DELETE */
		if (vy_stmt_type(stmt) == IPROTO_REPLACE ||
		    vy_stmt_type(stmt) == IPROTO_DELETE) {
			/*
			 * If the tuple has extra size - it has
			 * column mask of an update operation.
			 * The tuples from secondary indexes
			 * which don't modify its keys can be
			 * skipped during dump,
			 * @sa vy_can_skip_update().
			 */
			if (def->iid > 0 &&
			    vy_can_skip_update(index, vy_stmt_column_mask(stmt)))
				continue;
			break; /* It's the resulting statement */
		}

		/* Squash upserts */
		assert(vy_stmt_type(stmt) == IPROTO_UPSERT);
		if (vy_merge_iterator_squash_upsert(mi, &stmt, false, NULL)) {
			tuple_unref(stmt);
			return -1;
		}
		if (vy_stmt_type(stmt) == IPROTO_UPSERT && wi->is_last_level) {
			/* Turn UPSERT to REPLACE. */
			struct tuple *applied;
			applied = vy_apply_upsert(stmt, NULL, def,
						  wi->surrogate_format,
						  wi->upsert_format, false,
						  NULL);
			tuple_unref(stmt);
			if (applied == NULL)
				return -1;
			stmt = applied;
		}
		wi->tmp_stmt = stmt;
		break;
	}
	*ret = stmt;
	return 0;
}

static void
vy_write_iterator_cleanup(struct vy_write_iterator *wi)
{
	assert(! cord_is_main());
	if (wi->tmp_stmt != NULL)
		tuple_unref(wi->tmp_stmt);
	wi->tmp_stmt = NULL;
	if (wi->key != NULL)
		tuple_unref(wi->key);
	wi->key = NULL;
	vy_merge_iterator_cleanup(&wi->mi);
}

static void
vy_write_iterator_delete(struct vy_write_iterator *wi)
{
	assert(cord_is_main());

	assert(wi->tmp_stmt == NULL);
	assert(wi->key == NULL);
	tuple_format_ref(wi->surrogate_format, -1);
	tuple_format_ref(wi->upsert_format, -1);
	vy_merge_iterator_close(&wi->mi);

	free(wi);
}

/* Write iterator }}} */

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
			       itr->index->cache, itr->iterator_type,
			       itr->key, itr->vlsn);
	bool stop = false;
	int rc = sub_src->iterator.iface->restore(&sub_src->iterator,
						  itr->curr_stmt,
						  &sub_src->stmt, &stop);
	(void)rc;
}

static void
vy_read_iterator_add_mem_range(struct vy_read_iterator *itr,
			       struct vy_range *range)
{
	struct vy_iterator_stat *stat = &itr->index->env->stat->mem_stat;
	struct vy_merge_src *sub_src;

	/* Add the active in-memory index. */
	if (range->mem != NULL) {
		sub_src = vy_merge_iterator_add(&itr->merge_iterator,
						true, true);
		vy_mem_iterator_open(&sub_src->mem_iterator, stat , range->mem,
				     itr->iterator_type, itr->key, itr->vlsn);
	}
	/* Add frozen in-memory indexes. */
	struct vy_mem *mem;
	rlist_foreach_entry(mem, &range->frozen, in_frozen) {
		sub_src = vy_merge_iterator_add(&itr->merge_iterator,
						false, true);
		vy_mem_iterator_open(&sub_src->mem_iterator, stat , mem,
				     itr->iterator_type, itr->key, itr->vlsn);
	}
}

static void
vy_read_iterator_add_mem(struct vy_read_iterator *itr)
{
	struct vy_range *range = itr->curr_range;

	assert(range != NULL);
	assert(range->shadow == NULL);

	/*
	 * The range may be in the middle of split, in which case we
	 * must add in-memory indexes of new ranges first.
	 */
	struct vy_range *r;
	rlist_foreach_entry(r, &range->split_list, split_list)
		vy_read_iterator_add_mem_range(itr, r);

	vy_read_iterator_add_mem_range(itr, range);
}

static void
vy_read_iterator_add_disk(struct vy_read_iterator *itr)
{
	assert(itr->curr_range != NULL);
	assert(itr->curr_range->shadow == NULL);
	struct vy_iterator_stat *stat = &itr->index->env->stat->run_stat;
	struct vy_run *run;
	struct tuple_format *format = itr->index->surrogate_format;
	/*
	 * The format of the statement must be exactly the space
	 * format with the same identifier to fully match the
	 * format in vy_mem.
	 */
	if (itr->index->space_index_count == 1)
		format = itr->index->space_format;
	rlist_foreach_entry(run, &itr->curr_range->runs, in_range) {
		struct vy_merge_src *sub_src = vy_merge_iterator_add(
			&itr->merge_iterator, false, true);
		vy_run_iterator_open(&sub_src->run_iterator, stat,
				     itr->index, run, itr->iterator_type,
				     itr->key, itr->vlsn, format,
				     itr->index->upsert_format);
	}
}

/**
 * Set up merge iterator for the current range.
 */
static void
vy_read_iterator_use_range(struct vy_read_iterator *itr)
{
	if (!itr->only_disk && itr->tx != NULL)
		vy_read_iterator_add_tx(itr);

	if (!itr->only_disk)
		vy_read_iterator_add_cache(itr);

	if (itr->curr_range == NULL)
		return;

	if (!itr->only_disk)
		vy_read_iterator_add_mem(itr);

	vy_read_iterator_add_disk(itr);

	/* Enable range and range index version checks */
	vy_merge_iterator_set_version(&itr->merge_iterator, itr->curr_range);
}

/**
 * Open the iterator.
 */
static void
vy_read_iterator_open(struct vy_read_iterator *itr, struct vy_index *index,
		      struct vy_tx *tx, enum iterator_type iterator_type,
		      const struct tuple *key, const int64_t *vlsn,
		      bool only_disk)
{
	itr->index = index;
	itr->tx = tx;
	itr->iterator_type = iterator_type;
	itr->key = key;
	itr->vlsn = vlsn;
	itr->only_disk = only_disk;
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
	vy_merge_iterator_open(&itr->merge_iterator, itr->index,
			       itr->iterator_type, itr->key,
			       itr->index->space_format,
			       itr->index->upsert_format);
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
	vy_merge_iterator_open(&itr->merge_iterator, itr->index,
			       itr->iterator_type, itr->key,
			       itr->index->space_format,
			       itr->index->upsert_format);
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
	*ret = NULL;
	struct vy_merge_iterator *mi = &itr->merge_iterator;
	while ((rc = vy_merge_iterator_next_key(mi, ret)) == -2) {
		if (vy_read_iterator_restore(itr) < 0)
			return -1;
		/* Check if the iterator is restored not on the same key. */
		if (itr->curr_stmt) {
			rc = vy_merge_iterator_next_key(mi, ret);
			if (rc == -1)
				return -1;
			if (rc == -2) {
				if (vy_read_iterator_restore(itr) < 0)
					return -1;
				continue;
			}
			/* If the iterator is empty then return. */
			if (*ret == NULL)
				return 0;
			/*
			 * If the iterator after restoration is on the same key
			 * then go to the next.
			 */
			if (vy_tuple_compare(itr->curr_stmt, *ret,
					     &itr->index->index_def->key_def) == 0)
				continue;
			/* Else return the new key. */
			break;
		}
	}
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
	*ret = NULL;
	assert(itr->curr_range != NULL);
	vy_merge_iterator_cleanup(&itr->merge_iterator);
	vy_merge_iterator_close(&itr->merge_iterator);
	vy_merge_iterator_open(&itr->merge_iterator, itr->index,
			       itr->iterator_type, itr->key,
			       itr->index->space_format,
			       itr->index->upsert_format);
	vy_range_iterator_next(&itr->range_iterator, &itr->curr_range);
	vy_read_iterator_use_range(itr);
	struct tuple *stmt = NULL;
	int rc = vy_read_iterator_merge_next_key(itr, &stmt);
	if (rc < 0)
		return -1;
	assert(rc >= 0);
	if (!stmt && itr->merge_iterator.range_ended && itr->curr_range != NULL)
		return vy_read_iterator_next_range(itr, ret);
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
	struct index_def *def = itr->index->index_def;
	struct vy_stat *stat = itr->index->env->stat;
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
				applied = vy_apply_upsert(t, NULL, def,
							  mi->format,
							  mi->upsert_format,
							  true, stat);
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
	if (*(itr->vlsn) == INT64_MAX) /* Do not store non-latest data */
		vy_cache_add(itr->index->cache, *result, prev_key,
			     itr->key, itr->iterator_type);

clear:
	if (prev_key != NULL)
		tuple_unref(prev_key);

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

/** Argument passed to vy_join_cb(). */
struct vy_join_arg {
	/** Vinyl environment. */
	struct vy_env *env;
	/** Recovery context to relay. */
	struct vy_recovery *recovery;
	/** Stream to relay statements to. */
	struct xstream *stream;
	/** ID of the space currently being relayed. */
	uint32_t space_id;
	/** Ordinal number of the index. */
	uint32_t index_id;
	/** Path to the index directory. */
	char *index_path;
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

/** Relay callback, passed to vy_recovery_iterate(). */
static int
vy_join_cb(const struct vy_log_record *record, void *cb_arg)
{
	struct vy_join_arg *arg = cb_arg;
	int rc = 0;

	if (record->type == VY_LOG_CREATE_INDEX) {
		arg->space_id = record->space_id;
		arg->index_id = record->index_id;
		vy_index_snprint_path(arg->index_path, PATH_MAX,
				      arg->env->conf->path,
				      arg->space_id, arg->index_id);
	}

	/*
	 * We are only interested in the primary index.
	 * Secondary keys will be rebuilt on the destination.
	 */
	if (arg->index_id != 0)
		goto out;

	/*
	 * We only send statements, not metadata, because the
	 * latter is a replica's private business.
	 */
	if (record->type != VY_LOG_INSERT_RUN)
		goto out;

	rc = -1;

	/* Load the run. */
	struct vy_run *run = vy_run_new(record->run_id);
	if (run == NULL)
		goto out;
	if (vy_run_recover(run, arg->index_path) != 0)
		goto out_free_run;

	ZSTD_DStream *zdctx = vy_env_get_zdctx(arg->env);
	if (zdctx == NULL)
		goto out_free_run;

	/* Send the run's statements to the replica. */
	for (uint32_t page_no = 0; page_no < run->info.count; page_no++) {
		struct vy_page_info *pi = vy_run_page_info(run, page_no);
		struct vy_page *page = vy_page_new(pi);
		if (page == NULL)
			goto out_free_run;
		if (vy_page_read(page, pi, run->fd, zdctx) != 0)
			goto out_free_page;
		for (uint32_t stmt_no = 0; stmt_no < pi->count; stmt_no++) {
			struct xrow_header xrow;
			if (vy_page_xrow(page, stmt_no, &xrow) != 0)
				goto out_free_page;
			xrow.lsn = ++arg->lsn;
			if (xstream_write(arg->stream, &xrow) != 0)
				goto out_free_page;
		}
		vy_page_delete(page);
		continue;
out_free_page:
		vy_page_delete(page);
		goto out_free_run;
	}
	rc = 0; /* success */

out_free_run:
	vy_run_unref(run);
out:
	return rc;
}

/** Relay cord function. */
static int
vy_join_f(va_list ap)
{
	struct vy_join_arg *arg = va_arg(ap, struct vy_join_arg *);
	return vy_recovery_iterate(arg->recovery, false, vy_join_cb, arg);
}

int
vy_join(struct vy_env *env, struct vclock *vclock, struct xstream *stream)
{
	(void)vclock;
	struct vy_join_arg arg = {
		.env = env,
		.stream = stream,
	};

	arg.index_path = malloc(PATH_MAX);
	if (arg.index_path == NULL) {
		diag_set(OutOfMemory, PATH_MAX, "malloc", "path");
		goto err_path;
	}

	arg.recovery = vy_recovery_new(vclock_sum(vclock));
	if (arg.recovery == NULL)
		goto err_recovery;

	struct cord cord;
	if (cord_costart(&cord, "initial_join", vy_join_f, &arg) != 0)
		goto err_cord;

	int rc = cord_cojoin(&cord);

	vy_recovery_delete(arg.recovery);
	free(arg.index_path);
	return rc;

err_cord:
	vy_recovery_delete(arg.recovery);
err_recovery:
	free(arg.index_path);
err_path:
	return -1;
}

/* }}} Replication */

/* {{{ Garbage collection */

/** Garbage collection callback, passed to vy_recovery_iterate(). */
static int
vy_collect_garbage_cb(const struct vy_log_record *record, void *cb_arg)
{
	struct vy_env *env = cb_arg;

	if (record->type == VY_LOG_DELETE_RUN)
		vy_run_record_gc(env->conf->path, record);

	fiber_reschedule();
	return 0;
}

void
vy_collect_garbage(struct vy_env *env, int64_t lsn)
{
	/* Cleanup old metadata log files. */
	vy_log_collect_garbage(lsn);

	/* Cleanup run files. */
	struct vy_recovery *recovery = vy_recovery_new(lsn);
	if (recovery == NULL) {
		say_warn("vinyl garbage collection failed: %s",
			 diag_last_error(diag_get())->errmsg);
		return;
	}
	vy_recovery_iterate(recovery, true, vy_collect_garbage_cb, env);
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
};

/** Backup callback, passed to vy_recovery_iterate(). */
static int
vy_backup_cb(const struct vy_log_record *record, void *cb_arg)
{
	struct vy_backup_arg *arg = cb_arg;

	if (record->type != VY_LOG_INSERT_RUN)
		goto out;

	char path[PATH_MAX];
	for (int type = 0; type < vy_file_MAX; type++) {
		vy_run_record_snprint_path(path, sizeof(path),
					   arg->env->conf->path, record, type);
		if (arg->cb(path, arg->cb_arg) != 0)
			return -1;
	}
out:
	fiber_reschedule();
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
	struct vy_recovery *recovery = vy_recovery_new(vclock_sum(vclock));
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
	ERROR_INJECT_U64(ERRINJ_VY_SQUASH_TIMEOUT,
		errinj_getu64(ERRINJ_VY_SQUASH_TIMEOUT) > 0,
		fiber_sleep(errinj_getu64(ERRINJ_VY_SQUASH_TIMEOUT) * 0.001));

	struct vy_index *index = squash->index;
	struct vy_env *env = index->env;
	struct vy_stat *stat = env->stat;

	struct index_def *index_def = index->index_def;
	/* Upserts enabled only in the primary index. */
	assert(index_def->iid == 0);

	struct vy_read_iterator itr;
	const int64_t lsn = INT64_MAX;
	vy_read_iterator_open(&itr, index, NULL, ITER_EQ,
			      squash->stmt, &lsn, false);
	struct tuple *result;
	int rc = vy_read_iterator_next(&itr, &result);
	if (rc == 0 && result != NULL)
		tuple_ref(result);
	vy_read_iterator_close(&itr);
	if (rc != 0)
		return -1;
	if (result == NULL)
		return 0;

	struct vy_range *range;
	range = vy_range_tree_find_by_key(&index->tree, ITER_EQ, index_def,
					  result);
	/*
	 * While we were reading on-disk runs, new statements could
	 * have been inserted into the in-memory tree. Apply them to
	 * the result.
	 */
	struct vy_mem *mem = range->mem;
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
	vy_mem_tree_iterator_prev(&mem->tree, &mem_itr);
	while (!vy_mem_tree_iterator_is_invalid(&mem_itr)) {
		const struct tuple *mem_stmt =
			*vy_mem_tree_iterator_get_elem(&mem->tree, &mem_itr);
		if (vy_tuple_compare(result, mem_stmt, &index_def->key_def) != 0)
			break;
		struct tuple *applied;
		if (vy_stmt_type(mem_stmt) == IPROTO_UPSERT) {
			applied = vy_apply_upsert(mem_stmt, result, index_def,
						  mem->format,
						  mem->upsert_format,
						  true, stat);
		} else {
			applied = vy_stmt_dup(mem_stmt, mem->format);
		}
		tuple_unref(result);
		if (applied == NULL)
			return -1;
		result = applied;
		vy_mem_tree_iterator_prev(&mem->tree, &mem_itr);
	}

	rmean_collect(stat->rmean, VY_STAT_UPSERT_SQUASHED, 1);

	/*
	 * Insert the resulting REPLACE statement to the mem
	 * and adjust the quota.
	 */
	size_t mem_used_before = lsregion_used(&env->allocator);
	const struct tuple *region_stmt = NULL;
	rc = vy_range_set(range, mem, result, &region_stmt);
	tuple_unref(result);
	size_t mem_used_after = lsregion_used(&env->allocator);
	assert(mem_used_after >= mem_used_before);
	if (rc == 0) {
		vy_quota_force_use(&env->quota,
				   mem_used_after - mem_used_before);
	}
	return rc;
}

static struct vy_squash_queue *
vy_squash_queue_new(void)
{
	struct vy_squash_queue *sq = malloc(sizeof(*sq));
	if (sq == NULL)
		return NULL;
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
	struct index_def *index_def = index->index_def;
	struct vy_squash_queue *sq = index->env->squash_queue;

	say_debug("optimize upsert slow: %"PRIu32"/%"PRIu32": %s",
		  index_def->space_id, index_def->iid, vy_stmt_str(stmt));

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
	assert(part_count <= index->index_def->key_def.part_count);
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
		struct index_def *def = index->index_def;
		/* point-lookup iterator (optimization) */
		if (def->opts.is_unique && part_count == def->key_def.part_count) {
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
			      &tx->vlsn, false);
	c->iterator_type = iterator_type;
	return c;
}

int
vy_cursor_next(struct vy_cursor *c, struct tuple **result)
{
	struct tuple *vyresult = NULL;
	struct vy_index *index = c->index;
	struct index_def *def = index->index_def;
	assert(index->space_index_count > 0);
	*result = NULL;

	if (c->tx == NULL) {
		diag_set(ClientError, ER_NO_ACTIVE_TRANSACTION);
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
	    vy_tuple_compare_with_key(vyresult, c->key, &def->key_def) != 0)
		return 0;
	if (def->iid > 0 && vy_index_full_by_stmt(c->tx, index, vyresult,
						  &vyresult))
		return -1;
	*result = vyresult;
	/**
	 * If the index is not primary (def->iid != 0) then no
	 * need to reference the tuple, because it is returned
	 * from vy_index_full_by_stmt() as new statement with 1
	 * reference.
	 */
	if (def->iid == 0)
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
