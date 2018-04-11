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
#include "vy_range.h"
#include "vy_lsm.h"
#include "vy_tx.h"
#include "vy_cache.h"
#include "vy_log.h"
#include "vy_upsert.h"
#include "vy_write_iterator.h"
#include "vy_read_iterator.h"
#include "vy_point_lookup.h"
#include "vy_quota.h"
#include "vy_scheduler.h"
#include "vy_stat.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include <small/lsregion.h>
#include <small/region.h>
#include <small/mempool.h>

#include "coio_task.h"
#include "cbus.h"
#include "histogram.h"
#include "tuple_update.h"
#include "txn.h"
#include "xrow.h"
#include "xlog.h"
#include "engine.h"
#include "space.h"
#include "index.h"
#include "xstream.h"
#include "info.h"
#include "column_mask.h"
#include "trigger.h"
#include "checkpoint.h"
#include "session.h"
#include "wal.h" /* wal_mode() */

/**
 * Yield after iterating over this many objects (e.g. ranges).
 * Yield more often in debug mode.
 */
#if defined(NDEBUG)
enum { VY_YIELD_LOOPS = 128 };
#else
enum { VY_YIELD_LOOPS = 2 };
#endif

struct vy_squash_queue;

enum vy_status {
	VINYL_OFFLINE,
	VINYL_INITIAL_RECOVERY_LOCAL,
	VINYL_INITIAL_RECOVERY_REMOTE,
	VINYL_FINAL_RECOVERY_LOCAL,
	VINYL_FINAL_RECOVERY_REMOTE,
	VINYL_ONLINE,
};

struct vy_env {
	/** Recovery status */
	enum vy_status status;
	/** TX manager */
	struct tx_manager   *xm;
	/** Upsert squash queue */
	struct vy_squash_queue *squash_queue;
	/** Memory pool for index iterator. */
	struct mempool iterator_pool;
	/** Memory quota */
	struct vy_quota     quota;
	/** Timer for updating quota watermark. */
	ev_timer            quota_timer;
	/**
	 * Amount of quota used since the last
	 * invocation of the quota timer callback.
	 */
	size_t quota_use_curr;
	/**
	 * Quota use rate, in bytes per second.
	 * Calculated as exponentially weighted
	 * moving average of quota_use_curr.
	 */
	size_t quota_use_rate;
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
	/** Common LSM tree environment. */
	struct vy_lsm_env lsm_env;
	/** Environment for cache subsystem */
	struct vy_cache_env cache_env;
	/** Environment for run subsystem */
	struct vy_run_env run_env;
	/** Environment for memory subsystem. */
	struct vy_mem_env mem_env;
	/** Scheduler */
	struct vy_scheduler scheduler;
	/** Local recovery context. */
	struct vy_recovery *recovery;
	/** Local recovery vclock. */
	const struct vclock *recovery_vclock;
	/**
	 * LSN to assign to the next statement received during
	 * initial join.
	 *
	 * We can't use original statements' LSNs, because we
	 * send statements not in the chronological order while
	 * the receiving end expects LSNs to grow monotonically
	 * due to the design of the lsregion allocator, which is
	 * used for storing statements in memory.
	 */
	int64_t join_lsn;
	/** Path to the data directory. */
	char *path;
	/** Max size of the memory level. */
	size_t memory;
	/** Max time a transaction may wait for memory. */
	double timeout;
	/** Max number of threads used for reading. */
	int read_threads;
	/** Max number of threads used for writing. */
	int write_threads;
	/** Try to recover corrupted data if set. */
	bool force_recovery;
};

enum {
	/**
	 * Time interval between successive updates of
	 * quota watermark and use rate, in seconds.
	 */
	VY_QUOTA_UPDATE_INTERVAL = 1,
	/**
	 * Period of time over which the quota use rate
	 * is averaged, in seconds.
	 */
	VY_QUOTA_RATE_AVG_PERIOD = 5,
};

static inline int64_t
vy_dump_bandwidth(struct vy_env *env)
{
	/* See comment to vy_env::dump_bw. */
	return histogram_percentile(env->dump_bw, 10);
}

struct vinyl_engine {
	struct engine base;
	/** Vinyl environment. */
	struct vy_env *env;
};

/** Extract vy_env from an engine object. */
static inline struct vy_env *
vy_env(struct engine *engine)
{
	return ((struct vinyl_engine *)engine)->env;
}

struct vinyl_index {
	struct index base;
	/** LSM tree that stores index data. */
	struct vy_lsm *lsm;
};

/** Extract vy_lsm from an index object. */
struct vy_lsm *
vy_lsm(struct index *index)
{
	return ((struct vinyl_index *)index)->lsm;
}

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

struct vinyl_iterator {
	struct iterator base;
	/** Vinyl environment. */
	struct vy_env *env;
	/** LSM tree this iterator is for. */
	struct vy_lsm *lsm;
	/**
	 * Points either to tx_autocommit for autocommit mode
	 * or to a multi-statement transaction active when the
	 * iterator was created.
	 */
	struct vy_tx *tx;
	/** Search key. */
	struct tuple *key;
	/** Vinyl read iterator. */
	struct vy_read_iterator iterator;
	/**
	 * Built-in transaction created when iterator is opened
	 * in autocommit mode.
	 */
	struct vy_tx tx_autocommit;
	/** Trigger invoked when tx ends to close the iterator. */
	struct trigger on_tx_destroy;
};

static const struct engine_vtab vinyl_engine_vtab;
static const struct space_vtab vinyl_space_vtab;
static const struct index_vtab vinyl_index_vtab;

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

/** {{{ Introspection */

static void
vy_info_append_quota(struct vy_env *env, struct info_handler *h)
{
	struct vy_quota *q = &env->quota;

	info_table_begin(h, "quota");
	info_append_int(h, "used", q->used);
	info_append_int(h, "limit", q->limit);
	info_append_int(h, "watermark", q->watermark);
	info_append_int(h, "use_rate", env->quota_use_rate);
	info_append_int(h, "dump_bandwidth", vy_dump_bandwidth(env));
	info_table_end(h);
}

static void
vy_info_append_cache(struct vy_env *env, struct info_handler *h)
{
	struct vy_cache_env *c = &env->cache_env;

	info_table_begin(h, "cache");

	info_append_int(h, "used", c->mem_used);
	info_append_int(h, "limit", c->mem_quota);

	struct mempool_stats mstats;
	mempool_stats(&c->cache_entry_mempool, &mstats);
	info_append_int(h, "tuples", mstats.objcount);

	info_table_end(h);
}

static void
vy_info_append_tx(struct vy_env *env, struct info_handler *h)
{
	struct tx_manager *xm = env->xm;

	info_table_begin(h, "tx");

	info_append_int(h, "commit", xm->stat.commit);
	info_append_int(h, "rollback", xm->stat.rollback);
	info_append_int(h, "conflict", xm->stat.conflict);

	struct mempool_stats mstats;
	mempool_stats(&xm->tx_mempool, &mstats);
	info_append_int(h, "transactions", mstats.objcount);
	mempool_stats(&xm->txv_mempool, &mstats);
	info_append_int(h, "statements", mstats.objcount);
	mempool_stats(&xm->read_interval_mempool, &mstats);
	info_append_int(h, "gap_locks", mstats.objcount);
	mempool_stats(&xm->read_view_mempool, &mstats);
	info_append_int(h, "read_views", mstats.objcount);

	info_table_end(h);
}

void
vinyl_engine_info(struct vinyl_engine *vinyl, struct info_handler *h)
{
	struct vy_env *env = vinyl->env;

	info_begin(h);
	vy_info_append_quota(env, h);
	vy_info_append_cache(env, h);
	vy_info_append_tx(env, h);
	info_end(h);
}

static void
vy_info_append_stmt_counter(struct info_handler *h, const char *name,
			    const struct vy_stmt_counter *count)
{
	if (name != NULL)
		info_table_begin(h, name);
	info_append_int(h, "rows", count->rows);
	info_append_int(h, "bytes", count->bytes);
	if (name != NULL)
		info_table_end(h);
}

static void
vy_info_append_disk_stmt_counter(struct info_handler *h, const char *name,
				 const struct vy_disk_stmt_counter *count)
{
	if (name != NULL)
		info_table_begin(h, name);
	info_append_int(h, "rows", count->rows);
	info_append_int(h, "bytes", count->bytes);
	info_append_int(h, "bytes_compressed", count->bytes_compressed);
	info_append_int(h, "pages", count->pages);
	if (name != NULL)
		info_table_end(h);
}

static void
vy_info_append_compact_stat(struct info_handler *h, const char *name,
			    const struct vy_compact_stat *stat)
{
	info_table_begin(h, name);
	info_append_int(h, "count", stat->count);
	vy_info_append_stmt_counter(h, "in", &stat->in);
	vy_info_append_stmt_counter(h, "out", &stat->out);
	info_table_end(h);
}

static void
vinyl_index_info(struct index *index, struct info_handler *h)
{
	char buf[1024];
	struct vy_lsm *lsm = vy_lsm(index);
	struct vy_lsm_stat *stat = &lsm->stat;
	struct vy_cache_stat *cache_stat = &lsm->cache.stat;

	info_begin(h);

	struct vy_stmt_counter count = stat->memory.count;
	vy_stmt_counter_add_disk(&count, &stat->disk.count);
	vy_info_append_stmt_counter(h, NULL, &count);

	info_append_int(h, "lookup", stat->lookup);
	vy_info_append_stmt_counter(h, "get", &stat->get);
	vy_info_append_stmt_counter(h, "put", &stat->put);

	info_table_begin(h, "latency");
	info_append_double(h, "p50", latency_get(&stat->latency, 50));
	info_append_double(h, "p75", latency_get(&stat->latency, 75));
	info_append_double(h, "p90", latency_get(&stat->latency, 90));
	info_append_double(h, "p95", latency_get(&stat->latency, 95));
	info_append_double(h, "p99", latency_get(&stat->latency, 99));
	info_table_end(h);

	info_table_begin(h, "upsert");
	info_append_int(h, "squashed", stat->upsert.squashed);
	info_append_int(h, "applied", stat->upsert.applied);
	info_table_end(h);

	info_table_begin(h, "memory");
	vy_info_append_stmt_counter(h, NULL, &stat->memory.count);
	info_table_begin(h, "iterator");
	info_append_int(h, "lookup", stat->memory.iterator.lookup);
	vy_info_append_stmt_counter(h, "get", &stat->memory.iterator.get);
	info_table_end(h);
	info_append_int(h, "index_size", vy_lsm_mem_tree_size(lsm));
	info_table_end(h);

	info_table_begin(h, "disk");
	vy_info_append_disk_stmt_counter(h, NULL, &stat->disk.count);
	info_table_begin(h, "iterator");
	info_append_int(h, "lookup", stat->disk.iterator.lookup);
	vy_info_append_stmt_counter(h, "get", &stat->disk.iterator.get);
	vy_info_append_disk_stmt_counter(h, "read", &stat->disk.iterator.read);
	info_table_begin(h, "bloom");
	info_append_int(h, "hit", stat->disk.iterator.bloom_hit);
	info_append_int(h, "miss", stat->disk.iterator.bloom_miss);
	info_table_end(h);
	info_table_end(h);
	vy_info_append_compact_stat(h, "dump", &stat->disk.dump);
	vy_info_append_compact_stat(h, "compact", &stat->disk.compact);
	info_append_int(h, "index_size", lsm->page_index_size);
	info_append_int(h, "bloom_size", lsm->bloom_size);
	info_table_end(h);

	info_table_begin(h, "cache");
	vy_info_append_stmt_counter(h, NULL, &cache_stat->count);
	info_append_int(h, "lookup", cache_stat->lookup);
	vy_info_append_stmt_counter(h, "get", &cache_stat->get);
	vy_info_append_stmt_counter(h, "put", &cache_stat->put);
	vy_info_append_stmt_counter(h, "invalidate", &cache_stat->invalidate);
	vy_info_append_stmt_counter(h, "evict", &cache_stat->evict);
	info_append_int(h, "index_size",
			vy_cache_tree_mem_used(&lsm->cache.cache_tree));
	info_table_end(h);

	info_table_begin(h, "txw");
	vy_info_append_stmt_counter(h, NULL, &stat->txw.count);
	info_table_begin(h, "iterator");
	info_append_int(h, "lookup", stat->txw.iterator.lookup);
	vy_info_append_stmt_counter(h, "get", &stat->txw.iterator.get);
	info_table_end(h);
	info_table_end(h);

	info_append_int(h, "range_count", lsm->range_count);
	info_append_int(h, "run_count", lsm->run_count);
	info_append_int(h, "run_avg", lsm->run_count / lsm->range_count);
	histogram_snprint(buf, sizeof(buf), lsm->run_hist);
	info_append_str(h, "run_histogram", buf);

	info_end(h);
}

static void
vinyl_index_reset_stat(struct index *index)
{
	struct vy_lsm *lsm = vy_lsm(index);
	struct vy_lsm_stat *stat = &lsm->stat;
	struct vy_cache_stat *cache_stat = &lsm->cache.stat;

	stat->lookup = 0;
	latency_reset(&stat->latency);
	memset(&stat->get, 0, sizeof(stat->get));
	memset(&stat->put, 0, sizeof(stat->put));
	memset(&stat->upsert, 0, sizeof(stat->upsert));
	memset(&stat->txw.iterator, 0, sizeof(stat->txw.iterator));
	memset(&stat->memory.iterator, 0, sizeof(stat->memory.iterator));
	memset(&stat->disk.iterator, 0, sizeof(stat->disk.iterator));
	memset(&stat->disk.dump, 0, sizeof(stat->disk.dump));
	memset(&stat->disk.compact, 0, sizeof(stat->disk.compact));

	cache_stat->lookup = 0;
	memset(&cache_stat->get, 0, sizeof(cache_stat->get));
	memset(&cache_stat->put, 0, sizeof(cache_stat->put));
	memset(&cache_stat->invalidate, 0, sizeof(cache_stat->invalidate));
	memset(&cache_stat->evict, 0, sizeof(cache_stat->evict));
}

static void
vinyl_engine_memory_stat(struct engine *engine, struct engine_memory_stat *stat)
{
	struct vy_env *env = vy_env(engine);
	struct mempool_stats mstats;

	stat->data += lsregion_used(&env->mem_env.allocator) -
				env->mem_env.tree_extent_size;
	stat->index += env->mem_env.tree_extent_size;
	stat->index += env->lsm_env.bloom_size;
	stat->index += env->lsm_env.page_index_size;
	stat->cache += env->cache_env.mem_used;
	stat->tx += env->xm->write_set_size + env->xm->read_set_size;
	mempool_stats(&env->xm->tx_mempool, &mstats);
	stat->tx += mstats.totals.used;
	mempool_stats(&env->xm->txv_mempool, &mstats);
	stat->tx += mstats.totals.used;
	mempool_stats(&env->xm->read_interval_mempool, &mstats);
	stat->tx += mstats.totals.used;
	mempool_stats(&env->xm->read_view_mempool, &mstats);
	stat->tx += mstats.totals.used;
}

static void
vinyl_engine_reset_stat(struct engine *engine)
{
	struct vy_env *env = vy_env(engine);
	struct tx_manager *xm = env->xm;

	memset(&xm->stat, 0, sizeof(xm->stat));
}

/** }}} Introspection */

/**
 * Check if WAL is enabled.
 *
 * Vinyl needs to log all operations done on indexes in its own
 * journal - vylog. If we allowed to use it in conjunction with
 * wal_mode = 'none', vylog and WAL could get out of sync, which
 * can result in weird recovery errors. So we forbid DML/DDL
 * operations in case WAL is disabled.
 */
static inline int
vinyl_check_wal(struct vy_env *env, const char *what)
{
	if (env->status == VINYL_ONLINE && wal_mode() == WAL_NONE) {
		diag_set(ClientError, ER_UNSUPPORTED, "Vinyl",
			 tt_sprintf("%s if wal_mode = 'none'", what));
		return -1;
	}
	return 0;
}

/**
 * Given a space and an index id, return vy_lsm.
 * If index not found, return NULL and set diag.
 */
static struct vy_lsm *
vy_lsm_find(struct space *space, uint32_t iid)
{
	struct index *index = index_find(space, iid);
	if (index == NULL)
		return NULL;
	return vy_lsm(index);
}

/**
 * Wrapper around vy_lsm_find() which ensures that
 * the found index is unique.
 */
static  struct vy_lsm *
vy_lsm_find_unique(struct space *space, uint32_t index_id)
{
	struct vy_lsm *lsm = vy_lsm_find(space, index_id);
	if (lsm != NULL && !lsm->opts.is_unique) {
		diag_set(ClientError, ER_MORE_THAN_ONE_TUPLE);
		return NULL;
	}
	return lsm;
}

static int
vinyl_engine_check_space_def(struct space_def *def)
{
	if (def->opts.temporary) {
		diag_set(ClientError, ER_ALTER_SPACE,
			 def->name, "engine does not support temporary flag");
		return -1;
	}
	return 0;
}

static struct space *
vinyl_engine_create_space(struct engine *engine, struct space_def *def,
			  struct rlist *key_list)
{
	struct space *space = malloc(sizeof(*space));
	if (space == NULL) {
		diag_set(OutOfMemory, sizeof(*space),
			 "malloc", "struct space");
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
		free(space);
		return NULL;
	}
	key_count = 0;
	rlist_foreach_entry(index_def, key_list, link)
		keys[key_count++] = index_def->key_def;

	struct tuple_format *format =
		tuple_format_new(&vy_tuple_format_vtab, keys, key_count, 0,
				 def->fields, def->field_count, def->dict);
	if (format == NULL) {
		free(space);
		return NULL;
	}
	format->exact_field_count = def->exact_field_count;
	tuple_format_ref(format);

	if (space_create(space, engine, &vinyl_space_vtab,
			 def, key_list, format) != 0) {
		tuple_format_unref(format);
		free(space);
		return NULL;
	}

	/* Format is now referenced by the space. */
	tuple_format_unref(format);

	/*
	 * Check if there are unique indexes that are contained
	 * by other unique indexes. For them, we can skip check
	 * for duplicates on INSERT. Prefer indexes with higher
	 * ids for uniqueness check optimization as they are
	 * likelier to have a "colder" cache.
	 */
	for (int i = space->index_count - 1; i >= 0; i--) {
		struct vy_lsm *lsm = vy_lsm(space->index[i]);
		if (!lsm->check_is_unique)
			continue;
		for (int j = 0; j < (int)space->index_count; j++) {
			struct vy_lsm *other = vy_lsm(space->index[j]);
			if (other != lsm && other->check_is_unique &&
			    key_def_contains(lsm->key_def, other->key_def)) {
				lsm->check_is_unique = false;
				break;
			}
		}
	}
	return space;
}

static void
vinyl_space_destroy(struct space *space)
{
	free(space);
}

static int
vinyl_space_check_index_def(struct space *space, struct index_def *index_def)
{
	if (index_def->type != TREE) {
		diag_set(ClientError, ER_INDEX_TYPE,
			 index_def->name, space_name(space));
		return -1;
	}
	if (index_def->key_def->is_nullable && index_def->iid == 0) {
		diag_set(ClientError, ER_NULLABLE_PRIMARY, space_name(space));
		return -1;
	}
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

static struct index *
vinyl_space_create_index(struct space *space, struct index_def *index_def)
{
	assert(index_def->type == TREE);
	struct vinyl_engine *vinyl = (struct vinyl_engine *)space->engine;
	struct vinyl_index *index = calloc(1, sizeof(*index));
	if (index == NULL) {
		diag_set(OutOfMemory, sizeof(*index),
			 "malloc", "struct vinyl_index");
		return NULL;
	}
	struct vy_env *env = vinyl->env;
	struct vy_lsm *pk = NULL;
	if (index_def->iid > 0) {
		pk = vy_lsm(space_index(space, 0));
		assert(pk != NULL);
	}
	struct vy_lsm *lsm = vy_lsm_new(&env->lsm_env, &env->cache_env,
					&env->mem_env, index_def,
					space->format, pk);
	if (lsm == NULL) {
		free(index);
		return NULL;
	}
	if (index_create(&index->base, (struct engine *)vinyl,
			 &vinyl_index_vtab, index_def) != 0) {
		vy_lsm_delete(lsm);
		free(index);
		return NULL;
	}
	index->lsm = lsm;
	return &index->base;
}

static void
vinyl_index_destroy(struct index *index)
{
	struct vy_lsm *lsm = vy_lsm(index);
	/*
	 * There still may be a task scheduled for this LSM tree
	 * so postpone actual deletion until the last reference
	 * is gone.
	 */
	vy_lsm_unref(lsm);
	free(index);
}

/**
 * Detect whether we already have non-garbage index files,
 * and open an existing index if that's the case. Otherwise,
 * create a new index. Take the current recovery status into
 * account.
 */
static int
vinyl_index_open(struct index *index)
{
	struct vy_env *env = vy_env(index->engine);
	struct vy_lsm *lsm = vy_lsm(index);

	/* Ensure vinyl data directory exists. */
	if (access(env->path, F_OK) != 0) {
		diag_set(SystemError, "can not access vinyl data directory");
		return -1;
	}
	int rc;
	switch (env->status) {
	case VINYL_ONLINE:
		/*
		 * The recovery is complete, simply
		 * create a new index.
		 */
		rc = vy_lsm_create(lsm);
		if (rc == 0) {
			/* Make sure reader threads are up and running. */
			vy_run_env_enable_coio(&env->run_env,
					       env->read_threads);
		}
		break;
	case VINYL_INITIAL_RECOVERY_REMOTE:
	case VINYL_FINAL_RECOVERY_REMOTE:
		/*
		 * Remote recovery. The index files do not
		 * exist locally, and we should create the
		 * index directory from scratch.
		 */
		rc = vy_lsm_create(lsm);
		break;
	case VINYL_INITIAL_RECOVERY_LOCAL:
	case VINYL_FINAL_RECOVERY_LOCAL:
		/*
		 * Local WAL replay or recovery from snapshot.
		 * In either case the index directory should
		 * have already been created, so try to load
		 * the index files from it.
		 */
		rc = vy_lsm_recover(lsm, env->recovery, &env->run_env,
				    vclock_sum(env->recovery_vclock),
				    env->status == VINYL_INITIAL_RECOVERY_LOCAL,
				    env->force_recovery);
		break;
	default:
		unreachable();
	}
	return rc;
}

static void
vinyl_index_commit_create(struct index *index, int64_t lsn)
{
	struct vy_env *env = vy_env(index->engine);
	struct vy_lsm *lsm = vy_lsm(index);

	assert(lsm->id >= 0);

	if (env->status == VINYL_INITIAL_RECOVERY_LOCAL ||
	    env->status == VINYL_FINAL_RECOVERY_LOCAL) {
		/*
		 * Normally, if this is local recovery, the index
		 * should have been logged before restart. There's
		 * one exception though - we could've failed to log
		 * index due to a vylog write error, in which case
		 * the index isn't in the recovery context and we
		 * need to retry to log it now.
		 */
		if (lsm->commit_lsn >= 0) {
			vy_scheduler_add_lsm(&env->scheduler, lsm);
			return;
		}
	}

	if (env->status == VINYL_INITIAL_RECOVERY_REMOTE) {
		/*
		 * Records received during initial join do not
		 * have LSNs so we use a fake one to identify
		 * the index in vylog.
		 */
		lsn = ++env->join_lsn;
	}

	/*
	 * Backward compatibility fixup: historically, we used
	 * box.info.signature for LSN of index creation, which
	 * lags behind the LSN of the record that created the
	 * index by 1. So for legacy indexes use the LSN from
	 * index options.
	 */
	if (lsm->opts.lsn != 0)
		lsn = lsm->opts.lsn;

	assert(lsm->commit_lsn < 0);
	lsm->commit_lsn = lsn;

	assert(lsm->range_count == 1);
	struct vy_range *range = vy_range_tree_first(lsm->tree);

	/*
	 * Since it's too late to fail now, in case of vylog write
	 * failure we leave the records we attempted to write in
	 * the log buffer so that they are flushed along with the
	 * next write request. If they don't get flushed before
	 * the instance is shut down, we will replay them on local
	 * recovery.
	 */
	vy_log_tx_begin();
	vy_log_create_lsm(lsm->id, lsm->space_id, lsm->index_id,
			  lsm->key_def, lsn);
	vy_log_insert_range(lsm->id, range->id, NULL, NULL);
	vy_log_tx_try_commit();
	/*
	 * After we committed the index in the log, we can schedule
	 * a task for it.
	 */
	vy_scheduler_add_lsm(&env->scheduler, lsm);
}

static void
vinyl_index_commit_modify(struct index *index, int64_t lsn)
{
	struct vy_env *env = vy_env(index->engine);
	struct vy_lsm *lsm = vy_lsm(index);

	(void)env;
	assert(env->status == VINYL_ONLINE ||
	       env->status == VINYL_FINAL_RECOVERY_LOCAL ||
	       env->status == VINYL_FINAL_RECOVERY_REMOTE);

	if (lsn <= lsm->commit_lsn) {
		/*
		 * This must be local recovery from WAL, when
		 * the operation has already been committed to
		 * vylog.
		 */
		assert(env->status == VINYL_FINAL_RECOVERY_LOCAL);
		return;
	}

	lsm->commit_lsn = lsn;

	vy_log_tx_begin();
	vy_log_modify_lsm(lsm->id, lsm->key_def, lsn);
	vy_log_tx_try_commit();
}

/*
 * Delete all runs, ranges, and slices of a given LSM tree
 * from the metadata log.
 */
static void
vy_log_lsm_prune(struct vy_lsm *lsm, int64_t gc_lsn)
{
	int loops = 0;
	for (struct vy_range *range = vy_range_tree_first(lsm->tree);
	     range != NULL; range = vy_range_tree_next(lsm->tree, range)) {
		struct vy_slice *slice;
		rlist_foreach_entry(slice, &range->slices, in_range)
			vy_log_delete_slice(slice->id);
		vy_log_delete_range(range->id);
		if (++loops % VY_YIELD_LOOPS == 0)
			fiber_sleep(0);
	}
	struct vy_run *run;
	rlist_foreach_entry(run, &lsm->runs, in_lsm) {
		vy_log_drop_run(run->id, gc_lsn);
		if (++loops % VY_YIELD_LOOPS == 0)
			fiber_sleep(0);
	}
}

static void
vinyl_index_commit_drop(struct index *index)
{
	struct vy_env *env = vy_env(index->engine);
	struct vy_lsm *lsm = vy_lsm(index);

	vy_scheduler_remove_lsm(&env->scheduler, lsm);

	/*
	 * We can't abort here, because the index drop request has
	 * already been written to WAL. So if we fail to write the
	 * change to the metadata log, we leave it in the log buffer,
	 * to be flushed along with the next transaction. If it is
	 * not flushed before the instance is shut down, we replay it
	 * on local recovery from WAL.
	 */
	if (env->status == VINYL_FINAL_RECOVERY_LOCAL && lsm->is_dropped)
		return;

	lsm->is_dropped = true;

	vy_log_tx_begin();
	vy_log_lsm_prune(lsm, checkpoint_last(NULL));
	vy_log_drop_lsm(lsm->id);
	vy_log_tx_try_commit();
}

static bool
vinyl_index_depends_on_pk(struct index *index)
{
	(void)index;
	/*
	 * All secondary Vinyl indexes are non-clustered and hence
	 * have to be updated if the primary key is modified.
	 */
	return true;
}

static bool
vinyl_index_def_change_requires_rebuild(struct index *index,
					const struct index_def *new_def)
{
	struct index_def *old_def = index->def;

	assert(old_def->iid == new_def->iid);
	assert(old_def->space_id == new_def->space_id);
	assert(old_def->type == TREE && new_def->type == TREE);

	if (!old_def->opts.is_unique && new_def->opts.is_unique)
		return true;

	assert(index_depends_on_pk(index));
	const struct key_def *old_cmp_def = old_def->cmp_def;
	const struct key_def *new_cmp_def = new_def->cmp_def;

	/*
	 * It is not enough to check only fieldno in case of Vinyl,
	 * because the index may store some overwritten or deleted
	 * statements conforming to the old format. CheckSpaceFormat
	 * won't reveal such statements, but we may still need to
	 * compare them to statements inserted after ALTER hence
	 * we can't narrow field types without index rebuild.
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
		if (key_part_is_nullable(old_part) &&
		    !key_part_is_nullable(new_part))
			return true;
		if (!field_type1_contains_type2(new_part->type, old_part->type))
			return true;
	}
	return false;
}

static void
vinyl_init_system_space(struct space *space)
{
	(void)space;
	unreachable();
}

static void
vinyl_init_ephemeral_space(struct space *space)
{
	(void)space;
	unreachable();
}

static int
vinyl_space_prepare_alter(struct space *old_space, struct space *new_space)
{
	(void)new_space;
	struct vy_env *env = vy_env(old_space->engine);

	if (vinyl_check_wal(env, "DDL") != 0)
		return -1;

	return 0;
}

/** Argument passed to vy_check_format_on_replace(). */
struct vy_check_format_ctx {
	/** Format to check new tuples against. */
	struct tuple_format *format;
	/** Set if a new tuple doesn't conform to the format. */
	bool is_failed;
	/** Container for storing errors. */
	struct diag diag;
};

/**
 * This is an on_replace trigger callback that checks inserted
 * tuples against a new format.
 */
static void
vy_check_format_on_replace(struct trigger *trigger, void *event)
{
	struct txn *txn = event;
	struct txn_stmt *stmt = txn_current_stmt(txn);
	struct vy_check_format_ctx *ctx = trigger->data;

	if (stmt->new_tuple == NULL)
		return; /* DELETE, nothing to do */

	if (ctx->is_failed)
		return; /* already failed, nothing to do */

	if (tuple_validate(ctx->format, stmt->new_tuple) != 0) {
		ctx->is_failed = true;
		diag_move(diag_get(), &ctx->diag);
	}
}

static int
vinyl_space_check_format(struct space *space, struct tuple_format *format)
{
	struct vy_env *env = vy_env(space->engine);

	/*
	 * If this is local recovery, the space was checked before
	 * restart so there's nothing we need to do.
	 */
	if (env->status == VINYL_INITIAL_RECOVERY_LOCAL ||
	    env->status == VINYL_FINAL_RECOVERY_LOCAL)
		return 0;

	if (space->index_count == 0)
		return 0; /* space is empty, nothing to do */

	/*
	 * Iterate over all tuples stored in the given space and
	 * check each of them for conformity to the new format.
	 * Since read iterator may yield, we install an on_replace
	 * trigger to check tuples inserted after we started the
	 * iteration.
	 */
	struct vy_lsm *pk = vy_lsm(space->index[0]);

	struct tuple *key = vy_stmt_new_select(pk->env->key_format, NULL, 0);
	if (key == NULL)
		return -1;

	struct trigger on_replace;
	struct vy_check_format_ctx ctx;
	ctx.format = format;
	ctx.is_failed = false;
	diag_create(&ctx.diag);
	trigger_create(&on_replace, vy_check_format_on_replace, &ctx, NULL);
	trigger_add(&space->on_replace, &on_replace);

	struct vy_read_iterator itr;
	vy_read_iterator_open(&itr, pk, NULL, ITER_ALL, key,
			      &env->xm->p_global_read_view);
	int rc;
	int loops = 0;
	struct tuple *tuple;
	while ((rc = vy_read_iterator_next(&itr, &tuple)) == 0) {
		/*
		 * Read iterator yields only when it reads runs.
		 * Yield periodically in order not to stall the
		 * tx thread in case there are a lot of tuples in
		 * mems or cache.
		 */
		if (++loops % VY_YIELD_LOOPS == 0)
			fiber_sleep(0);
		if (ctx.is_failed) {
			diag_move(&ctx.diag, diag_get());
			rc = -1;
			break;
		}
		if (tuple == NULL)
			break;
		rc = tuple_validate(format, tuple);
		if (rc != 0)
			break;
	}
	vy_read_iterator_close(&itr);
	diag_destroy(&ctx.diag);
	trigger_clear(&on_replace);
	tuple_unref(key);
	return rc;
}

static void
vinyl_space_swap_index(struct space *old_space, struct space *new_space,
		       uint32_t old_index_id, uint32_t new_index_id)
{
	struct vy_lsm *old_lsm = vy_lsm(old_space->index_map[old_index_id]);
	struct vy_lsm *new_lsm = vy_lsm(new_space->index_map[new_index_id]);

	/*
	 * Swap the two indexes between the two spaces,
	 * but leave tuple formats.
	 */
	generic_space_swap_index(old_space, new_space,
				 old_index_id, new_index_id);

	SWAP(old_lsm, new_lsm);
	SWAP(old_lsm->check_is_unique, new_lsm->check_is_unique);
	SWAP(old_lsm->mem_format, new_lsm->mem_format);
	SWAP(old_lsm->mem_format_with_colmask,
	     new_lsm->mem_format_with_colmask);
	SWAP(old_lsm->disk_format, new_lsm->disk_format);
	SWAP(old_lsm->opts, new_lsm->opts);
	key_def_swap(old_lsm->key_def, new_lsm->key_def);
	key_def_swap(old_lsm->cmp_def, new_lsm->cmp_def);

	/* Update pointer to the primary key. */
	vy_lsm_update_pk(old_lsm, vy_lsm(old_space->index_map[0]));
	vy_lsm_update_pk(new_lsm, vy_lsm(new_space->index_map[0]));
}

static int
vinyl_space_add_primary_key(struct space *space)
{
	return vinyl_index_open(space->index[0]);
}

static void
vinyl_space_drop_primary_key(struct space *space)
{
	(void)space;
}

static int
vinyl_space_build_index(struct space *src_space, struct index *new_index,
			struct tuple_format *new_format)
{
	(void)new_format;

	struct vy_env *env = vy_env(src_space->engine);
	struct vy_lsm *pk = vy_lsm(src_space->index[0]);

	/*
	 * During local recovery we are loading existing indexes
	 * from disk, not building new ones.
	 */
	if (env->status != VINYL_INITIAL_RECOVERY_LOCAL &&
	    env->status != VINYL_FINAL_RECOVERY_LOCAL) {
		if (pk->stat.disk.count.rows != 0 ||
		    pk->stat.memory.count.rows != 0) {
			diag_set(ClientError, ER_UNSUPPORTED, "Vinyl",
				 "building an index for a non-empty space");
			return -1;
		}
	}

	/*
	 * Unlike Memtx, Vinyl does not need building of a secondary index.
	 * This is true because of two things:
	 * 1) Vinyl does not support alter of non-empty spaces
	 * 2) During recovery a Vinyl index already has all needed data on disk.
	 * And there are 3 cases:
	 * I. The secondary index is added in snapshot. Then Vinyl was
	 * snapshotted too and all necessary for that moment data is on disk.
	 * II. The secondary index is added in WAL. That means that vinyl
	 * space had no data at that point and had nothing to build. The
	 * index actually could contain recovered data, but it will handle it
	 * by itself during WAL recovery.
	 * III. Vinyl is online. The space is definitely empty and there's
	 * nothing to build.
	 */
	if (vinyl_index_open(new_index) != 0)
		return -1;

	/* Set pointer to the primary key for the new index. */
	vy_lsm_update_pk(vy_lsm(new_index), pk);
	return 0;
}

static size_t
vinyl_space_bsize(struct space *space)
{
	/*
	 * Return the sum size of user data this space
	 * accommodates. Since full tuples are stored in
	 * primary indexes, it is basically the size of
	 * binary data stored in this space's primary index.
	 */
	struct index *pk = space_index(space, 0);
	if (pk == NULL)
		return 0;
	struct vy_lsm *lsm = vy_lsm(pk);
	return lsm->stat.memory.count.bytes + lsm->stat.disk.count.bytes;
}

static ssize_t
vinyl_index_size(struct index *index)
{
	/*
	 * Return the total number of statements in the LSM tree.
	 * Note, it may be greater than the number of tuples actually
	 * stored in the space, but it should be a fairly good estimate.
	 */
	struct vy_lsm *lsm = vy_lsm(index);
	return lsm->stat.memory.count.rows + lsm->stat.disk.count.rows;
}

static ssize_t
vinyl_index_bsize(struct index *index)
{
	/*
	 * Return the cost of indexing user data. For both
	 * primary and secondary indexes, this includes the
	 * size of page index, bloom filter, and memory tree
	 * extents. For secondary indexes, we also add the
	 * total size of statements stored on disk, because
	 * they are only needed for building the index.
	 */
	struct vy_lsm *lsm = vy_lsm(index);
	ssize_t bsize = vy_lsm_mem_tree_size(lsm) +
		lsm->page_index_size + lsm->bloom_size;
	if (lsm->index_id > 0)
		bsize += lsm->stat.disk.count.bytes;
	return bsize;
}

/* {{{ Public API of transaction control: start/end transaction,
 * read, write data in the context of a transaction.
 */

/**
 * Check if a request has already been committed to an LSM tree.
 *
 * If we're recovering the WAL, it may happen so that this
 * particular run was dumped after the checkpoint, and we're
 * replaying records already present in the database. In this
 * case avoid overwriting a newer version with an older one.
 *
 * If the LSM tree is going to be dropped or truncated on WAL
 * recovery, there's no point in replaying statements for it,
 * either.
 */
static inline bool
vy_is_committed_one(struct vy_env *env, struct vy_lsm *lsm)
{
	if (likely(env->status != VINYL_FINAL_RECOVERY_LOCAL))
		return false;
	if (lsm->is_dropped)
		return true;
	if (vclock_sum(env->recovery_vclock) <= lsm->dump_lsn)
		return true;
	return false;
}

/**
 * Check if a request has already been committed to a space.
 * See also vy_is_committed_one().
 */
static inline bool
vy_is_committed(struct vy_env *env, struct space *space)
{
	if (likely(env->status != VINYL_FINAL_RECOVERY_LOCAL))
		return false;
	for (uint32_t iid = 0; iid < space->index_count; iid++) {
		struct vy_lsm *lsm = vy_lsm(space->index[iid]);
		if (!vy_is_committed_one(env, lsm))
			return false;
	}
	return true;
}

/**
 * Get a vinyl tuple from the LSM tree by the key.
 * @param lsm         LSM tree in which search.
 * @param tx          Current transaction.
 * @param rv          Read view.
 * @param key         Key statement.
 * @param[out] result The found tuple is stored here. Must be
 *                    unreferenced after usage.
 *
 * @param  0 Success.
 * @param -1 Memory error or read error.
 */
static inline int
vy_lsm_get(struct vy_lsm *lsm, struct vy_tx *tx,
	     const struct vy_read_view **rv,
	     struct tuple *key, struct tuple **result)
{
	/*
	 * tx can be NULL, for example, if an user calls
	 * space.index.get({key}).
	 */
	assert(tx == NULL || tx->state == VINYL_TX_READY);

	if (tuple_field_count(key) >= lsm->cmp_def->part_count) {
		if (tx != NULL && vy_tx_track_point(tx, lsm, key) != 0)
			return -1;
		return vy_point_lookup(lsm, tx, rv, key, result);
	}

	struct vy_read_iterator itr;
	vy_read_iterator_open(&itr, lsm, tx, ITER_EQ, key, rv);
	int rc = vy_read_iterator_next(&itr, result);
	if (*result != NULL)
		tuple_ref(*result);
	vy_read_iterator_close(&itr);
	return rc;
}

/**
 * Check if the LSM tree contains the key. If true, then set
 * a duplicate key error in the diagnostics area.
 * @param env        Vinyl environment.
 * @param tx         Current transaction.
 * @param space      Target space.
 * @param lsm        LSM tree in which to search.
 * @param key        Key statement.
 *
 * @retval  0 Success, the key isn't found.
 * @retval -1 Memory error or the key is found.
 */
static inline int
vy_check_is_unique(struct vy_env *env, struct vy_tx *tx, struct space *space,
		   struct vy_lsm *lsm, struct tuple *key)
{
	struct tuple *found;
	/*
	 * During recovery we apply rows that were successfully
	 * applied before restart so no conflict is possible.
	 */
	if (env->status != VINYL_ONLINE)
		return 0;
	if (vy_lsm_get(lsm, tx, vy_tx_read_view(tx), key, &found))
		return -1;

	if (found) {
		tuple_unref(found);
		diag_set(ClientError, ER_TUPLE_FOUND,
			 index_name_by_id(space, lsm->index_id),
			 space_name(space));
		return -1;
	}
	return 0;
}

/**
 * Insert a tuple in a primary index LSM tree.
 * @param env   Vinyl environment.
 * @param tx    Current transaction.
 * @param space Target space.
 * @param pk    Primary index LSM tree.
 * @param stmt  Tuple to insert.
 *
 * @retval  0 Success.
 * @retval -1 Memory error or duplicate key error.
 */
static inline int
vy_insert_primary(struct vy_env *env, struct vy_tx *tx, struct space *space,
		  struct vy_lsm *pk, struct tuple *stmt)
{
	assert(vy_stmt_type(stmt) == IPROTO_INSERT);
	assert(tx != NULL && tx->state == VINYL_TX_READY);
	assert(pk->index_id == 0);
	/*
	 * A primary index is always unique and the new tuple must not
	 * conflict with existing tuples.
	 */
	if (pk->check_is_unique &&
	    vy_check_is_unique(env, tx, space, pk, stmt) != 0)
		return -1;
	return vy_tx_set(tx, pk, stmt);
}

/**
 * Insert a tuple in a secondary index LSM tree.
 * @param env       Vinyl environment.
 * @param tx        Current transaction.
 * @param space     Target space.
 * @param lsm       Secondary index LSM tree.
 * @param stmt      Tuple to replace.
 *
 * @retval  0 Success.
 * @retval -1 Memory error or duplicate key error.
 */
static int
vy_insert_secondary(struct vy_env *env, struct vy_tx *tx, struct space *space,
		    struct vy_lsm *lsm, struct tuple *stmt)
{
	assert(vy_stmt_type(stmt) == IPROTO_INSERT ||
	       vy_stmt_type(stmt) == IPROTO_REPLACE);
	assert(tx != NULL && tx->state == VINYL_TX_READY);
	assert(lsm->index_id > 0);
	/*
	 * If the index is unique then the new tuple must not
	 * conflict with existing tuples. If the index is not
	 * unique a conflict is impossible.
	 */
	if (lsm->check_is_unique &&
	    !key_update_can_be_skipped(lsm->key_def->column_mask,
				       vy_stmt_column_mask(stmt)) &&
	    (!lsm->key_def->is_nullable ||
	     !vy_tuple_key_contains_null(stmt, lsm->key_def))) {
		struct tuple *key = vy_stmt_extract_key(stmt, lsm->key_def,
							lsm->env->key_format);
		if (key == NULL)
			return -1;
		int rc = vy_check_is_unique(env, tx, space, lsm, key);
		tuple_unref(key);
		if (rc != 0)
			return -1;
	}
	/*
	 * We must always append the statement to transaction write set
	 * of each LSM tree, even if operation itself does not update
	 * the LSM tree, e.g. it's an UPDATE, to ensure we read our
	 * own writes.
	 */
	return vy_tx_set(tx, lsm, stmt);
}

/**
 * Execute REPLACE in a space with a single index, possibly with
 * lookup for an old tuple if the space has at least one
 * on_replace trigger.
 * @param env     Vinyl environment.
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
vy_replace_one(struct vy_env *env, struct vy_tx *tx, struct space *space,
	       struct request *request, struct txn_stmt *stmt)
{
	(void)env;
	assert(tx != NULL && tx->state == VINYL_TX_READY);
	struct vy_lsm *pk = vy_lsm(space->index[0]);
	assert(pk->index_id == 0);
	if (tuple_validate_raw(pk->mem_format, request->tuple))
		return -1;
	struct tuple *new_tuple =
		vy_stmt_new_replace(pk->mem_format, request->tuple,
				    request->tuple_end);
	if (new_tuple == NULL)
		return -1;
	/**
	 * If the space has triggers, then we need to fetch the
	 * old tuple to pass it to the trigger.
	 */
	if (stmt != NULL && !rlist_empty(&space->on_replace)) {
		if (vy_lsm_get(pk, tx, vy_tx_read_view(tx),
			       new_tuple, &stmt->old_tuple) != 0)
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
 * @param env     Vinyl environment.
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
vy_replace_impl(struct vy_env *env, struct vy_tx *tx, struct space *space,
		struct request *request, struct txn_stmt *stmt)
{
	assert(tx != NULL && tx->state == VINYL_TX_READY);
	struct tuple *old_stmt = NULL;
	struct tuple *new_stmt = NULL;
	struct tuple *delete = NULL;
	struct vy_lsm *pk = vy_lsm_find(space, 0);
	if (pk == NULL) /* space has no primary key */
		return -1;
	/* Primary key is dumped last. */
	assert(!vy_is_committed_one(env, pk));
	assert(pk->index_id == 0);
	if (tuple_validate_raw(pk->mem_format, request->tuple))
		return -1;
	new_stmt = vy_stmt_new_replace(pk->mem_format, request->tuple,
				       request->tuple_end);
	if (new_stmt == NULL)
		return -1;

	/* Get full tuple from the primary index. */
	if (vy_lsm_get(pk, tx, vy_tx_read_view(tx),
			 new_stmt, &old_stmt) != 0)
		goto error;

	if (old_stmt == NULL) {
		/*
		 * We can turn REPLACE into INSERT if the new key
		 * does not have history.
		 */
		vy_stmt_set_type(new_stmt, IPROTO_INSERT);
	}

	/*
	 * Replace in the primary index without explicit deletion
	 * of the old tuple.
	 */
	if (vy_tx_set(tx, pk, new_stmt) != 0)
		goto error;

	if (space->index_count > 1 && old_stmt != NULL) {
		delete = vy_stmt_new_surrogate_delete(pk->mem_format, old_stmt);
		if (delete == NULL)
			goto error;
	}

	/* Update secondary keys, avoid duplicates. */
	for (uint32_t iid = 1; iid < space->index_count; ++iid) {
		struct vy_lsm *lsm = vy_lsm(space->index[iid]);
		if (vy_is_committed_one(env, lsm))
			continue;
		/*
		 * Delete goes first, so if old and new keys
		 * fully match, there is no look up beyond the
		 * transaction index.
		 */
		if (old_stmt != NULL) {
			if (vy_tx_set(tx, lsm, delete) != 0)
				goto error;
		}
		if (vy_insert_secondary(env, tx, space, lsm, new_stmt) != 0)
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
 * Check that the key can be used for search in a unique index
 * LSM tree.
 * @param  lsm        LSM tree for checking.
 * @param  key        MessagePack'ed data, the array without a
 *                    header.
 * @param  part_count Part count of the key.
 *
 * @retval  0 The key is valid.
 * @retval -1 The key is not valid, the appropriate error is set
 *            in the diagnostics area.
 */
static inline int
vy_unique_key_validate(struct vy_lsm *lsm, const char *key,
		       uint32_t part_count)
{
	assert(lsm->opts.is_unique);
	assert(key != NULL || part_count == 0);
	/*
	 * The LSM tree contains tuples with concatenation of
	 * secondary and primary key fields, while the key
	 * supplied by the user only contains the secondary key
	 * fields. Use the correct key def to validate the key.
	 * The key can be used to look up in the LSM tree since
	 * the supplied key parts uniquely identify the tuple,
	 * as long as the index is unique.
	 */
	uint32_t original_part_count = lsm->key_def->part_count;
	if (original_part_count != part_count) {
		diag_set(ClientError, ER_EXACT_MATCH,
			 original_part_count, part_count);
		return -1;
	}
	return key_validate_parts(lsm->cmp_def, key, part_count, false);
}

/**
 * Find a tuple in the primary index LSM tree by the key of the
 * specified LSM tree.
 * @param lsm         LSM tree for which the key is specified.
 *                    Can be both primary and secondary.
 * @param tx          Current transaction.
 * @param rv          Read view.
 * @param key_raw     MessagePack'ed data, the array without a
 *                    header.
 * @param part_count  Count of parts in the key.
 * @param[out] result The found statement is stored here. Must be
 *                    unreferenced after usage.
 *
 * @retval  0 Success.
 * @retval -1 Memory error.
 */
static inline int
vy_lsm_full_by_key(struct vy_lsm *lsm, struct vy_tx *tx,
		   const struct vy_read_view **rv,
		   const char *key_raw, uint32_t part_count,
		   struct tuple **result)
{
	int rc;
	struct tuple *key = vy_stmt_new_select(lsm->env->key_format,
					       key_raw, part_count);
	if (key == NULL)
		return -1;
	struct tuple *found;
	rc = vy_lsm_get(lsm, tx, rv, key, &found);
	tuple_unref(key);
	if (rc != 0)
		return -1;
	if (lsm->index_id == 0 || found == NULL) {
		*result = found;
		return 0;
	}
	/*
	 * No need in vy_tx_track() as the tuple is already
	 * tracked in the secondary index LSM tree.
	 */
	rc = vy_point_lookup(lsm->pk, tx, rv, found, result);
	tuple_unref(found);
	return rc;
}

/**
 * Delete the tuple from all LSM trees of the vinyl space.
 * @param env        Vinyl environment.
 * @param tx         Current transaction.
 * @param space      Vinyl space.
 * @param tuple      Tuple to delete.
 *
 * @retval  0 Success
 * @retval -1 Memory error or the index is not found.
 */
static inline int
vy_delete_impl(struct vy_env *env, struct vy_tx *tx, struct space *space,
	       const struct tuple *tuple)
{
	struct vy_lsm *pk = vy_lsm_find(space, 0);
	if (pk == NULL)
		return -1;
	/* Primary key is dumped last. */
	assert(!vy_is_committed_one(env, pk));
	struct tuple *delete =
		vy_stmt_new_surrogate_delete(pk->mem_format, tuple);
	if (delete == NULL)
		return -1;
	if (vy_tx_set(tx, pk, delete) != 0)
		goto error;

	/* At second, delete from seconary indexes. */
	for (uint32_t i = 1; i < space->index_count; ++i) {
		struct vy_lsm *lsm = vy_lsm(space->index[i]);
		if (vy_is_committed_one(env, lsm))
			continue;
		if (vy_tx_set(tx, lsm, delete) != 0)
			goto error;
	}
	tuple_unref(delete);
	return 0;
error:
	tuple_unref(delete);
	return -1;
}

/**
 * Execute DELETE in a vinyl space.
 * @param env     Vinyl environment.
 * @param tx      Current transaction.
 * @param stmt    Statement for triggers filled with deleted
 *                statement.
 * @param space   Vinyl space.
 * @param request Request with the tuple data.
 *
 * @retval  0 Success
 * @retval -1 Memory error OR the index is not found OR a tuple
 *            reference increment error.
 */
static int
vy_delete(struct vy_env *env, struct vy_tx *tx, struct txn_stmt *stmt,
	  struct space *space, struct request *request)
{
	if (vy_is_committed(env, space))
		return 0;
	struct vy_lsm *pk = vy_lsm_find(space, 0);
	if (pk == NULL)
		return -1;
	struct vy_lsm *lsm = vy_lsm_find_unique(space, request->index_id);
	if (lsm == NULL)
		return -1;
	bool has_secondary = space->index_count > 1;
	const char *key = request->key;
	uint32_t part_count = mp_decode_array(&key);
	if (vy_unique_key_validate(lsm, key, part_count))
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
		if (vy_lsm_full_by_key(lsm, tx, vy_tx_read_view(tx),
				key, part_count, &stmt->old_tuple) != 0)
			return -1;
		if (stmt->old_tuple == NULL)
			return 0;
	}
	if (has_secondary) {
		assert(stmt->old_tuple != NULL);
		return vy_delete_impl(env, tx, space, stmt->old_tuple);
	} else { /* Primary is the single index in the space. */
		assert(lsm->index_id == 0);
		struct tuple *delete =
			vy_stmt_new_surrogate_delete_from_key(request->key,
							      pk->key_def,
							      pk->mem_format);
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
 * @param pk         Primary index LSM tree.
 * @param old_tuple  The tuple before update.
 * @param new_tuple  The tuple after update.
 * @param column_mask Bitmask of the update operation.
 *
 * @retval  0 Success, the primary key is not modified in the new
 *            tuple.
 * @retval -1 Attempt to modify the primary key.
 */
static inline int
vy_check_update(struct space *space, const struct vy_lsm *pk,
		const struct tuple *old_tuple, const struct tuple *new_tuple,
		uint64_t column_mask)
{
	if (!key_update_can_be_skipped(pk->key_def->column_mask, column_mask) &&
	    vy_tuple_compare(old_tuple, new_tuple, pk->key_def) != 0) {
		diag_set(ClientError, ER_CANT_UPDATE_PRIMARY_KEY,
			 index_name_by_id(space, pk->index_id),
			 space_name(space));
		return -1;
	}
	return 0;
}

/**
 * Execute UPDATE in a vinyl space.
 * @param env     Vinyl environment.
 * @param tx      Current transaction.
 * @param stmt    Statement for triggers filled with old and new
 *                statements.
 * @param space   Vinyl space.
 * @param request Request with the tuple data.
 *
 * @retval  0 Success
 * @retval -1 Memory error OR the index is not found OR a tuple
 *            reference increment error.
 */
static int
vy_update(struct vy_env *env, struct vy_tx *tx, struct txn_stmt *stmt,
	  struct space *space, struct request *request)
{
	assert(tx != NULL && tx->state == VINYL_TX_READY);
	if (vy_is_committed(env, space))
		return 0;
	struct vy_lsm *lsm = vy_lsm_find_unique(space, request->index_id);
	if (lsm == NULL)
		return -1;
	const char *key = request->key;
	uint32_t part_count = mp_decode_array(&key);
	if (vy_unique_key_validate(lsm, key, part_count))
		return -1;

	if (vy_lsm_full_by_key(lsm, tx, vy_tx_read_view(tx),
			       key, part_count, &stmt->old_tuple) != 0)
		return -1;
	/* Nothing to update. */
	if (stmt->old_tuple == NULL)
		return 0;

	/* Apply update operations. */
	struct vy_lsm *pk = vy_lsm(space->index[0]);
	assert(pk != NULL);
	assert(pk->index_id == 0);
	/* Primary key is dumped last. */
	assert(!vy_is_committed_one(env, pk));
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
	if (tuple_validate_raw(pk->mem_format, new_tuple))
		return -1;

	struct tuple_format *mask_format = pk->mem_format_with_colmask;
	if (space->index_count == 1) {
		stmt->new_tuple = vy_stmt_new_replace(pk->mem_format, new_tuple,
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

	struct tuple *delete = vy_stmt_new_surrogate_delete(mask_format,
							    stmt->old_tuple);
	if (delete == NULL)
		return -1;
	vy_stmt_set_column_mask(delete, column_mask);

	for (uint32_t i = 1; i < space->index_count; ++i) {
		lsm = vy_lsm(space->index[i]);
		if (vy_is_committed_one(env, lsm))
			continue;
		if (vy_tx_set(tx, lsm, delete) != 0)
			goto error;
		if (vy_insert_secondary(env, tx, space, lsm, stmt->new_tuple))
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
 * @param env       Vinyl environment.
 * @param tx        Current transaction.
 * @param space     Space in which insert.
 * @param stmt      Tuple to upsert.
 *
 * @retval  0 Success.
 * @retval -1 Memory error or a secondary index duplicate error.
 */
static int
vy_insert_first_upsert(struct vy_env *env, struct vy_tx *tx,
		       struct space *space, struct tuple *stmt)
{
	assert(tx != NULL && tx->state == VINYL_TX_READY);
	assert(space->index_count > 0);
	assert(vy_stmt_type(stmt) == IPROTO_INSERT);
	struct vy_lsm *pk = vy_lsm(space->index[0]);
	assert(pk->index_id == 0);
	if (vy_tx_set(tx, pk, stmt) != 0)
		return -1;
	for (uint32_t i = 1; i < space->index_count; ++i) {
		struct vy_lsm *lsm = vy_lsm(space->index[i]);
		if (vy_insert_secondary(env, tx, space, lsm, stmt) != 0)
			return -1;
	}
	return 0;
}

/**
 * Insert UPSERT into the write set of the transaction.
 * @param tx        Transaction which deletes.
 * @param lsm       LSM tree in which \p tx deletes.
 * @param tuple     MessagePack array.
 * @param tuple_end End of the tuple.
 * @param expr      MessagePack array of update operations.
 * @param expr_end  End of the \p expr.
 *
 * @retval  0 Success.
 * @retval -1 Memory error.
 */
static int
vy_lsm_upsert(struct vy_tx *tx, struct vy_lsm *lsm,
	  const char *tuple, const char *tuple_end,
	  const char *expr, const char *expr_end)
{
	assert(tx == NULL || tx->state == VINYL_TX_READY);
	struct tuple *vystmt;
	struct iovec operations[1];
	operations[0].iov_base = (void *)expr;
	operations[0].iov_len = expr_end - expr;
	vystmt = vy_stmt_new_upsert(lsm->mem_format, tuple, tuple_end,
				    operations, 1);
	if (vystmt == NULL)
		return -1;
	assert(vy_stmt_type(vystmt) == IPROTO_UPSERT);
	int rc = vy_tx_set(tx, lsm, vystmt);
	tuple_unref(vystmt);
	return rc;
}

static int
request_normalize_ops(struct request *request)
{
	assert(request->type == IPROTO_UPSERT ||
	       request->type == IPROTO_UPDATE);
	assert(request->index_base != 0);
	char *ops;
	ssize_t ops_len = request->ops_end - request->ops;
	ops = (char *)region_alloc(&fiber()->gc, ops_len);
	if (ops == NULL)
		return -1;
	char *ops_end = ops;
	const char *pos = request->ops;
	int op_cnt = mp_decode_array(&pos);
	ops_end = mp_encode_array(ops_end, op_cnt);
	int op_no = 0;
	for (op_no = 0; op_no < op_cnt; ++op_no) {
		int op_len = mp_decode_array(&pos);
		ops_end = mp_encode_array(ops_end, op_len);

		uint32_t op_name_len;
		const char  *op_name = mp_decode_str(&pos, &op_name_len);
		ops_end = mp_encode_str(ops_end, op_name, op_name_len);

		int field_no;
		if (mp_typeof(*pos) == MP_INT) {
			field_no = mp_decode_int(&pos);
			ops_end = mp_encode_int(ops_end, field_no);
		} else {
			field_no = mp_decode_uint(&pos);
			field_no -= request->index_base;
			ops_end = mp_encode_uint(ops_end, field_no);
		}

		if (*op_name == ':') {
			/**
			 * splice op adjust string pos and copy
			 * 2 additional arguments
			 */
			int str_pos;
			if (mp_typeof(*pos) == MP_INT) {
				str_pos = mp_decode_int(&pos);
				ops_end = mp_encode_int(ops_end, str_pos);
			} else {
				str_pos = mp_decode_uint(&pos);
				str_pos -= request->index_base;
				ops_end = mp_encode_uint(ops_end, str_pos);
			}
			const char *arg = pos;
			mp_next(&pos);
			memcpy(ops_end, arg, pos - arg);
			ops_end += pos - arg;
		}
		const char *arg = pos;
		mp_next(&pos);
		memcpy(ops_end, arg, pos - arg);
		ops_end += pos - arg;
	}
	request->ops = (const char *)ops;
	request->ops_end = (const char *)ops_end;
	request->index_base = 0;

	/* Clear the header to ensure it's rebuilt at commit. */
	request->header = NULL;
	return 0;
}

/**
 * Execute UPSERT in a vinyl space.
 * @param env     Vinyl environment.
 * @param tx      Current transaction.
 * @param stmt    Statement for triggers filled with old and new
 *                statements.
 * @param space   Vinyl space.
 * @param request Request with the tuple data and update
 *                operations.
 *
 * @retval  0 Success
 * @retval -1 Memory error OR the index is not found OR a tuple
 *            reference increment error.
 */
static int
vy_upsert(struct vy_env *env, struct vy_tx *tx, struct txn_stmt *stmt,
	  struct space *space, struct request *request)
{
	assert(tx != NULL && tx->state == VINYL_TX_READY);
	if (vy_is_committed(env, space))
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
	struct vy_lsm *pk = vy_lsm_find(space, 0);
	if (pk == NULL)
		return -1;
	/* Primary key is dumped last. */
	assert(!vy_is_committed_one(env, pk));
	if (tuple_validate_raw(pk->mem_format, tuple))
		return -1;

	if (space->index_count == 1 && rlist_empty(&space->on_replace))
		return vy_lsm_upsert(tx, pk, tuple, tuple_end, ops, ops_end);

	const char *old_tuple, *old_tuple_end;
	const char *new_tuple, *new_tuple_end;
	uint32_t new_size;
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
	struct tuple *key = vy_stmt_extract_key_raw(tuple, tuple_end,
					pk->key_def, pk->env->key_format);
	if (key == NULL)
		return -1;
	int rc = vy_lsm_get(pk, tx, vy_tx_read_view(tx),
			      key, &stmt->old_tuple);
	tuple_unref(key);
	if (rc != 0)
		return -1;
	/*
	 * If the old tuple was not found then UPSERT
	 * turns into INSERT.
	 */
	if (stmt->old_tuple == NULL) {
		stmt->new_tuple = vy_stmt_new_insert(pk->mem_format,
						     tuple, tuple_end);
		if (stmt->new_tuple == NULL)
			return -1;
		return vy_insert_first_upsert(env, tx, space, stmt->new_tuple);
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
	if (tuple_validate_raw(pk->mem_format, new_tuple))
		return -1;
	new_tuple_end = new_tuple + new_size;
	struct tuple_format *mask_format = pk->mem_format_with_colmask;
	if (space->index_count == 1) {
		stmt->new_tuple = vy_stmt_new_replace(pk->mem_format, new_tuple,
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
		diag_log();
		/*
		 * Upsert is skipped, to match the semantics of
		 * vy_lsm_upsert().
		 */
		return 0;
	}
	if (vy_tx_set(tx, pk, stmt->new_tuple))
		return -1;
	if (space->index_count == 1)
		return 0;

	/* Replace in secondary indexes works as delete insert. */
	struct tuple *delete = vy_stmt_new_surrogate_delete(mask_format,
							    stmt->old_tuple);
	if (delete == NULL)
		return -1;
	vy_stmt_set_column_mask(delete, column_mask);

	for (uint32_t i = 1; i < space->index_count; ++i) {
		struct vy_lsm *lsm = vy_lsm(space->index[i]);
		if (vy_is_committed_one(env, lsm))
			continue;
		if (vy_tx_set(tx, lsm, delete) != 0)
			goto error;
		if (vy_insert_secondary(env, tx, space, lsm,
					stmt->new_tuple) != 0)
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
 * @param env     Vinyl environment.
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
vy_insert(struct vy_env *env, struct vy_tx *tx, struct txn_stmt *stmt,
	  struct space *space, struct request *request)
{
	assert(stmt != NULL);
	struct vy_lsm *pk = vy_lsm_find(space, 0);
	if (pk == NULL)
		/* The space hasn't the primary index. */
		return -1;
	assert(pk->index_id == 0);
	/* Primary key is dumped last. */
	assert(!vy_is_committed_one(env, pk));
	if (tuple_validate_raw(pk->mem_format, request->tuple))
		return -1;
	/* First insert into the primary index. */
	stmt->new_tuple = vy_stmt_new_insert(pk->mem_format, request->tuple,
					     request->tuple_end);
	if (stmt->new_tuple == NULL)
		return -1;
	if (vy_insert_primary(env, tx, space, pk, stmt->new_tuple) != 0)
		return -1;

	for (uint32_t iid = 1; iid < space->index_count; ++iid) {
		struct vy_lsm *lsm = vy_lsm(space->index[iid]);
		if (vy_is_committed_one(env, lsm))
			continue;
		if (vy_insert_secondary(env, tx, space, lsm,
					stmt->new_tuple) != 0)
			return -1;
	}
	return 0;
}

/**
 * Execute REPLACE in a vinyl space.
 * @param env     Vinyl environment.
 * @param tx      Current transaction.
 * @param stmt    Statement for triggers filled with old
 *                statement.
 * @param space   Vinyl space.
 * @param request Request with the tuple data.
 *
 * @retval  0 Success
 * @retval -1 Memory error OR duplicate key error OR the primary
 *            index is not found OR a tuple reference increment
 *            error.
 */
static int
vy_replace(struct vy_env *env, struct vy_tx *tx, struct txn_stmt *stmt,
	   struct space *space, struct request *request)
{
	if (vy_is_committed(env, space))
		return 0;
	if (request->type == IPROTO_INSERT)
		return vy_insert(env, tx, stmt, space, request);

	if (space->index_count == 1) {
		/* Replace in a space with a single index. */
		return vy_replace_one(env, tx, space, request, stmt);
	} else {
		/* Replace in a space with secondary indexes. */
		return vy_replace_impl(env, tx, space, request, stmt);
	}
}

static int
vinyl_space_execute_replace(struct space *space, struct txn *txn,
			    struct request *request, struct tuple **result)
{
	assert(request->index_id == 0);
	struct vy_env *env = vy_env(space->engine);
	struct vy_tx *tx = txn->engine_tx;
	struct txn_stmt *stmt = txn_current_stmt(txn);
	if (vy_replace(env, tx, stmt, space, request))
		return -1;
	*result = stmt->new_tuple;
	return 0;
}

static int
vinyl_space_execute_delete(struct space *space, struct txn *txn,
			   struct request *request, struct tuple **result)
{
	struct vy_env *env = vy_env(space->engine);
	struct vy_tx *tx = txn->engine_tx;
	struct txn_stmt *stmt = txn_current_stmt(txn);
	if (vy_delete(env, tx, stmt, space, request))
		return -1;
	/*
	 * Delete may or may not set stmt->old_tuple,
	 * but we always return NULL.
	 */
	*result = NULL;
	return 0;
}

static int
vinyl_space_execute_update(struct space *space, struct txn *txn,
			   struct request *request, struct tuple **result)
{
	struct vy_env *env = vy_env(space->engine);
	struct vy_tx *tx = txn->engine_tx;
	struct txn_stmt *stmt = txn_current_stmt(txn);
	if (vy_update(env, tx, stmt, space, request) != 0)
		return -1;
	*result = stmt->new_tuple;
	return 0;
}

static int
vinyl_space_execute_upsert(struct space *space, struct txn *txn,
                           struct request *request)
{
	struct vy_env *env = vy_env(space->engine);
	struct vy_tx *tx = txn->engine_tx;
	struct txn_stmt *stmt = txn_current_stmt(txn);
	return vy_upsert(env, tx, stmt, space, request);
}

static int
vinyl_space_ephemeral_replace(struct space *space, const char *tuple,
			      const char *tuple_end)
{
	(void)space;
	(void)tuple;
	(void)tuple_end;
	unreachable();
	return -1;
}

static int
vinyl_space_ephemeral_delete(struct space *space, const char *key)
{
	(void)space;
	(void)key;
	unreachable();
	return -1;
}

static void
vinyl_space_ephemeral_cleanup(struct space *space)
{
	(void)space;
	unreachable();
}

static inline void
txn_stmt_unref_tuples(struct txn_stmt *stmt)
{
	if (stmt->old_tuple)
		tuple_unref(stmt->old_tuple);
	if (stmt->new_tuple)
		tuple_unref(stmt->new_tuple);
	stmt->old_tuple = NULL;
	stmt->new_tuple = NULL;
}

static void
txn_on_stop(struct trigger *trigger, void *event)
{
	(void)trigger;
	(void)event;
	txn_rollback();
}

static int
vinyl_engine_begin(struct engine *engine, struct txn *txn)
{
	struct vy_env *env = vy_env(engine);
	assert(txn->engine_tx == NULL);
	txn->engine_tx = vy_tx_begin(env->xm);
	if (txn->engine_tx == NULL)
		return -1;
	if (!txn->is_autocommit) {
		trigger_create(&txn->fiber_on_stop, txn_on_stop, NULL, NULL);
		trigger_add(&fiber()->on_stop, &txn->fiber_on_stop);
	}
	return 0;
}

static int
vinyl_engine_prepare(struct engine *engine, struct txn *txn)
{
	struct vy_env *env = vy_env(engine);
	struct vy_tx *tx = txn->engine_tx;
	assert(tx != NULL);

	if (tx->write_size > 0 &&
	    vinyl_check_wal(env, "DML") != 0)
		return -1;

	/*
	 * The configured memory limit will never allow us to commit
	 * this transaction. Fail early.
	 */
	if (tx->write_size > env->quota.limit) {
		diag_set(OutOfMemory, tx->write_size,
			 "lsregion", "vinyl transaction");
		return -1;
	}

	/*
	 * Do not abort join/subscribe on quota timeout - replication
	 * is asynchronous anyway and there's box.info.replication
	 * available for the admin to track the lag so let the applier
	 * wait as long as necessary for memory dump to complete.
	 */
	double timeout = (current_session()->type != SESSION_TYPE_APPLIER ?
			  env->timeout : TIMEOUT_INFINITY);
	/*
	 * Reserve quota needed by the transaction before allocating
	 * memory. Since this may yield, which opens a time window for
	 * the transaction to be sent to read view or aborted, we call
	 * it before checking for conflicts.
	 */
	if (vy_quota_use(&env->quota, tx->write_size, timeout) != 0) {
		diag_set(ClientError, ER_VY_QUOTA_TIMEOUT);
		return -1;
	}

	size_t mem_used_before = lsregion_used(&env->mem_env.allocator);

	int rc = vy_tx_prepare(tx);

	size_t mem_used_after = lsregion_used(&env->mem_env.allocator);
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

	env->quota_use_curr += write_size;
	return 0;
}

static void
vinyl_engine_commit(struct engine *engine, struct txn *txn)
{
	struct vy_env *env = vy_env(engine);
	struct vy_tx *tx = txn->engine_tx;
	assert(tx != NULL);

	/*
	 * vy_tx_commit() may trigger an upsert squash.
	 * If there is no memory for a created statement,
	 * it silently fails. But if it succeeds, we
	 * need to account the memory in the quota.
	 */
	size_t mem_used_before = lsregion_used(&env->mem_env.allocator);

	vy_tx_commit(tx, txn->signature);

	size_t mem_used_after = lsregion_used(&env->mem_env.allocator);
	assert(mem_used_after >= mem_used_before);
	/* We can't abort the transaction at this point, use force. */
	vy_quota_force_use(&env->quota, mem_used_after - mem_used_before);

	struct txn_stmt *stmt;
	stailq_foreach_entry(stmt, &txn->stmts, next)
		txn_stmt_unref_tuples(stmt);
	txn->engine_tx = NULL;

	if (!txn->is_autocommit)
		trigger_clear(&txn->fiber_on_stop);
}

static void
vinyl_engine_rollback(struct engine *engine, struct txn *txn)
{
	(void)engine;
	struct vy_tx *tx = txn->engine_tx;
	if (tx == NULL)
		return;

	vy_tx_rollback(tx);

	struct txn_stmt *stmt;
	stailq_foreach_entry(stmt, &txn->stmts, next)
		txn_stmt_unref_tuples(stmt);
	txn->engine_tx = NULL;

	if (!txn->is_autocommit)
		trigger_clear(&txn->fiber_on_stop);
}

static int
vinyl_engine_begin_statement(struct engine *engine, struct txn *txn)
{
	(void)engine;
	struct vy_tx *tx = txn->engine_tx;
	struct txn_stmt *stmt = txn_current_stmt(txn);
	assert(tx != NULL);
	stmt->engine_savepoint = vy_tx_savepoint(tx);
	return 0;
}

static void
vinyl_engine_rollback_statement(struct engine *engine, struct txn *txn,
				struct txn_stmt *stmt)
{
	(void)engine;
	struct vy_tx *tx = txn->engine_tx;
	assert(tx != NULL);
	vy_tx_rollback_to_savepoint(tx, stmt->engine_savepoint);
	txn_stmt_unref_tuples(stmt);
}

/* }}} Public API of transaction control */

/** {{{ Environment */

static void
vy_env_quota_timer_cb(ev_loop *loop, ev_timer *timer, int events)
{
	(void)loop;
	(void)events;

	struct vy_env *e = timer->data;

	/*
	 * Update the quota use rate with the new measurement.
	 */
	const double weight = 1 - exp(-VY_QUOTA_UPDATE_INTERVAL /
				      (double)VY_QUOTA_RATE_AVG_PERIOD);
	e->quota_use_rate = (1 - weight) * e->quota_use_rate +
		weight * e->quota_use_curr / VY_QUOTA_UPDATE_INTERVAL;
	e->quota_use_curr = 0;

	/*
	 * Due to log structured nature of the lsregion allocator,
	 * which is used for allocating statements, we cannot free
	 * memory in chunks, only all at once. Therefore we should
	 * configure the watermark so that by the time we hit the
	 * limit, all memory have been dumped, i.e.
	 *
	 *   limit - watermark      watermark
	 *   ----------------- = --------------
	 *     quota_use_rate    dump_bandwidth
	 */
	int64_t dump_bandwidth = vy_dump_bandwidth(e);
	size_t watermark = ((double)e->quota.limit * dump_bandwidth /
			    (dump_bandwidth + e->quota_use_rate + 1));

	vy_quota_set_watermark(&e->quota, watermark);
}

static void
vy_env_quota_exceeded_cb(struct vy_quota *quota)
{
	struct vy_env *env = container_of(quota, struct vy_env, quota);

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

	if (lsregion_used(&env->mem_env.allocator) == 0) {
		/*
		 * The memory limit has been exceeded, but there's
		 * nothing to dump. This may happen if all available
		 * quota has been consumed by pending transactions.
		 * There's nothing we can do about that.
		 */
		return;
	}
	vy_scheduler_trigger_dump(&env->scheduler);
}

static void
vy_env_dump_complete_cb(struct vy_scheduler *scheduler,
			int64_t dump_generation, double dump_duration)
{
	struct vy_env *env = container_of(scheduler, struct vy_env, scheduler);

	/* Free memory and release quota. */
	struct lsregion *allocator = &env->mem_env.allocator;
	struct vy_quota *quota = &env->quota;
	size_t mem_used_before = lsregion_used(allocator);
	lsregion_gc(allocator, dump_generation);
	size_t mem_used_after = lsregion_used(allocator);
	assert(mem_used_after <= mem_used_before);
	size_t mem_dumped = mem_used_before - mem_used_after;
	vy_quota_release(quota, mem_dumped);

	say_info("dumped %zu bytes in %.1f sec", mem_dumped, dump_duration);

	/* Account dump bandwidth. */
	if (dump_duration > 0)
		histogram_collect(env->dump_bw,
				  mem_dumped / dump_duration);
}

static struct vy_squash_queue *
vy_squash_queue_new(void);
static void
vy_squash_queue_delete(struct vy_squash_queue *q);
static void
vy_squash_schedule(struct vy_lsm *lsm, struct tuple *stmt,
		   void /* struct vy_env */ *arg);

static struct vy_env *
vy_env_new(const char *path, size_t memory,
	   int read_threads, int write_threads, bool force_recovery)
{
	enum { KB = 1000, MB = 1000 * 1000 };
	static int64_t dump_bandwidth_buckets[] = {
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

	struct vy_env *e = malloc(sizeof(*e));
	if (unlikely(e == NULL)) {
		diag_set(OutOfMemory, sizeof(*e), "malloc", "struct vy_env");
		return NULL;
	}
	memset(e, 0, sizeof(*e));
	e->status = VINYL_OFFLINE;
	e->memory = memory;
	e->timeout = TIMEOUT_INFINITY;
	e->read_threads = read_threads;
	e->write_threads = write_threads;
	e->force_recovery = force_recovery;
	e->path = strdup(path);
	if (e->path == NULL) {
		diag_set(OutOfMemory, strlen(path),
			 "malloc", "env->path");
		goto error_path;
	}

	e->dump_bw = histogram_new(dump_bandwidth_buckets,
				   lengthof(dump_bandwidth_buckets));
	if (e->dump_bw == NULL) {
		diag_set(OutOfMemory, 0, "histogram_new",
			 "dump bandwidth histogram");
		goto error_dump_bw;
	}
	/*
	 * Until we dump anything, assume bandwidth to be 10 MB/s,
	 * which should be fine for initial guess.
	 */
	histogram_collect(e->dump_bw, 10 * MB);

	e->xm = tx_manager_new();
	if (e->xm == NULL)
		goto error_xm;
	e->squash_queue = vy_squash_queue_new();
	if (e->squash_queue == NULL)
		goto error_squash_queue;

	vy_mem_env_create(&e->mem_env, e->memory);
	vy_scheduler_create(&e->scheduler, e->write_threads,
			    vy_env_dump_complete_cb,
			    &e->run_env, &e->xm->read_views);

	if (vy_lsm_env_create(&e->lsm_env, e->path,
			      &e->scheduler.generation,
			      vy_squash_schedule, e) != 0)
		goto error_lsm_env;

	struct slab_cache *slab_cache = cord_slab_cache();
	mempool_create(&e->iterator_pool, slab_cache,
	               sizeof(struct vinyl_iterator));
	vy_quota_create(&e->quota, vy_env_quota_exceeded_cb);
	ev_timer_init(&e->quota_timer, vy_env_quota_timer_cb, 0,
		      VY_QUOTA_UPDATE_INTERVAL);
	e->quota_timer.data = e;
	ev_timer_start(loop(), &e->quota_timer);
	vy_cache_env_create(&e->cache_env, slab_cache);
	vy_run_env_create(&e->run_env);
	vy_log_init(e->path);
	return e;
error_lsm_env:
	vy_mem_env_destroy(&e->mem_env);
	vy_scheduler_destroy(&e->scheduler);
	vy_squash_queue_delete(e->squash_queue);
error_squash_queue:
	tx_manager_delete(e->xm);
error_xm:
	histogram_delete(e->dump_bw);
error_dump_bw:
	free(e->path);
error_path:
	free(e);
	return NULL;
}

static void
vy_env_delete(struct vy_env *e)
{
	ev_timer_stop(loop(), &e->quota_timer);
	vy_scheduler_destroy(&e->scheduler);
	vy_squash_queue_delete(e->squash_queue);
	tx_manager_delete(e->xm);
	free(e->path);
	histogram_delete(e->dump_bw);
	mempool_destroy(&e->iterator_pool);
	vy_run_env_destroy(&e->run_env);
	vy_lsm_env_destroy(&e->lsm_env);
	vy_mem_env_destroy(&e->mem_env);
	vy_cache_env_destroy(&e->cache_env);
	vy_quota_destroy(&e->quota);
	if (e->recovery != NULL)
		vy_recovery_delete(e->recovery);
	vy_log_free();
	TRASH(e);
	free(e);
}

struct vinyl_engine *
vinyl_engine_new(const char *dir, size_t memory,
		 int read_threads, int write_threads, bool force_recovery)
{
	struct vinyl_engine *vinyl = calloc(1, sizeof(*vinyl));
	if (vinyl == NULL) {
		diag_set(OutOfMemory, sizeof(*vinyl),
			 "malloc", "struct vinyl_engine");
		return NULL;
	}

	vinyl->env = vy_env_new(dir, memory, read_threads,
				write_threads, force_recovery);
	if (vinyl->env == NULL) {
		free(vinyl);
		return NULL;
	}

	vinyl->base.vtab = &vinyl_engine_vtab;
	vinyl->base.name = "vinyl";
	return vinyl;
}

static void
vinyl_engine_shutdown(struct engine *engine)
{
	struct vinyl_engine *vinyl = (struct vinyl_engine *)engine;
	vy_env_delete(vinyl->env);
	free(vinyl);
}

void
vinyl_engine_set_cache(struct vinyl_engine *vinyl, size_t quota)
{
	vy_cache_env_set_quota(&vinyl->env->cache_env, quota);
}

void
vinyl_engine_set_max_tuple_size(struct vinyl_engine *vinyl, size_t max_size)
{
	(void)vinyl;
	vy_max_tuple_size = max_size;
}

void
vinyl_engine_set_timeout(struct vinyl_engine *vinyl, double timeout)
{
	vinyl->env->timeout = timeout;
}

void
vinyl_engine_set_too_long_threshold(struct vinyl_engine *vinyl,
				    double too_long_threshold)
{
	vinyl->env->quota.too_long_threshold = too_long_threshold;
	vinyl->env->lsm_env.too_long_threshold = too_long_threshold;
}

/** }}} Environment */

/* {{{ Checkpoint */

static int
vinyl_engine_begin_checkpoint(struct engine *engine)
{
	struct vy_env *env = vy_env(engine);
	assert(env->status == VINYL_ONLINE);
	/*
	 * The scheduler starts worker threads upon the first wakeup.
	 * To avoid starting the threads for nothing, do not wake it
	 * up if Vinyl is not used.
	 */
	if (lsregion_used(&env->mem_env.allocator) == 0)
		return 0;
	if (vy_scheduler_begin_checkpoint(&env->scheduler) != 0)
		return -1;
	return 0;
}

static int
vinyl_engine_wait_checkpoint(struct engine *engine, struct vclock *vclock)
{
	struct vy_env *env = vy_env(engine);
	assert(env->status == VINYL_ONLINE);
	if (vy_scheduler_wait_checkpoint(&env->scheduler) != 0)
		return -1;
	if (vy_log_rotate(vclock) != 0)
		return -1;
	return 0;
}

static void
vinyl_engine_commit_checkpoint(struct engine *engine, struct vclock *vclock)
{
	(void)vclock;
	struct vy_env *env = vy_env(engine);
	assert(env->status == VINYL_ONLINE);
	vy_scheduler_end_checkpoint(&env->scheduler);
}

static void
vinyl_engine_abort_checkpoint(struct engine *engine)
{
	struct vy_env *env = vy_env(engine);
	assert(env->status == VINYL_ONLINE);
	vy_scheduler_end_checkpoint(&env->scheduler);
}

/* }}} Checkpoint */

/** {{{ Recovery */

static int
vinyl_engine_bootstrap(struct engine *engine)
{
	struct vy_env *e = vy_env(engine);
	assert(e->status == VINYL_OFFLINE);
	if (vy_log_bootstrap() != 0)
		return -1;
	vy_quota_set_limit(&e->quota, e->memory);
	e->status = VINYL_ONLINE;
	return 0;
}

static int
vinyl_engine_begin_initial_recovery(struct engine *engine,
				    const struct vclock *recovery_vclock)
{
	struct vy_env *e = vy_env(engine);
	assert(e->status == VINYL_OFFLINE);
	if (recovery_vclock != NULL) {
		e->xm->lsn = vclock_sum(recovery_vclock);
		e->recovery_vclock = recovery_vclock;
		e->recovery = vy_log_begin_recovery(recovery_vclock);
		if (e->recovery == NULL)
			return -1;
		e->status = VINYL_INITIAL_RECOVERY_LOCAL;
	} else {
		if (vy_log_bootstrap() != 0)
			return -1;
		vy_quota_set_limit(&e->quota, e->memory);
		e->status = VINYL_INITIAL_RECOVERY_REMOTE;
	}
	return 0;
}

static int
vinyl_engine_begin_final_recovery(struct engine *engine)
{
	struct vy_env *e = vy_env(engine);
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

static int
vinyl_engine_end_recovery(struct engine *engine)
{
	struct vy_env *e = vy_env(engine);
	switch (e->status) {
	case VINYL_FINAL_RECOVERY_LOCAL:
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
		e->status = VINYL_ONLINE;
		vy_quota_set_limit(&e->quota, e->memory);
		break;
	case VINYL_FINAL_RECOVERY_REMOTE:
		e->status = VINYL_ONLINE;
		break;
	default:
		unreachable();
	}
	/*
	 * Do not start reader threads if no LSM tree was recovered.
	 * The threads will be started lazily upon the first LSM tree
	 * creation, see vinyl_index_open().
	 */
	if (e->lsm_env.lsm_count > 0)
		vy_run_env_enable_coio(&e->run_env, e->read_threads);
	return 0;
}

/** }}} Recovery */

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
	/**
	 * LSM tree key definition, as defined by the user.
	 * We only send the primary key, so the definition
	 * provided by the user is correct for compare.
	 */
	struct key_def *key_def;
	/** LSM tree format used for REPLACE and DELETE statements. */
	struct tuple_format *format;
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
};

/**
 * Recover a slice and add it to the list of slices.
 * Newer slices are supposed to be recovered first.
 * Returns 0 on success, -1 on failure.
 */
static int
vy_prepare_send_slice(struct vy_join_ctx *ctx,
		      struct vy_slice_recovery_info *slice_info)
{
	int rc = -1;
	struct vy_run *run = NULL;
	struct tuple *begin = NULL, *end = NULL;

	run = vy_run_new(&ctx->env->run_env, slice_info->run->id);
	if (run == NULL)
		goto out;
	if (vy_run_recover(run, ctx->env->path, ctx->space_id, 0) != 0)
		goto out;

	if (slice_info->begin != NULL) {
		begin = vy_key_from_msgpack(ctx->env->lsm_env.key_format,
					    slice_info->begin);
		if (begin == NULL)
			goto out;
	}
	if (slice_info->end != NULL) {
		end = vy_key_from_msgpack(ctx->env->lsm_env.key_format,
					  slice_info->end);
		if (end == NULL)
			goto out;
	}

	struct vy_slice *slice = vy_slice_new(slice_info->id, run,
					      begin, end, ctx->key_def);
	if (slice == NULL)
		goto out;

	rlist_add_tail_entry(&ctx->slices, slice, in_join);
	rc = 0;
out:
	if (run != NULL)
		vy_run_unref(run);
	if (begin != NULL)
		tuple_unref(begin);
	if (end != NULL)
		tuple_unref(end);
	return rc;
}

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
		/*
		 * Reset the LSN as the replica will ignore it
		 * anyway - see comment to vy_env::join_lsn.
		 */
		xrow.lsn = 0;
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

/** Merge and send all runs of the given range. */
static int
vy_send_range(struct vy_join_ctx *ctx,
	      struct vy_range_recovery_info *range_info)
{
	int rc;
	struct vy_slice *slice, *tmp;

	if (rlist_empty(&range_info->slices))
		return 0; /* nothing to do */

	/* Recover slices. */
	struct vy_slice_recovery_info *slice_info;
	rlist_foreach_entry(slice_info, &range_info->slices, in_range) {
		rc = vy_prepare_send_slice(ctx, slice_info);
		if (rc != 0)
			goto out_delete_slices;
	}

	/* Create a write iterator. */
	struct rlist fake_read_views;
	rlist_create(&fake_read_views);
	ctx->wi = vy_write_iterator_new(ctx->key_def, ctx->format,
					true, true, &fake_read_views);
	if (ctx->wi == NULL) {
		rc = -1;
		goto out;
	}
	rlist_foreach_entry(slice, &ctx->slices, in_join) {
		rc = vy_write_iterator_new_slice(ctx->wi, slice);
		if (rc != 0)
			goto out_delete_wi;
	}

	/* Do the actual work from the relay thread. */
	bool cancellable = fiber_set_cancellable(false);
	rc = cbus_call(&ctx->relay_pipe, &ctx->tx_pipe, &ctx->cmsg,
		       vy_send_range_f, NULL, TIMEOUT_INFINITY);
	fiber_set_cancellable(cancellable);

out_delete_slices:
	rlist_foreach_entry_safe(slice, &ctx->slices, in_join, tmp)
		vy_slice_delete(slice);
	rlist_create(&ctx->slices);
out_delete_wi:
	ctx->wi->iface->close(ctx->wi);
	ctx->wi = NULL;
out:
	return rc;
}

/** Send all tuples stored in the given LSM tree. */
static int
vy_send_lsm(struct vy_join_ctx *ctx, struct vy_lsm_recovery_info *lsm_info)
{
	int rc = -1;

	if (lsm_info->is_dropped)
		return 0;

	/*
	 * We are only interested in the primary index LSM tree.
	 * Secondary keys will be rebuilt on the destination.
	 */
	if (lsm_info->index_id != 0)
		return 0;

	ctx->space_id = lsm_info->space_id;

	/* Create key definition and tuple format. */
	ctx->key_def = key_def_new_with_parts(lsm_info->key_parts,
					      lsm_info->key_part_count);
	if (ctx->key_def == NULL)
		goto out;
	ctx->format = tuple_format_new(&vy_tuple_format_vtab, &ctx->key_def,
				       1, 0, NULL, 0, NULL);
	if (ctx->format == NULL)
		goto out_free_key_def;
	tuple_format_ref(ctx->format);

	/* Send ranges. */
	struct vy_range_recovery_info *range_info;
	assert(!rlist_empty(&lsm_info->ranges));
	rlist_foreach_entry(range_info, &lsm_info->ranges, in_lsm) {
		rc = vy_send_range(ctx, range_info);
		if (rc != 0)
			break;
	}

	tuple_format_unref(ctx->format);
	ctx->format = NULL;
out_free_key_def:
	key_def_delete(ctx->key_def);
	ctx->key_def = NULL;
out:
	return rc;
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

static int
vinyl_engine_join(struct engine *engine, struct vclock *vclock,
		  struct xstream *stream)
{
	struct vy_env *env = vy_env(engine);
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
	if (recovery == NULL) {
		say_error("failed to recover vylog to join a replica");
		goto out_join_cord;
	}
	rc = 0;
	struct vy_lsm_recovery_info *lsm_info;
	rlist_foreach_entry(lsm_info, &recovery->lsms, in_recovery) {
		rc = vy_send_lsm(ctx, lsm_info);
		if (rc != 0)
			break;
	}
	vy_recovery_delete(recovery);

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

static int
vinyl_space_apply_initial_join_row(struct space *space, struct request *request)
{
	assert(request->header != NULL);
	struct vy_env *env = vy_env(space->engine);

	struct vy_tx *tx = vy_tx_begin(env->xm);
	if (tx == NULL)
		return -1;

	struct txn_stmt stmt;
	memset(&stmt, 0, sizeof(stmt));

	int rc = -1;
	switch (request->type) {
	case IPROTO_INSERT:
	case IPROTO_REPLACE:
		rc = vy_replace(env, tx, &stmt, space, request);
		break;
	case IPROTO_UPSERT:
		rc = vy_upsert(env, tx, &stmt, space, request);
		break;
	case IPROTO_DELETE:
		rc = vy_delete(env, tx, &stmt, space, request);
		break;
	default:
		diag_set(ClientError, ER_UNKNOWN_REQUEST_TYPE, request->type);
		break;
	}
	if (rc != 0) {
		vy_tx_rollback(tx);
		return -1;
	}

	/*
	 * Account memory quota, see vinyl_engine_prepare()
	 * and vinyl_engine_commit() for more details about
	 * quota accounting.
	 */
	size_t reserved = tx->write_size;
	if (vy_quota_use(&env->quota, reserved, TIMEOUT_INFINITY) != 0)
		unreachable();

	size_t mem_used_before = lsregion_used(&env->mem_env.allocator);

	rc = vy_tx_prepare(tx);
	if (rc == 0)
		vy_tx_commit(tx, ++env->join_lsn);
	else
		vy_tx_rollback(tx);

	txn_stmt_unref_tuples(&stmt);

	size_t mem_used_after = lsregion_used(&env->mem_env.allocator);
	assert(mem_used_after >= mem_used_before);
	size_t used = mem_used_after - mem_used_before;
	if (used >= reserved)
		vy_quota_force_use(&env->quota, used - reserved);
	else
		vy_quota_release(&env->quota, reserved - used);

	return rc;
}

/* }}} Replication */

/* {{{ Garbage collection */

/**
 * Given a record encoding information about a vinyl run, try to
 * delete the corresponding files. On success, write a "forget" record
 * to the log so that all information about the run is deleted on the
 * next log rotation.
 */
static void
vy_gc_run(struct vy_env *env,
	  struct vy_lsm_recovery_info *lsm_info,
	  struct vy_run_recovery_info *run_info)
{
	ERROR_INJECT(ERRINJ_VY_GC,
		     {say_error("error injection: vinyl run %lld not deleted",
				(long long)run_info->id); return;});

	/* Try to delete files. */
	if (vy_run_remove_files(env->path, lsm_info->space_id,
				lsm_info->index_id, run_info->id) != 0)
		return;

	/* Forget the run on success. */
	vy_log_tx_begin();
	vy_log_forget_run(run_info->id);
	/*
	 * Leave the record in the vylog buffer on disk error.
	 * If we fail to flush it before restart, we will retry
	 * to delete the run file next time garbage collection
	 * is invoked, which is harmless.
	 */
	vy_log_tx_try_commit();
}

/**
 * Delete unused run files stored in the recovery context.
 * @param env      Vinyl environment.
 * @param recovery Recovery context.
 * @param gc_mask  Specifies what kinds of runs to delete (see VY_GC_*).
 * @param gc_lsn   LSN of the oldest checkpoint to save.
 */
static void
vy_gc(struct vy_env *env, struct vy_recovery *recovery,
      unsigned int gc_mask, int64_t gc_lsn)
{
	int loops = 0;
	struct vy_lsm_recovery_info *lsm_info;
	rlist_foreach_entry(lsm_info, &recovery->lsms, in_recovery) {
		struct vy_run_recovery_info *run_info;
		rlist_foreach_entry(run_info, &lsm_info->runs, in_lsm) {
			if ((run_info->is_dropped &&
			     run_info->gc_lsn < gc_lsn &&
			     (gc_mask & VY_GC_DROPPED) != 0) ||
			    (run_info->is_incomplete &&
			     (gc_mask & VY_GC_INCOMPLETE) != 0)) {
				vy_gc_run(env, lsm_info, run_info);
			}
			if (loops % VY_YIELD_LOOPS == 0)
				fiber_sleep(0);
		}
	}
}

static int
vinyl_engine_collect_garbage(struct engine *engine, int64_t lsn)
{
	struct vy_env *env = vy_env(engine);

	/* Cleanup old metadata log files. */
	vy_log_collect_garbage(lsn);

	/* Cleanup run files. */
	int64_t signature = checkpoint_last(NULL);
	struct vy_recovery *recovery = vy_recovery_new(signature, false);
	if (recovery == NULL) {
		say_error("failed to recover vylog for garbage collection");
		return 0;
	}
	vy_gc(env, recovery, VY_GC_DROPPED, lsn);
	vy_recovery_delete(recovery);
	return 0;
}

/* }}} Garbage collection */

/* {{{ Backup */

static int
vinyl_engine_backup(struct engine *engine, struct vclock *vclock,
		    engine_backup_cb cb, void *cb_arg)
{
	struct vy_env *env = vy_env(engine);

	/* Backup the metadata log. */
	const char *path = vy_log_backup_path(vclock);
	if (path == NULL)
		return 0; /* vinyl not used */
	if (cb(path, cb_arg) != 0)
		return -1;

	/* Backup run files. */
	struct vy_recovery *recovery;
	recovery = vy_recovery_new(vclock_sum(vclock), true);
	if (recovery == NULL) {
		say_error("failed to recover vylog for backup");
		return -1;
	}
	int rc = 0;
	int loops = 0;
	struct vy_lsm_recovery_info *lsm_info;
	rlist_foreach_entry(lsm_info, &recovery->lsms, in_recovery) {
		if (lsm_info->is_dropped)
			continue;
		struct vy_run_recovery_info *run_info;
		rlist_foreach_entry(run_info, &lsm_info->runs, in_lsm) {
			if (run_info->is_dropped || run_info->is_incomplete)
				continue;
			char path[PATH_MAX];
			for (int type = 0; type < vy_file_MAX; type++) {
				vy_run_snprint_path(path, sizeof(path),
						    env->path,
						    lsm_info->space_id,
						    lsm_info->index_id,
						    run_info->id, type);
				rc = cb(path, cb_arg);
				if (rc != 0)
					goto out;
			}
			if (loops % VY_YIELD_LOOPS == 0)
				fiber_sleep(0);
		}
	}
out:
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
	/** Vinyl environment. */
	struct vy_env *env;
	/** LSM tree this request is for. */
	struct vy_lsm *lsm;
	/** Key to squash upserts for. */
	struct tuple *stmt;
};

struct vy_squash_queue {
	/** Fiber doing background upsert squashing. */
	struct fiber *fiber;
	/** Used to wake up the fiber to process more requests. */
	struct fiber_cond cond;
	/** Queue of vy_squash objects to be processed. */
	struct stailq queue;
	/** Mempool for struct vy_squash. */
	struct mempool pool;
};

static struct vy_squash *
vy_squash_new(struct mempool *pool, struct vy_env *env,
	      struct vy_lsm *lsm, struct tuple *stmt)
{
	struct vy_squash *squash;
	squash = mempool_alloc(pool);
	if (squash == NULL)
		return NULL;
	squash->env = env;
	vy_lsm_ref(lsm);
	squash->lsm = lsm;
	tuple_ref(stmt);
	squash->stmt = stmt;
	return squash;
}

static void
vy_squash_delete(struct mempool *pool, struct vy_squash *squash)
{
	vy_lsm_unref(squash->lsm);
	tuple_unref(squash->stmt);
	mempool_free(pool, squash);
}

static int
vy_squash_process(struct vy_squash *squash)
{
	struct errinj *inj = errinj(ERRINJ_VY_SQUASH_TIMEOUT, ERRINJ_DOUBLE);
	if (inj != NULL && inj->dparam > 0)
		fiber_sleep(inj->dparam);

	struct vy_lsm *lsm = squash->lsm;
	struct vy_env *env = squash->env;
	/*
	 * vy_apply_upsert() is used for primary key only,
	 * so this is the same as lsm->key_def
	 */
	struct key_def *def = lsm->cmp_def;

	/* Upserts enabled only in the primary index LSM tree. */
	assert(lsm->index_id == 0);

	/*
	 * Use the committed read view to avoid squashing
	 * prepared, but not committed statements.
	 */
	struct tuple *result;
	if (vy_point_lookup(lsm, NULL, &env->xm->p_committed_read_view,
			    squash->stmt, &result) != 0)
		return -1;
	if (result == NULL)
		return 0;

	/*
	 * While we were reading on-disk runs, new statements could
	 * have been inserted into the in-memory tree. Apply them to
	 * the result.
	 */
	struct vy_mem *mem = lsm->mem;
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
	 * vy_lsm_commit_upsert will not be able to squash them.
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
		assert(lsm->index_id == 0);
		struct tuple *applied = vy_apply_upsert(mem_stmt, result, def,
							mem->format, true);
		lsm->stat.upsert.applied++;
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

	lsm->stat.upsert.squashed++;

	/*
	 * Insert the resulting REPLACE statement to the mem
	 * and adjust the quota.
	 */
	size_t mem_used_before = lsregion_used(&env->mem_env.allocator);
	const struct tuple *region_stmt = NULL;
	int rc = vy_lsm_set(lsm, mem, result, &region_stmt);
	tuple_unref(result);
	size_t mem_used_after = lsregion_used(&env->mem_env.allocator);
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
	fiber_cond_create(&sq->cond);
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
		fiber_cond_signal(&sq->cond);
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
			fiber_cond_wait(&sq->cond);
			continue;
		}
		struct vy_squash *squash;
		squash = stailq_shift_entry(&sq->queue, struct vy_squash, next);
		if (vy_squash_process(squash) != 0)
			diag_log();
		vy_squash_delete(&sq->pool, squash);
	}
	return 0;
}

/*
 * For a given UPSERT statement, insert the resulting REPLACE
 * statement after it. Done in a background fiber.
 */
static void
vy_squash_schedule(struct vy_lsm *lsm, struct tuple *stmt, void *arg)
{
	struct vy_env *env = arg;
	struct vy_squash_queue *sq = env->squash_queue;

	say_verbose("%s: schedule upsert optimization for %s",
		    vy_lsm_name(lsm), vy_stmt_str(stmt));

	/* Start the upsert squashing fiber on demand. */
	if (sq->fiber == NULL) {
		sq->fiber = fiber_new("vinyl.squash_queue", vy_squash_queue_f);
		if (sq->fiber == NULL)
			goto fail;
		fiber_start(sq->fiber, sq);
	}

	struct vy_squash *squash = vy_squash_new(&sq->pool, env, lsm, stmt);
	if (squash == NULL)
		goto fail;

	stailq_add_tail_entry(&sq->queue, squash, next);
	fiber_cond_signal(&sq->cond);
	return;
fail:
	diag_log();
	diag_clear(diag_get());
}

/* {{{ Cursor */

static void
vinyl_iterator_on_tx_destroy(struct trigger *trigger, void *event)
{
	(void)event;
	struct vinyl_iterator *it = container_of(trigger,
			struct vinyl_iterator, on_tx_destroy);
	it->tx = NULL;
}

static int
vinyl_iterator_last(struct iterator *base, struct tuple **ret)
{
	(void)base;
	*ret = NULL;
	return 0;
}

static void
vinyl_iterator_close(struct vinyl_iterator *it)
{
	vy_read_iterator_close(&it->iterator);
	vy_lsm_unref(it->lsm);
	it->lsm = NULL;
	tuple_unref(it->key);
	it->key = NULL;
	if (it->tx == &it->tx_autocommit) {
		/*
		 * Rollback the automatic transaction.
		 * Use vy_tx_destroy() so as not to spoil
		 * the statistics of rollbacks issued by
		 * user transactions.
		 */
		vy_tx_destroy(it->tx);
	} else {
		trigger_clear(&it->on_tx_destroy);
	}
	it->tx = NULL;
	it->base.next = vinyl_iterator_last;
}

static int
vinyl_iterator_primary_next(struct iterator *base, struct tuple **ret)
{
	assert(base->next = vinyl_iterator_primary_next);
	struct vinyl_iterator *it = (struct vinyl_iterator *)base;
	assert(it->lsm->index_id == 0);
	struct tuple *tuple;

	if (it->tx == NULL) {
		diag_set(ClientError, ER_CURSOR_NO_TRANSACTION);
		goto fail;
	}
	if (it->tx->state == VINYL_TX_ABORT || it->tx->read_view->is_aborted) {
		diag_set(ClientError, ER_READ_VIEW_ABORTED);
		goto fail;
	}

	if (vy_read_iterator_next(&it->iterator, &tuple) != 0)
		goto fail;

	if (tuple == NULL) {
		/* EOF. Close the iterator immediately. */
		vinyl_iterator_close(it);
		*ret = NULL;
		return 0;
	}
	*ret = tuple_bless(tuple);
	if (*ret != NULL)
		return 0;
fail:
	vinyl_iterator_close(it);
	return -1;
}

static int
vinyl_iterator_secondary_next(struct iterator *base, struct tuple **ret)
{
	assert(base->next = vinyl_iterator_secondary_next);
	struct vinyl_iterator *it = (struct vinyl_iterator *)base;
	assert(it->lsm->index_id > 0);
	struct tuple *tuple;

	if (it->tx == NULL) {
		diag_set(ClientError, ER_CURSOR_NO_TRANSACTION);
		goto fail;
	}
	if (it->tx->state == VINYL_TX_ABORT || it->tx->read_view->is_aborted) {
		diag_set(ClientError, ER_READ_VIEW_ABORTED);
		goto fail;
	}


	if (vy_read_iterator_next(&it->iterator, &tuple) != 0)
		goto fail;

	if (tuple == NULL) {
		/* EOF. Close the iterator immediately. */
		vinyl_iterator_close(it);
		*ret = NULL;
		return 0;
	}
#ifndef NDEBUG
	struct errinj *delay = errinj(ERRINJ_VY_DELAY_PK_LOOKUP,
				      ERRINJ_BOOL);
	if (delay && delay->bparam) {
		while (delay->bparam)
			fiber_sleep(0.01);
	}
#endif
	/*
	 * Get the full tuple from the primary index.
	 * Note, there's no need in vy_tx_track() as the
	 * tuple is already tracked in the secondary index.
	 */
	if (vy_point_lookup(it->lsm->pk, it->tx, vy_tx_read_view(it->tx),
			    tuple, &tuple) != 0)
		goto fail;
	*ret = tuple_bless(tuple);
	tuple_unref(tuple);
	if (*ret != NULL)
		return 0;
fail:
	vinyl_iterator_close(it);
	return -1;
}

static void
vinyl_iterator_free(struct iterator *base)
{
	assert(base->free == vinyl_iterator_free);
	struct vinyl_iterator *it = (struct vinyl_iterator *)base;
	if (base->next != vinyl_iterator_last)
		vinyl_iterator_close(it);
	mempool_free(&it->env->iterator_pool, it);
}

static struct iterator *
vinyl_index_create_iterator(struct index *base, enum iterator_type type,
			    const char *key, uint32_t part_count)
{
	struct vy_lsm *lsm = vy_lsm(base);
	struct vy_env *env = vy_env(base->engine);

	if (type > ITER_GT) {
		diag_set(UnsupportedIndexFeature, base->def,
			 "requested iterator type");
		return NULL;
	}

	struct vinyl_iterator *it = mempool_alloc(&env->iterator_pool);
	if (it == NULL) {
	        diag_set(OutOfMemory, sizeof(struct vinyl_iterator),
			 "mempool", "struct vinyl_iterator");
		return NULL;
	}
	it->key = vy_stmt_new_select(lsm->env->key_format, key, part_count);
	if (it->key == NULL) {
		mempool_free(&env->iterator_pool, it);
		return NULL;
	}

	iterator_create(&it->base, base);
	if (lsm->index_id == 0)
		it->base.next = vinyl_iterator_primary_next;
	else
		it->base.next = vinyl_iterator_secondary_next;
	it->base.free = vinyl_iterator_free;

	it->env = env;
	it->lsm = lsm;
	vy_lsm_ref(lsm);

	struct vy_tx *tx = in_txn() ? in_txn()->engine_tx : NULL;
	assert(tx == NULL || tx->state == VINYL_TX_READY);
	if (tx != NULL) {
		/*
		 * Register a trigger that will abort this iterator
		 * when the transaction ends.
		 */
		trigger_create(&it->on_tx_destroy,
			       vinyl_iterator_on_tx_destroy, NULL, NULL);
		trigger_add(&tx->on_destroy, &it->on_tx_destroy);
	} else {
		tx = &it->tx_autocommit;
		vy_tx_create(env->xm, tx);
	}
	it->tx = tx;

	vy_read_iterator_open(&it->iterator, lsm, tx, type, it->key,
			      (const struct vy_read_view **)&tx->read_view);
	return (struct iterator *)it;
}

static int
vinyl_index_get(struct index *index, const char *key,
		uint32_t part_count, struct tuple **ret)
{
	assert(index->def->opts.is_unique);
	assert(index->def->key_def->part_count == part_count);

	struct vy_lsm *lsm = vy_lsm(index);
	struct vy_env *env = vy_env(index->engine);
	struct vy_tx *tx = in_txn() ? in_txn()->engine_tx : NULL;
	const struct vy_read_view **rv = (tx != NULL ? vy_tx_read_view(tx) :
					  &env->xm->p_global_read_view);

	struct tuple *tuple;
	if (vy_lsm_full_by_key(lsm, tx, rv, key, part_count, &tuple) != 0)
		return -1;

	if (tuple != NULL) {
		*ret = tuple_bless(tuple);
		tuple_unref(tuple);
		return *ret == NULL ? -1 : 0;
	}
	*ret = NULL;
	return 0;
}

/*** }}} Cursor */

static const struct engine_vtab vinyl_engine_vtab = {
	/* .shutdown = */ vinyl_engine_shutdown,
	/* .create_space = */ vinyl_engine_create_space,
	/* .join = */ vinyl_engine_join,
	/* .begin = */ vinyl_engine_begin,
	/* .begin_statement = */ vinyl_engine_begin_statement,
	/* .prepare = */ vinyl_engine_prepare,
	/* .commit = */ vinyl_engine_commit,
	/* .rollback_statement = */ vinyl_engine_rollback_statement,
	/* .rollback = */ vinyl_engine_rollback,
	/* .bootstrap = */ vinyl_engine_bootstrap,
	/* .begin_initial_recovery = */ vinyl_engine_begin_initial_recovery,
	/* .begin_final_recovery = */ vinyl_engine_begin_final_recovery,
	/* .end_recovery = */ vinyl_engine_end_recovery,
	/* .begin_checkpoint = */ vinyl_engine_begin_checkpoint,
	/* .wait_checkpoint = */ vinyl_engine_wait_checkpoint,
	/* .commit_checkpoint = */ vinyl_engine_commit_checkpoint,
	/* .abort_checkpoint = */ vinyl_engine_abort_checkpoint,
	/* .collect_garbage = */ vinyl_engine_collect_garbage,
	/* .backup = */ vinyl_engine_backup,
	/* .memory_stat = */ vinyl_engine_memory_stat,
	/* .reset_stat = */ vinyl_engine_reset_stat,
	/* .check_space_def = */ vinyl_engine_check_space_def,
};

static const struct space_vtab vinyl_space_vtab = {
	/* .destroy = */ vinyl_space_destroy,
	/* .bsize = */ vinyl_space_bsize,
	/* .apply_initial_join_row = */ vinyl_space_apply_initial_join_row,
	/* .execute_replace = */ vinyl_space_execute_replace,
	/* .execute_delete = */ vinyl_space_execute_delete,
	/* .execute_update = */ vinyl_space_execute_update,
	/* .execute_upsert = */ vinyl_space_execute_upsert,
	/* .ephemeral_replace = */ vinyl_space_ephemeral_replace,
	/* .ephemeral_delete = */ vinyl_space_ephemeral_delete,
	/* .ephemeral_cleanup = */ vinyl_space_ephemeral_cleanup,
	/* .init_system_space = */ vinyl_init_system_space,
	/* .init_ephemeral_space = */ vinyl_init_ephemeral_space,
	/* .check_index_def = */ vinyl_space_check_index_def,
	/* .create_index = */ vinyl_space_create_index,
	/* .add_primary_key = */ vinyl_space_add_primary_key,
	/* .drop_primary_key = */ vinyl_space_drop_primary_key,
	/* .check_format = */ vinyl_space_check_format,
	/* .build_index = */ vinyl_space_build_index,
	/* .swap_index = */ vinyl_space_swap_index,
	/* .prepare_alter = */ vinyl_space_prepare_alter,
};

static const struct index_vtab vinyl_index_vtab = {
	/* .destroy = */ vinyl_index_destroy,
	/* .commit_create = */ vinyl_index_commit_create,
	/* .abort_create = */ generic_index_abort_create,
	/* .commit_modify = */ vinyl_index_commit_modify,
	/* .commit_drop = */ vinyl_index_commit_drop,
	/* .update_def = */ generic_index_update_def,
	/* .depends_on_pk = */ vinyl_index_depends_on_pk,
	/* .def_change_requires_rebuild = */
		vinyl_index_def_change_requires_rebuild,
	/* .size = */ vinyl_index_size,
	/* .bsize = */ vinyl_index_bsize,
	/* .min = */ generic_index_min,
	/* .max = */ generic_index_max,
	/* .random = */ generic_index_random,
	/* .count = */ generic_index_count,
	/* .get = */ vinyl_index_get,
	/* .replace = */ generic_index_replace,
	/* .create_iterator = */ vinyl_index_create_iterator,
	/* .create_snapshot_iterator = */
		generic_index_create_snapshot_iterator,
	/* .info = */ vinyl_index_info,
	/* .reset_stat = */ vinyl_index_reset_stat,
	/* .begin_build = */ generic_index_begin_build,
	/* .reserve = */ generic_index_reserve,
	/* .build_next = */ generic_index_build_next,
	/* .end_build = */ generic_index_end_build,
};
