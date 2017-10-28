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
#include "vy_index.h"
#include "vy_tx.h"
#include "vy_cache.h"
#include "vy_log.h"
#include "vy_upsert.h"
#include "vy_write_iterator.h"
#include "vy_read_iterator.h"
#include "vy_quota.h"
#include "vy_stat.h"

#include <small/lsregion.h>
#include <coio_file.h>

#include "coio_task.h"
#include "cbus.h"
#include "histogram.h"

#include "tuple_update.h"
#include "txn.h"
#include "xrow.h"
#include "xlog.h"
#include "space.h"
#include "xstream.h"
#include "info.h"
#include "column_mask.h"
#include "trigger.h"
#include "checkpoint.h"

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

struct vy_env {
	/** Recovery status */
	enum vy_status status;
	/** TX manager */
	struct tx_manager   *xm;
	/** Scheduler */
	struct vy_scheduler *scheduler;
	/** Statistics */
	struct vy_stat      *stat;
	/** Upsert squash queue */
	struct vy_squash_queue *squash_queue;
	/** Mempool for struct vy_cursor */
	struct mempool      cursor_pool;
	/** Memory quota */
	struct vy_quota     quota;
	/** Timer for updating quota watermark. */
	ev_timer            quota_timer;
	/** Common index environment. */
	struct vy_index_env index_env;
	/** Environment for cache subsystem */
	struct vy_cache_env cache_env;
	/** Environment for run subsystem */
	struct vy_run_env run_env;
	/** Environment for statements subsystem. */
	struct vy_stmt_env stmt_env;
	/** Local recovery context. */
	struct vy_recovery *recovery;
	/** Local recovery vclock. */
	const struct vclock *recovery_vclock;
	/** Path to the data directory. */
	char *path;
	/** Max size of the memory level. */
	size_t memory;
	/** Max size of the tuple cache. */
	/** Max time a transaction may wait for memory. */
	double timeout;
	/** Max number of threads used for reading. */
	int read_threads;
	/** Max number of threads used for writing. */
	int write_threads;
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

enum vy_stat_name {
	VY_STAT_GET,
	VY_STAT_TX,
	VY_STAT_TX_OPS,
	VY_STAT_TX_WRITE,
	VY_STAT_CURSOR,
	VY_STAT_CURSOR_OPS,
	VY_STAT_LAST,
};

static const char *vy_stat_strings[] = {
	"get",
	"tx",
	"tx_ops",
	"tx_write",
	"cursor",
	"cursor_ops",
};

struct vy_stat {
	struct rmean *rmean;
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

/** Cursor. */
struct vy_cursor {
	/**
	 * A built-in transaction created when a cursor is open
	 * in autocommit mode.
	 */
	struct vy_tx tx_autocommit;
	struct vy_index *index;
	struct tuple *key;
	/**
	 * Points either to tx_autocommit for autocommit mode or
	 * to a multi-statement transaction active when the cursor
	 * was created.
	 */
	struct vy_tx *tx;
	/** The number of vy_cursor_next() invocations. */
	int n_reads;
	/** Cursor creation time, used for statistics. */
	ev_tstamp start;
	/** Trigger invoked when tx ends to close the cursor. */
	struct trigger on_tx_destroy;
	/** Iterator over index */
	struct vy_read_iterator iterator;
};

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
	vy_log_prepare_run(index->commit_lsn, run->id);
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

#define HEAP_NAME vy_dump_heap

static bool
vy_dump_heap_less(struct heap_node *a, struct heap_node *b)
{
	struct vy_index *i1 = container_of(a, struct vy_index, in_dump);
	struct vy_index *i2 = container_of(b, struct vy_index, in_dump);

	/*
	 * Indexes that are currently being dumped or can't be scheduled
	 * for dump right now are moved off the top of the heap.
	 */
	if (i1->is_dumping != i2->is_dumping)
		return i1->is_dumping < i2->is_dumping;
	if (i1->pin_count != i2->pin_count)
		return i1->pin_count < i2->pin_count;

	/* Older indexes are dumped first. */
	int64_t i1_generation = vy_index_generation(i1);
	int64_t i2_generation = vy_index_generation(i2);
	if (i1_generation != i2_generation)
		return i1_generation < i2_generation;
	/*
	 * If a space has more than one index, appending a statement
	 * to it requires reading the primary index to get the old
	 * tuple and delete it from secondary indexes. This means that
	 * on local recovery from WAL, the primary index must not be
	 * ahead of secondary indexes of the same space, i.e. it must
	 * be dumped last.
	 */
	return i1->id > i2->id;
}

#define HEAP_LESS(h, l, r) vy_dump_heap_less(l, r)

#include "salad/heap.h"

#undef HEAP_LESS
#undef HEAP_NAME

#define HEAP_NAME vy_compact_heap

static bool
vy_compact_heap_less(struct heap_node *a, struct heap_node *b)
{
	struct vy_index *i1 = container_of(a, struct vy_index, in_compact);
	struct vy_index *i2 = container_of(b, struct vy_index, in_compact);
	/*
	 * Prefer indexes whose read amplification will be reduced
	 * most as a result of compaction.
	 */
	return vy_index_compact_priority(i1) > vy_index_compact_priority(i2);
}

#define HEAP_LESS(h, l, r) vy_compact_heap_less(l, r)

#include "salad/heap.h"

#undef HEAP_LESS
#undef HEAP_NAME

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
	 * scheduler. Scheduler is a fiber in TX, so ev_async + fiber_channel
	 * are used here instead of pthread_cond_t.
	 */
	struct ev_async scheduler_async;
	struct fiber_cond scheduler_cond;
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
	 * Generation of in-memory data currently being dumped.
	 *
	 * If @dump_generation < @generation, the scheduler is dumping
	 * in-memory trees created at @dump_generation. When all such
	 * trees have been dumped, it bumps @dump_generation and frees
	 * memory.
	 *
	 * If @dump_generation == @generation, dump have been completed
	 * and the scheduler won't schedule a dump task until @generation
	 * is bumped, which may happen either on exceeding the memory
	 * quota or on checkpoint.
	 *
	 * Throughout the code, a process of dumping all in-memory trees
	 * at @dump_generation is called 'dump round'.
	 */
	int64_t dump_generation;
	/** Number of dump tasks that are currently in progress. */
	int dump_task_count;
	/** Time when the current dump round started. */
	ev_tstamp dump_start;
	/** Signaled on dump round completion. */
	struct fiber_cond dump_cond;
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
vy_scheduler_trigger_dump(struct vy_scheduler *);
static void
vy_scheduler_complete_dump(struct vy_scheduler *);

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
	int (*complete)(struct vy_scheduler *scheduler, struct vy_task *task);
	/**
	 * This function is called by the scheduler if either ->execute
	 * or ->complete failed. It may be used to undo changes done to
	 * the index when preparing the task.
	 *
	 * If @in_shutdown is set, the callback is invoked from the
	 * engine destructor.
	 */
	void (*abort)(struct vy_scheduler *scheduler, struct vy_task *task,
		      bool in_shutdown);
};

struct vy_task {
	const struct vy_task_ops *ops;
	/** Return code of ->execute. */
	int status;
	/** If ->execute fails, the error is stored here. */
	struct diag diag;
	/** Index this task is for. */
	struct vy_index *index;
	/** Range to compact. */
	struct vy_range *range;
	/** Run written by this task. */
	struct vy_run *new_run;
	/** Write iterator producing statements for the new run. */
	struct vy_stmt_stream *wi;
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

	return vy_run_write(task->new_run, index->env->path,
			    index->space_id, index->id, task->wi,
			    task->page_size, index->cmp_def,
			    index->key_def, task->max_output_count,
			    task->bloom_fpr);
}

static int
vy_task_dump_complete(struct vy_scheduler *scheduler, struct vy_task *task)
{
	struct vy_index *index = task->index;
	struct vy_run *new_run = task->new_run;
	int64_t dump_lsn = new_run->dump_lsn;
	struct tuple_format *key_format = index->env->key_format;
	struct vy_mem *mem, *next_mem;
	struct vy_slice **new_slices, *slice;
	struct vy_range *range, *begin_range, *end_range;
	struct tuple *min_key, *max_key;
	int i, loops = 0;

	assert(index->is_dumping);

	if (vy_run_is_empty(new_run)) {
		/*
		 * In case the run is empty, we can discard the run
		 * and delete dumped in-memory trees right away w/o
		 * inserting slices into ranges. However, we need
		 * to log index dump anyway.
		 */
		vy_log_tx_begin();
		vy_log_dump_index(index->commit_lsn, dump_lsn);
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
				     range->begin, range->end, index->cmp_def);
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
	vy_log_create_run(index->commit_lsn, new_run->id, dump_lsn);
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
	vy_log_dump_index(index->commit_lsn, dump_lsn);
	if (vy_log_tx_commit() < 0)
		goto fail_free_slices;

	/*
	 * Account the new run.
	 */
	vy_index_add_run(index, new_run);
	vy_stmt_counter_add_disk(&index->stat.disk.dump.out, &new_run->count);

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
		vy_range_update_compact_priority(range, &index->opts);
		if (!vy_range_is_scheduled(range))
			vy_range_heap_update(&index->range_heap,
					     &range->heap_node);
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
		if (mem->generation > scheduler->dump_generation)
			continue;
		vy_stmt_counter_add(&index->stat.disk.dump.in, &mem->count);
		vy_index_delete_mem(index, mem);
	}
	index->dump_lsn = dump_lsn;
	index->stat.disk.dump.count++;

	/* The iterator has been cleaned up in a worker thread. */
	task->wi->iface->close(task->wi);

	index->is_dumping = false;
	vy_scheduler_update_index(scheduler, index);

	if (index->id != 0)
		vy_scheduler_unpin_index(scheduler, index->pk);

	assert(scheduler->dump_task_count > 0);
	scheduler->dump_task_count--;

	vy_scheduler_complete_dump(scheduler);

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
vy_task_dump_abort(struct vy_scheduler *scheduler, struct vy_task *task,
		   bool in_shutdown)
{
	struct vy_index *index = task->index;

	assert(index->is_dumping);

	/* The iterator has been cleaned up in a worker thread. */
	task->wi->iface->close(task->wi);

	/*
	 * It's no use alerting the user if the server is
	 * shutting down or the index was dropped.
	 */
	if (!in_shutdown && !index->is_dropped) {
		say_error("%s: dump failed: %s", vy_index_name(index),
			  diag_last_error(&task->diag)->errmsg);
	}

	/* The metadata log is unavailable on shutdown. */
	if (!in_shutdown)
		vy_run_discard(task->new_run);
	else
		vy_run_unref(task->new_run);

	index->is_dumping = false;
	vy_scheduler_update_index(scheduler, index);

	if (index->id != 0)
		vy_scheduler_unpin_index(scheduler, index->pk);

	assert(scheduler->dump_task_count > 0);
	scheduler->dump_task_count--;
}

/**
 * Create a task to dump an index.
 *
 * On success the task is supposed to dump all in-memory
 * trees created at @scheduler->dump_generation.
 */
static int
vy_task_dump_new(struct vy_scheduler *scheduler, struct vy_index *index,
		 struct vy_task **p_task)
{
	static struct vy_task_ops dump_ops = {
		.execute = vy_task_dump_execute,
		.complete = vy_task_dump_complete,
		.abort = vy_task_dump_abort,
	};

	struct tx_manager *xm = scheduler->env->xm;

	assert(!index->is_dropped);
	assert(!index->is_dumping);
	assert(index->pin_count == 0);
	assert(vy_index_generation(index) == scheduler->dump_generation);
	assert(scheduler->dump_generation < scheduler->generation);

	struct errinj *inj = errinj(ERRINJ_VY_INDEX_DUMP, ERRINJ_INT);
	if (inj != NULL && inj->iparam == (int)index->id) {
		diag_set(ClientError, ER_INJECTION, "vinyl index dump");
		goto err;
	}

	/* Rotate the active tree if it needs to be dumped. */
	if (index->mem->generation == scheduler->dump_generation &&
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
		if (mem->generation > scheduler->dump_generation)
			continue;
		vy_mem_wait_pinned(mem);
		if (mem->tree.size == 0) {
			/*
			 * The tree is empty so we can delete it
			 * right away, without involving a worker.
			 */
			vy_index_delete_mem(index, mem);
			continue;
		}
		dump_lsn = MAX(dump_lsn, mem->max_lsn);
		max_output_count += mem->tree.size;
	}

	if (max_output_count == 0) {
		/* Nothing to do, pick another index. */
		vy_scheduler_update_index(scheduler, index);
		vy_scheduler_complete_dump(scheduler);
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
	wi = vy_write_iterator_new(index->cmp_def, index->disk_format,
				   index->upsert_format, index->id == 0,
				   is_last_level, &xm->read_views);
	if (wi == NULL)
		goto err_wi;
	rlist_foreach_entry(mem, &index->sealed, in_sealed) {
		if (mem->generation > scheduler->dump_generation)
			continue;
		if (vy_write_iterator_new_mem(wi, mem) != 0)
			goto err_wi_sub;
	}

	task->new_run = new_run;
	task->wi = wi;
	task->max_output_count = max_output_count;
	task->bloom_fpr = index->opts.bloom_fpr;
	task->page_size = index->opts.page_size;

	index->is_dumping = true;
	vy_scheduler_update_index(scheduler, index);

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

	scheduler->dump_task_count++;

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

	return vy_run_write(task->new_run, index->env->path,
			    index->space_id, index->id, task->wi,
			    task->page_size, index->cmp_def,
			    index->key_def, task->max_output_count,
			    task->bloom_fpr);
}

static int
vy_task_compact_complete(struct vy_scheduler *scheduler, struct vy_task *task)
{
	struct vy_index *index = task->index;
	struct vy_range *range = task->range;
	struct vy_run *new_run = task->new_run;
	struct vy_slice *first_slice = task->first_slice;
	struct vy_slice *last_slice = task->last_slice;
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
					 index->cmp_def);
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
	int64_t gc_lsn = checkpoint_last(NULL);
	rlist_foreach_entry(run, &unused_runs, in_unused)
		vy_log_drop_run(run->id, gc_lsn);
	if (new_slice != NULL) {
		vy_log_create_run(index->commit_lsn, new_run->id,
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
		vy_stmt_counter_add_disk(&index->stat.disk.compact.out,
					 &new_run->count);
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
		vy_stmt_counter_add_disk(&index->stat.disk.compact.in,
					 &slice->count);
		if (slice == last_slice)
			break;
	}
	range->n_compactions++;
	range->version++;
	vy_index_acct_range(index, range);
	vy_range_update_compact_priority(range, &index->opts);
	index->stat.disk.compact.count++;

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

	assert(range->heap_node.pos == UINT32_MAX);
	vy_range_heap_insert(&index->range_heap, &range->heap_node);
	vy_scheduler_update_index(scheduler, index);

	say_info("%s: completed compacting range %s",
		 vy_index_name(index), vy_range_str(range));
	return 0;
}

static void
vy_task_compact_abort(struct vy_scheduler *scheduler, struct vy_task *task,
		      bool in_shutdown)
{
	struct vy_index *index = task->index;
	struct vy_range *range = task->range;

	/* The iterator has been cleaned up in worker. */
	task->wi->iface->close(task->wi);

	/*
	 * It's no use alerting the user if the server is
	 * shutting down or the index was dropped.
	 */
	if (!in_shutdown && !index->is_dropped) {
		say_error("%s: failed to compact range %s: %s",
			  vy_index_name(index), vy_range_str(range),
			  diag_last_error(&task->diag)->errmsg);
	}

	/* The metadata log is unavailable on shutdown. */
	if (!in_shutdown)
		vy_run_discard(task->new_run);
	else
		vy_run_unref(task->new_run);

	assert(range->heap_node.pos == UINT32_MAX);
	vy_range_heap_insert(&index->range_heap, &range->heap_node);
	vy_scheduler_update_index(scheduler, index);
}

static int
vy_task_compact_new(struct vy_scheduler *scheduler, struct vy_index *index,
		    struct vy_task **p_task)
{
	static struct vy_task_ops compact_ops = {
		.execute = vy_task_compact_execute,
		.complete = vy_task_compact_complete,
		.abort = vy_task_compact_abort,
	};

	struct tx_manager *xm = scheduler->env->xm;
	struct heap_node *range_node;
	struct vy_range *range;

	assert(!index->is_dropped);

	range_node = vy_range_heap_top(&index->range_heap);
	assert(range_node != NULL);
	range = container_of(range_node, struct vy_range, heap_node);
	assert(range->compact_priority > 1);

	if (vy_index_split_range(index, range) ||
	    vy_index_coalesce_range(index, range)) {
		vy_scheduler_update_index(scheduler, index);
		return 0;
	}

	struct vy_task *task = vy_task_new(&scheduler->task_pool,
					   index, &compact_ops);
	if (task == NULL)
		goto err_task;

	struct vy_run *new_run = vy_run_prepare(index);
	if (new_run == NULL)
		goto err_run;

	struct vy_stmt_stream *wi;
	bool is_last_level = (range->compact_priority == range->slice_count);
	wi = vy_write_iterator_new(index->cmp_def, index->disk_format,
				   index->upsert_format, index->id == 0,
				   is_last_level, &xm->read_views);
	if (wi == NULL)
		goto err_wi;

	struct vy_slice *slice;
	int n = range->compact_priority;
	rlist_foreach_entry(slice, &range->slices, in_range) {
		if (vy_write_iterator_new_slice(wi, slice,
						&scheduler->env->run_env) != 0)
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

	/*
	 * Remove the range we are going to compact from the heap
	 * so that it doesn't get selected again.
	 */
	vy_range_heap_delete(&index->range_heap, range_node);
	range_node->pos = UINT32_MAX;
	vy_scheduler_update_index(scheduler, index);

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
vy_scheduler_async_cb(ev_loop *loop, struct ev_async *watcher, int events)
{
	(void) loop;
	(void) events;
	struct vy_scheduler *scheduler =
		container_of(watcher, struct vy_scheduler, scheduler_async);
	fiber_cond_signal(&scheduler->scheduler_cond);
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
	fiber_cond_create(&scheduler->dump_cond);
	scheduler->env = env;
	vy_compact_heap_create(&scheduler->compact_heap);
	vy_dump_heap_create(&scheduler->dump_heap);
	tt_pthread_cond_init(&scheduler->worker_cond, NULL);
	scheduler->loop = loop();
	ev_async_init(&scheduler->scheduler_async, vy_scheduler_async_cb);
	fiber_cond_create(&scheduler->scheduler_cond);
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
	fiber_cond_signal(&scheduler->scheduler_cond);

	if (scheduler->is_worker_pool_running)
		vy_scheduler_stop_workers(scheduler);

	mempool_destroy(&scheduler->task_pool);
	diag_destroy(&scheduler->diag);
	vy_compact_heap_destroy(&scheduler->compact_heap);
	vy_dump_heap_destroy(&scheduler->dump_heap);
	tt_pthread_cond_destroy(&scheduler->worker_cond);
	TRASH(&scheduler->scheduler_async);
	fiber_cond_destroy(&scheduler->scheduler_cond);
	tt_pthread_mutex_destroy(&scheduler->mutex);
	free(scheduler);
}

static void
vy_scheduler_add_index(struct vy_scheduler *scheduler,
		       struct vy_index *index)
{
	assert(index->in_dump.pos == UINT32_MAX);
	assert(index->in_compact.pos == UINT32_MAX);
	vy_dump_heap_insert(&scheduler->dump_heap, &index->in_dump);
	vy_compact_heap_insert(&scheduler->compact_heap, &index->in_compact);
}

static void
vy_scheduler_update_index(struct vy_scheduler *scheduler,
			  struct vy_index *index)
{
	if (index->is_dropped) {
		/* Dropped indexes are exempted from scheduling. */
		assert(index->in_dump.pos == UINT32_MAX);
		assert(index->in_compact.pos == UINT32_MAX);
		return;
	}
	assert(index->in_dump.pos != UINT32_MAX);
	assert(index->in_compact.pos != UINT32_MAX);
	vy_dump_heap_update(&scheduler->dump_heap, &index->in_dump);
	vy_compact_heap_update(&scheduler->compact_heap, &index->in_compact);
}

static void
vy_scheduler_remove_index(struct vy_scheduler *scheduler,
			  struct vy_index *index)
{
	assert(index->in_dump.pos != UINT32_MAX);
	assert(index->in_compact.pos != UINT32_MAX);
	vy_dump_heap_delete(&scheduler->dump_heap, &index->in_dump);
	vy_compact_heap_delete(&scheduler->compact_heap, &index->in_compact);
	index->in_dump.pos = UINT32_MAX;
	index->in_compact.pos = UINT32_MAX;
}

static void
vy_scheduler_pin_index(struct vy_scheduler *scheduler, struct vy_index *index)
{
	assert(!index->is_dumping);
	if (index->pin_count++ == 0)
		vy_scheduler_update_index(scheduler, index);
}

static void
vy_scheduler_unpin_index(struct vy_scheduler *scheduler, struct vy_index *index)
{
	assert(!index->is_dumping);
	assert(index->pin_count > 0);
	if (--index->pin_count == 0)
		vy_scheduler_update_index(scheduler, index);
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
	bool dump_in_progress = (scheduler->dump_generation <
				 scheduler->generation);
	/*
	 * Look up the oldest index eligible for dump.
	 */
	struct heap_node *pn = vy_dump_heap_top(&scheduler->dump_heap);
	if (pn == NULL) {
		/*
		 * There is no vinyl index and so no task to schedule.
		 * If a dump round is in progress, complete it.
		 */
		if (dump_in_progress)
			vy_scheduler_complete_dump(scheduler);
		return 0;
	}
	struct vy_index *index = container_of(pn, struct vy_index, in_dump);
	if (index->is_dumping || index->pin_count > 0) {
		/* All eligible indexes are already being dumped. */
		return 0;
	}
	if (dump_in_progress &&
	    vy_index_generation(index) == scheduler->dump_generation) {
		/*
		 * Dump is in progress and there is an index that
		 * contains data that must be dumped at the current
		 * round. Try to create a task for it.
		 */
		if (vy_task_dump_new(scheduler, index, ptask) != 0)
			return -1;
		if (*ptask != NULL)
			return 0; /* new task */
		/*
		 * All in-memory trees eligible for dump were empty
		 * and so were deleted without involving a worker
		 * thread. Check another index.
		 */
		goto retry;
	}
	if (dump_in_progress) {
		/*
		 * Dump is in progress, but there is no index eligible
		 * for dump in the heap. Wait until the current round
		 * is complete.
		 */
		assert(scheduler->dump_task_count > 0);
		return 0;
	}
	/*
	 * Nothing being dumped right now. Consider triggering
	 * dump if memory quota is exceeded.
	 */
	if (scheduler->checkpoint_in_progress) {
		/*
		 * Do not trigger another dump until checkpoint
		 * is complete so as to make sure no statements
		 * inserted after WAL rotation are written to
		 * the snapshot.
		 */
		return 0;
	}
	if (!vy_quota_is_exceeded(&scheduler->env->quota)) {
		/*
		 * Memory consumption is below the watermark,
		 * nothing to do.
		 */
		return 0;
	}
	if (lsregion_used(&scheduler->env->stmt_env.allocator) == 0) {
		/*
		 * Quota must be exceeded by a pending transaction,
		 * there's nothing we can do about that.
		 */
		return 0;
	}
	vy_scheduler_trigger_dump(scheduler);
	goto retry;
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
	struct vy_index *index = container_of(pn, struct vy_index, in_compact);
	if (vy_index_compact_priority(index) <= 1)
		return 0; /* nothing to do */
	if (vy_task_compact_new(scheduler, index, ptask) != 0)
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
			task->ops->abort(scheduler, task, false);
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
	    task->ops->complete(scheduler, task) != 0) {
		assert(!diag_is_empty(diag_get()));
		diag_move(diag_get(), diag);
		goto fail;
	}
	return 0;
fail:
	if (task->ops->abort)
		task->ops->abort(scheduler, task, false);
	diag_move(diag, &scheduler->diag);
	return -1;
}

static int
vy_scheduler_f(va_list va)
{
	struct vy_scheduler *scheduler = va_arg(va, struct vy_scheduler *);
	MAYBE_UNUSED struct vy_env *env = scheduler->env;

	/*
	 * Yield immediately, until the quota watermark is reached
	 * for the first time or a checkpoint is made.
	 * Then start the worker threads: we know they will be
	 * needed. If quota watermark is never reached, workers
	 * are not started and the scheduler is idle until
	 * shutdown or checkpoint.
	 */
	fiber_cond_wait(&scheduler->scheduler_cond);
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
		fiber_cond_signal(&scheduler->dump_cond);
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
		fiber_cond_wait(&scheduler->scheduler_cond);
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
	scheduler->worker_pool_size = scheduler->env->write_threads;
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
			task->ops->abort(scheduler, task, true);
		vy_task_delete(&scheduler->task_pool, task);
	}
}

/** Trigger dump of all currently existing in-memory trees. */
static void
vy_scheduler_trigger_dump(struct vy_scheduler *scheduler)
{
	assert(scheduler->dump_generation <= scheduler->generation);
	if (scheduler->generation == scheduler->dump_generation) {
		/*
		 * We are about to start a new dump round.
		 * Remember the current time so that we can update
		 * dump bandwidth when the dump round is complete
		 * (see vy_scheduler_complete_dump()).
		 */
		scheduler->dump_start = ev_monotonic_now(loop());
	}
	scheduler->generation++;
}

/**
 * Check whether the current dump round is complete.
 * If it is, free memory and proceed to the next dump round.
 */
static void
vy_scheduler_complete_dump(struct vy_scheduler *scheduler)
{
	assert(scheduler->dump_generation < scheduler->generation);

	if (scheduler->dump_task_count > 0) {
		/*
		 * There are still dump tasks in progress,
		 * the dump round can't be over yet.
		 */
		return;
	}

	int64_t min_generation = scheduler->generation;
	struct heap_node *pn = vy_dump_heap_top(&scheduler->dump_heap);
	if (pn != NULL) {
		struct vy_index *index;
		index = container_of(pn, struct vy_index, in_dump);
		min_generation = vy_index_generation(index);
	}
	if (min_generation == scheduler->dump_generation) {
		/*
		 * There are still indexes that must be dumped
		 * during the current dump round.
		 */
		return;
	}

	/*
	 * The oldest index data is newer than @dump_generation,
	 * so the current dump round has been finished.
	 * Free memory, release quota, and signal dump completion.
	 */
	struct lsregion *allocator = &scheduler->env->stmt_env.allocator;
	struct vy_quota *quota = &scheduler->env->quota;
	size_t mem_used_before = lsregion_used(allocator);
	lsregion_gc(allocator, min_generation - 1);
	size_t mem_used_after = lsregion_used(allocator);
	assert(mem_used_after <= mem_used_before);
	size_t mem_dumped = mem_used_before - mem_used_after;
	vy_quota_release(quota, mem_dumped);

	scheduler->dump_generation = min_generation;
	fiber_cond_signal(&scheduler->dump_cond);

	/* Account dump bandwidth. */
	struct vy_stat *stat = scheduler->env->stat;
	ev_tstamp now = ev_monotonic_now(loop());
	ev_tstamp dump_duration = now - scheduler->dump_start;
	if (dump_duration > 0)
		histogram_collect(stat->dump_bw, mem_dumped / dump_duration);
	scheduler->dump_start = now;
}

/**
 * Schedule checkpoint. Please call vy_scheduler_wait_checkpoint()
 * after that.
 */
static int
vy_scheduler_begin_checkpoint(struct vy_scheduler *scheduler)
{
	assert(!scheduler->checkpoint_in_progress);

	/*
	 * The scheduler starts worker threads upon the first wakeup.
	 * To avoid starting the threads for nothing, do not wake it
	 * up if Vinyl is not used.
	 */
	if (lsregion_used(&scheduler->env->stmt_env.allocator) == 0)
		return 0;

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

	vy_scheduler_trigger_dump(scheduler);
	scheduler->checkpoint_in_progress = true;
	fiber_cond_signal(&scheduler->scheduler_cond);
	return 0;
}

/**
 * Wait for checkpoint. Please call vy_scheduler_end_checkpoint()
 * after that.
 */
static int
vy_scheduler_wait_checkpoint(struct vy_scheduler *scheduler)
{
	if (!scheduler->checkpoint_in_progress)
		return 0;

	/*
	 * Wait until all in-memory trees created before
	 * checkpoint started have been dumped.
	 */
	while (scheduler->dump_generation < scheduler->generation) {
		if (scheduler->is_throttled) {
			/* A dump error occurred, abort checkpoint. */
			assert(!diag_is_empty(&scheduler->diag));
			diag_add_error(diag_get(),
				       diag_last_error(&scheduler->diag));
			say_error("vinyl checkpoint error: %s",
				  diag_last_error(diag_get())->errmsg);
			return -1;
		}
		fiber_cond_wait(&scheduler->dump_cond);
	}
	say_info("vinyl checkpoint done");
	return 0;
}

/**
 * End checkpoint. Called on both checkpoint commit and abort.
 */
static void
vy_scheduler_end_checkpoint(struct vy_scheduler *scheduler)
{
	if (!scheduler->checkpoint_in_progress)
		return;

	/*
	 * Checkpoint blocks dumping of in-memory trees created after
	 * checkpoint started, so wake up the scheduler after we are
	 * done so that it can catch up.
	 */
	scheduler->checkpoint_in_progress = false;
	fiber_cond_signal(&scheduler->scheduler_cond);
}

/* Scheduler }}} */

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
vy_info_append_performance(struct vy_env *env, struct info_handler *h)
{
	struct vy_stat *stat = env->stat;

	info_table_begin(h, "performance");

	rmean_foreach(stat->rmean, vy_info_append_stat_rmean, h);

	struct mempool_stats mstats;
	mempool_stats(&env->xm->tx_mempool, &mstats);
	info_append_int(h, "tx_allocated", mstats.objcount);
	mempool_stats(&env->xm->txv_mempool, &mstats);
	info_append_int(h, "txv_allocated", mstats.objcount);
	mempool_stats(&env->xm->read_interval_mempool, &mstats);
	info_append_int(h, "read_interval", mstats.objcount);
	mempool_stats(&env->xm->read_view_mempool, &mstats);
	info_append_int(h, "read_view", mstats.objcount);

	info_append_int(h, "dump_bandwidth", vy_stat_dump_bandwidth(stat));

	struct vy_cache_env *ce = &env->cache_env;
	info_table_begin(h, "cache");
	info_append_int(h, "count", ce->cached_count);
	info_append_int(h, "used", ce->mem_used);
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

	struct vy_tx_stat *tx_stat = &env->xm->stat;
	info_table_begin(h, "tx");
	info_append_int(h, "active", tx_stat->active);
	info_append_int(h, "commit", tx_stat->commit);
	info_append_int(h, "rollback", tx_stat->rollback);
	info_append_int(h, "conflict", tx_stat->conflict);
	info_table_end(h);

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

void
vy_index_info(struct vy_index *index, struct info_handler *h)
{
	char buf[1024];
	struct vy_index_stat *stat = &index->stat;
	struct vy_cache_stat *cache_stat = &index->cache.stat;

	info_begin(h);

	struct vy_stmt_counter count = stat->memory.count;
	vy_stmt_counter_add_disk(&count, &stat->disk.count);
	vy_info_append_stmt_counter(h, NULL, &count);

	info_append_int(h, "lookup", stat->lookup);
	vy_info_append_stmt_counter(h, "get", &stat->get);
	vy_info_append_stmt_counter(h, "put", &stat->put);
	info_append_double(h, "latency", latency_get(&stat->latency));

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
	info_table_end(h);

	info_table_begin(h, "cache");
	vy_info_append_stmt_counter(h, NULL, &cache_stat->count);
	info_append_int(h, "lookup", cache_stat->lookup);
	vy_info_append_stmt_counter(h, "get", &cache_stat->get);
	vy_info_append_stmt_counter(h, "put", &cache_stat->put);
	vy_info_append_stmt_counter(h, "invalidate", &cache_stat->invalidate);
	vy_info_append_stmt_counter(h, "evict", &cache_stat->evict);
	info_table_end(h);

	info_table_begin(h, "txw");
	vy_info_append_stmt_counter(h, NULL, &stat->txw.count);
	info_table_begin(h, "iterator");
	info_append_int(h, "lookup", stat->txw.iterator.lookup);
	vy_info_append_stmt_counter(h, "get", &stat->txw.iterator.get);
	info_table_end(h);
	info_table_end(h);

	info_append_int(h, "range_count", index->range_count);
	info_append_int(h, "run_count", index->run_count);
	info_append_int(h, "run_avg", index->run_count / index->range_count);
	histogram_snprint(buf, sizeof(buf), index->run_hist);
	info_append_str(h, "run_histogram", buf);

	info_end(h);
}

/** }}} Introspection */

/**
 * Given a space and an index id, return vy_index.
 * If index not found, return NULL and set diag.
 */
static struct vy_index *
vy_index_find(struct space *space, uint32_t iid)
{
	struct index *index = index_find(space, iid);
	if (index == NULL)
		return NULL;
	return vy_index(index);
}

/**
 * Wrapper around vy_index_find() which ensures that
 * the found index is unique.
 */
static  struct vy_index *
vy_index_find_unique(struct space *space, uint32_t index_id)
{
	struct vy_index *index = vy_index_find(space, index_id);
	if (index != NULL && !index->opts.is_unique) {
		diag_set(ClientError, ER_MORE_THAN_ONE_TUPLE);
		return NULL;
	}
	return index;
}

struct vy_index *
vy_new_index(struct vy_env *env, struct index_def *index_def,
	     struct tuple_format *format, struct vy_index *pk)
{
	return vy_index_new(&env->index_env, &env->cache_env,
			    index_def, format, pk);
}

void
vy_delete_index(struct vy_env *env, struct vy_index *index)
{
	(void)env;
	/*
	 * There still may be a task scheduled for this index
	 * so postpone actual deletion until the last reference
	 * is gone.
	 */
	vy_index_unref(index);
}

/**
 * Detect whether we already have non-garbage index files,
 * and open an existing index if that's the case. Otherwise,
 * create a new index. Take the current recovery status into
 * account.
 */
int
vy_index_open(struct vy_env *env, struct vy_index *index, bool force_recovery)
{
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
		rc = vy_index_create(index);
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
		rc = vy_index_create(index);
		break;
	case VINYL_INITIAL_RECOVERY_LOCAL:
	case VINYL_FINAL_RECOVERY_LOCAL:
		/*
		 * Local WAL replay or recovery from snapshot.
		 * In either case the index directory should
		 * have already been created, so try to load
		 * the index files from it.
		 */
		rc = vy_index_recover(index, env->recovery,
				vclock_sum(env->recovery_vclock),
				env->status == VINYL_INITIAL_RECOVERY_LOCAL,
				force_recovery);
		break;
	default:
		unreachable();
	}
	return rc;
}

void
vy_index_commit_create(struct vy_env *env, struct vy_index *index, int64_t lsn)
{
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
		if (index->commit_lsn >= 0) {
			vy_scheduler_add_index(env->scheduler, index);
			return;
		}
	}

	/*
	 * Backward compatibility fixup: historically, we used
	 * box.info.signature for LSN of index creation, which
	 * lags behind the LSN of the record that created the
	 * index by 1. So for legacy indexes use the LSN from
	 * index options.
	 */
	if (index->opts.lsn != 0)
		lsn = index->opts.lsn;

	index->commit_lsn = lsn;

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
	vy_log_create_index(index->commit_lsn, index->id,
			    index->space_id, index->key_def);
	vy_log_insert_range(index->commit_lsn, range->id, NULL, NULL);
	if (vy_log_tx_try_commit() != 0)
		say_warn("failed to log index creation: %s",
			 diag_last_error(diag_get())->errmsg);
	/*
	 * After we committed the index in the log, we can schedule
	 * a task for it.
	 */
	vy_scheduler_add_index(env->scheduler, index);
}

/*
 * Delete all runs, ranges, and slices of a given index
 * from the metadata log.
 */
static void
vy_log_index_prune(struct vy_index *index, int64_t gc_lsn)
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
	rlist_foreach_entry(run, &index->runs, in_index) {
		vy_log_drop_run(run->id, gc_lsn);
		if (++loops % VY_YIELD_LOOPS == 0)
			fiber_sleep(0);
	}
}

void
vy_index_commit_drop(struct vy_env *env, struct vy_index *index)
{
	vy_scheduler_remove_index(env->scheduler, index);

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
	vy_log_index_prune(index, checkpoint_last(NULL));
	vy_log_drop_index(index->commit_lsn);
	if (vy_log_tx_try_commit() < 0)
		say_warn("failed to log drop index: %s",
			 diag_last_error(diag_get())->errmsg);
}

int
vy_prepare_truncate_space(struct vy_env *env, struct space *old_space,
			  struct space *new_space)
{
	assert(old_space->index_count == new_space->index_count);
	uint32_t index_count = new_space->index_count;
	if (index_count == 0)
		return 0;

	struct vy_index *pk = vy_index(old_space->index[0]);

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

		new_index->commit_lsn = old_index->commit_lsn;

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
			vy_scheduler_remove_index(env->scheduler, old_index);
			vy_scheduler_add_index(env->scheduler, new_index);
			continue;
		}

		if (vy_index_init_range_tree(new_index) != 0)
			return -1;

		new_index->truncate_count = new_space->truncate_count;
	}
	return 0;
}

void
vy_commit_truncate_space(struct vy_env *env, struct space *old_space,
			 struct space *new_space)
{
	assert(old_space->index_count == new_space->index_count);
	uint32_t index_count = new_space->index_count;
	if (index_count == 0)
		return;

	struct vy_index *pk = vy_index(old_space->index[0]);

	/*
	 * See the comment in vy_prepare_truncate_space().
	 */
	if (env->status == VINYL_FINAL_RECOVERY_LOCAL &&
	    pk->truncate_count > old_space->truncate_count)
		return;

	/*
	 * Mark old indexes as dropped and remove them from the scheduler.
	 * After this point no task can be scheduled or completed for any
	 * of them (only aborted).
	 */
	for (uint32_t i = 0; i < index_count; i++) {
		struct vy_index *index = vy_index(old_space->index[i]);
		index->is_dropped = true;
		vy_scheduler_remove_index(env->scheduler, index);
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
	int64_t gc_lsn = checkpoint_last(NULL);
	for (uint32_t i = 0; i < index_count; i++) {
		struct vy_index *old_index = vy_index(old_space->index[i]);
		struct vy_index *new_index = vy_index(new_space->index[i]);
		struct vy_range *range = vy_range_tree_first(new_index->tree);

		assert(!new_index->is_dropped);
		assert(new_index->truncate_count == new_space->truncate_count);
		assert(new_index->range_count == 1);

		vy_log_index_prune(old_index, gc_lsn);
		vy_log_insert_range(new_index->commit_lsn,
				    range->id, NULL, NULL);
		vy_log_truncate_index(new_index->commit_lsn,
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
		vy_scheduler_add_index(env->scheduler, index);
	}
}

int
vy_prepare_alter_space(struct vy_env *env, struct space *old_space,
		       struct space *new_space)
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
	if (env->status != VINYL_ONLINE)
		return 0;
	/* The space is empty. Allow alter. */
	if (pk->stat.disk.count.rows == 0 &&
	    pk->stat.memory.count.rows == 0)
		return 0;
	if (space_def_check_compatibility(old_space->def, new_space->def,
					  false) != 0)
		return -1;
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
							      new_def) ||
			    key_part_cmp(old_def->key_def->parts,
					 old_def->key_def->part_count,
					 new_def->key_def->parts,
					 new_def->key_def->part_count) != 0) {
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
vy_check_format(struct vy_env *env, struct space *old_space)
{
	/* @sa vy_prepare_alter_space for checks below. */
	if (old_space->index_count == 0)
		return 0;
	struct vy_index *pk = vy_index(old_space->index[0]);
	if (env->status != VINYL_ONLINE)
		return 0;
	if (pk->stat.disk.count.rows == 0 && pk->stat.memory.count.rows == 0)
		return 0;
	diag_set(ClientError, ER_UNSUPPORTED, "Vinyl",
		 "adding new fields to a non-empty space");
	return -1;
}

int
vy_commit_alter_space(struct vy_env *env, struct space *new_space,
		      struct tuple_format *new_format)
{
	(void) env;
	struct vy_index *pk = vy_index(new_space->index[0]);
	struct index_def *new_index_def = space_index_def(new_space, 0);

	assert(pk->pk == NULL);

	/* Update the format with column mask. */
	struct tuple_format *format =
		vy_tuple_format_new_with_colmask(new_format);
	if (format == NULL)
		return -1;

	/* Update the upsert format. */
	struct tuple_format *upsert_format =
		vy_tuple_format_new_upsert(new_format);
	if (upsert_format == NULL) {
		tuple_format_delete(format);
		return -1;
	}

	/* Set possibly changed opts. */
	pk->opts = new_index_def->opts;

	/* Set new formats. */
	tuple_format_unref(pk->disk_format);
	tuple_format_unref(pk->mem_format);
	tuple_format_unref(pk->upsert_format);
	tuple_format_unref(pk->mem_format_with_colmask);
	pk->disk_format = new_format;
	tuple_format_ref(new_format);
	pk->upsert_format = upsert_format;
	tuple_format_ref(upsert_format);
	pk->mem_format_with_colmask = format;
	tuple_format_ref(format);
	pk->mem_format = new_format;
	tuple_format_ref(new_format);
	vy_index_validate_formats(pk);

	for (uint32_t i = 1; i < new_space->index_count; ++i) {
		struct vy_index *index = vy_index(new_space->index[i]);
		vy_index_unref(index->pk);
		vy_index_ref(pk);
		index->pk = pk;
		new_index_def = space_index_def(new_space, i);
		index->opts = new_index_def->opts;
		tuple_format_unref(index->mem_format_with_colmask);
		tuple_format_unref(index->mem_format);
		tuple_format_unref(index->upsert_format);
		index->mem_format_with_colmask =
			pk->mem_format_with_colmask;
		index->mem_format = pk->mem_format;
		index->upsert_format = pk->upsert_format;
		tuple_format_ref(index->mem_format_with_colmask);
		tuple_format_ref(index->mem_format);
		tuple_format_ref(index->upsert_format);
		vy_index_validate_formats(index);
	}
	return 0;
}

size_t
vy_index_bsize(struct vy_index *index)
{
	return index->stat.memory.count.bytes;
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
vy_is_committed_one(struct vy_env *env, struct space *space,
		    struct vy_index *index)
{
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
vy_is_committed(struct vy_env *env, struct space *space)
{
	if (likely(env->status != VINYL_FINAL_RECOVERY_LOCAL))
		return false;
	for (uint32_t iid = 0; iid < space->index_count; iid++) {
		struct vy_index *index = vy_index(space->index[iid]);
		if (!vy_is_committed_one(env, space, index))
			return false;
	}
	return true;
}

/**
 * Get a vinyl tuple from the index by the key.
 * @param env         Vinyl environment.
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
vy_index_get(struct vy_env *env, struct vy_tx *tx, struct vy_index *index,
	     const char *key, uint32_t part_count, struct tuple **result)
{
	/*
	 * tx can be NULL, for example, if an user calls
	 * space.index.get({key}).
	 */
	assert(tx == NULL || tx->state == VINYL_TX_READY);
	struct tuple *vykey;
	assert(part_count <= index->cmp_def->part_count);
	vykey = vy_stmt_new_select(index->env->key_format, key, part_count);
	if (vykey == NULL)
		return -1;
	const struct vy_read_view **p_read_view;
	if (tx != NULL) {
		p_read_view = (const struct vy_read_view **) &tx->read_view;
	} else {
		p_read_view = &env->xm->p_global_read_view;
	}

	struct vy_read_iterator itr;
	vy_read_iterator_open(&itr, &env->run_env, index, tx,
			      ITER_EQ, vykey, p_read_view);
	int rc = vy_read_iterator_next(&itr, result);
	tuple_unref(vykey);
	if (*result != NULL)
		tuple_ref(*result);
	vy_read_iterator_close(&itr);
	if (rc != 0)
		return -1;
	rmean_collect(env->stat->rmean, VY_STAT_GET, 1);
	return 0;
}

/**
 * Check if the index contains the key. If true, then set
 * a duplicate key error in the diagnostics area.
 * @param env        Vinyl environment.
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
vy_check_dup_key(struct vy_env *env, struct vy_tx *tx, struct space *space,
		 struct vy_index *index, const char *key, uint32_t part_count)
{
	struct tuple *found;
	(void) part_count;
	/*
	 * Expect a full tuple as input (secondary key || primary key)
	 * but use only  the secondary key fields (partial key look
	 * up) to check for duplicates.
         */
	assert(part_count == index->cmp_def->part_count);
	if (vy_index_get(env, tx, index, key, index->key_def->part_count,
			 &found))
		return -1;

	if (found) {
		tuple_unref(found);
		diag_set(ClientError, ER_TUPLE_FOUND,
			 index_name_by_id(space, index->id), space_name(space));
		return -1;
	}
	return 0;
}

/**
 * Insert a tuple in a primary index.
 * @param env   Vinyl environment.
 * @param tx    Current transaction.
 * @param space Target space.
 * @param pk    Primary vinyl index.
 * @param stmt  Tuple to insert.
 *
 * @retval  0 Success.
 * @retval -1 Memory error or duplicate key error.
 */
static inline int
vy_insert_primary(struct vy_env *env, struct vy_tx *tx, struct space *space,
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
	if (vy_check_dup_key(env, tx, space, pk, key, part_count))
		return -1;
	return vy_tx_set(tx, pk, stmt);
}

/**
 * Insert a tuple in a secondary index.
 * @param env       Vinyl environment.
 * @param tx        Current transaction.
 * @param space     Target space.
 * @param index     Secondary index.
 * @param stmt      Tuple to replace.
 *
 * @retval  0 Success.
 * @retval -1 Memory error or duplicate key error.
 */
static int
vy_insert_secondary(struct vy_env *env, struct vy_tx *tx, struct space *space,
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
	if (index->opts.is_unique &&
	    (!index->key_def->is_nullable ||
	     !vy_tuple_key_contains_null(stmt, index->key_def))) {
		uint32_t key_len;
		const char *key = tuple_extract_key(stmt, index->cmp_def,
						    &key_len);
		if (key == NULL)
			return -1;
		uint32_t part_count = mp_decode_array(&key);
		if (vy_check_dup_key(env, tx, space, index, key, part_count))
			return -1;
	}
	return vy_tx_set(tx, index, stmt);
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
	assert(tx != NULL && tx->state == VINYL_TX_READY);
	struct vy_index *pk = vy_index(space->index[0]);
	assert(pk->id == 0);
	if (tuple_validate_raw(pk->mem_format, request->tuple))
		return -1;
	struct tuple *new_tuple =
		vy_stmt_new_replace(pk->mem_format, request->tuple,
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
		if (vy_get(env, tx, pk, key, part_count, &stmt->old_tuple) != 0)
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
	struct vy_index *pk = vy_index_find(space, 0);
	if (pk == NULL) /* space has no primary key */
		return -1;
	/* Primary key is dumped last. */
	assert(!vy_is_committed_one(env, space, pk));
	assert(pk->id == 0);
	if (tuple_validate_raw(pk->mem_format, request->tuple))
		return -1;
	new_stmt = vy_stmt_new_replace(pk->mem_format, request->tuple,
				       request->tuple_end);
	if (new_stmt == NULL)
		return -1;
	const char *key = tuple_extract_key(new_stmt, pk->key_def, NULL);
	if (key == NULL) /* out of memory */
		goto error;
	uint32_t part_count = mp_decode_array(&key);

	/* Get full tuple from the primary index. */
	if (vy_index_get(env, tx, pk, key, part_count, &old_stmt) != 0)
		goto error;

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
		struct vy_index *index;
		index = vy_index(space->index[iid]);
		if (vy_is_committed_one(env, space, index))
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
		if (vy_insert_secondary(env, tx, space, index, new_stmt) != 0)
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
	uint32_t original_part_count = index->key_def->part_count;
	if (original_part_count != part_count) {
		diag_set(ClientError, ER_EXACT_MATCH,
			 original_part_count, part_count);
		return -1;
	}
	return key_validate_parts(index->cmp_def, key, part_count, false);
}

/**
 * Get a tuple from the primary index by the partial tuple from
 * the secondary index.
 * @param env       Vinyl environment.
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
vy_index_full_by_stmt(struct vy_env *env, struct vy_tx *tx,
		      struct vy_index *index,
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
	return vy_index_get(env, tx, pk, pkey, part_count, full);
}

/**
 * Find a tuple in the primary index by the key of the specified
 * index.
 * @param env         Vinyl environment.
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
vy_index_full_by_key(struct vy_env *env, struct vy_tx *tx,
		     struct vy_index *index, const char *key,
		     uint32_t part_count, struct tuple **result)
{
	struct tuple *found;
	if (vy_index_get(env, tx, index, key, part_count, &found))
		return -1;
	if (index->id == 0 || found == NULL) {
		*result = found;
		return 0;
	}
	int rc = vy_index_full_by_stmt(env, tx, index, found, result);
	tuple_unref(found);
	return rc;
}

/**
 * Delete the tuple from all indexes of the vinyl space.
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
	struct vy_index *pk = vy_index_find(space, 0);
	if (pk == NULL)
		return -1;
	/* Primary key is dumped last. */
	assert(!vy_is_committed_one(env, space, pk));
	struct tuple *delete =
		vy_stmt_new_surrogate_delete(pk->mem_format, tuple);
	if (delete == NULL)
		return -1;
	if (vy_tx_set(tx, pk, delete) != 0)
		goto error;

	/* At second, delete from seconary indexes. */
	struct vy_index *index;
	for (uint32_t i = 1; i < space->index_count; ++i) {
		index = vy_index(space->index[i]);
		if (vy_is_committed_one(env, space, index))
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
vy_delete(struct vy_env *env, struct vy_tx *tx, struct txn_stmt *stmt,
	  struct space *space, struct request *request)
{
	if (vy_is_committed(env, space))
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
		if (vy_index_full_by_key(env, tx, index, key, part_count,
					 &stmt->old_tuple))
			return -1;
		if (stmt->old_tuple == NULL)
			return 0;
	}
	if (has_secondary) {
		assert(stmt->old_tuple != NULL);
		return vy_delete_impl(env, tx, space, stmt->old_tuple);
	} else { /* Primary is the single index in the space. */
		assert(index->id == 0);
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
		if (key_update_can_be_skipped(index->cmp_def->column_mask,
					      column_mask))
			return false;
	}
	return true;
}

int
vy_update(struct vy_env *env, struct vy_tx *tx, struct txn_stmt *stmt,
	  struct space *space, struct request *request)
{
	assert(tx != NULL && tx->state == VINYL_TX_READY);
	if (vy_is_committed(env, space))
		return 0;
	struct vy_index *index = vy_index_find_unique(space, request->index_id);
	if (index == NULL)
		return -1;
	const char *key = request->key;
	uint32_t part_count = mp_decode_array(&key);
	if (vy_unique_key_validate(index, key, part_count))
		return -1;

	if (vy_index_full_by_key(env, tx, index, key, part_count,
				 &stmt->old_tuple))
		return -1;
	/* Nothing to update. */
	if (stmt->old_tuple == NULL)
		return 0;

	/* Apply update operations. */
	struct vy_index *pk = vy_index(space->index[0]);
	assert(pk != NULL);
	assert(pk->id == 0);
	/* Primary key is dumped last. */
	assert(!vy_is_committed_one(env, space, pk));
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

	bool update_changes_all =
		vy_update_changes_all_indexes(space, column_mask);
	struct tuple_format *mask_format = pk->mem_format_with_colmask;
	if (space->index_count == 1 || update_changes_all) {
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

	struct tuple *delete = NULL;
	if (! update_changes_all) {
		delete = vy_stmt_new_surrogate_delete(mask_format,
						      stmt->old_tuple);
		if (delete == NULL)
			return -1;
		vy_stmt_set_column_mask(delete, column_mask);
	} else {
		delete = vy_stmt_new_surrogate_delete(pk->mem_format,
						      stmt->old_tuple);
		if (delete == NULL)
			return -1;
	}
	assert(delete != NULL);
	for (uint32_t i = 1; i < space->index_count; ++i) {
		index = vy_index(space->index[i]);
		if (vy_is_committed_one(env, space, index))
			continue;
		if (vy_tx_set(tx, index, delete) != 0)
			goto error;
		if (vy_insert_secondary(env, tx, space, index, stmt->new_tuple))
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
	assert(vy_stmt_type(stmt) == IPROTO_REPLACE);
	struct vy_index *pk = vy_index(space->index[0]);
	assert(pk->id == 0);
	if (vy_tx_set(tx, pk, stmt) != 0)
		return -1;
	struct vy_index *index;
	for (uint32_t i = 1; i < space->index_count; ++i) {
		index = vy_index(space->index[i]);
		if (vy_insert_secondary(env, tx, space, index, stmt) != 0)
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

int
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
	struct vy_index *pk = vy_index_find(space, 0);
	if (pk == NULL)
		return -1;
	/* Primary key is dumped last. */
	assert(!vy_is_committed_one(env, space, pk));
	if (tuple_validate_raw(pk->mem_format, tuple))
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
	if (vy_index_get(env, tx, pk, key, part_count, &stmt->old_tuple))
		return -1;
	/*
	 * If the old tuple was not found then UPSERT
	 * turns into INSERT.
	 */
	if (stmt->old_tuple == NULL) {
		stmt->new_tuple =
			vy_stmt_new_replace(pk->mem_format, tuple, tuple_end);
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
	bool update_changes_all =
		vy_update_changes_all_indexes(space, column_mask);
	struct tuple_format *mask_format = pk->mem_format_with_colmask;
	if (space->index_count == 1 || update_changes_all) {
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
		delete = vy_stmt_new_surrogate_delete(pk->mem_format,
						      stmt->old_tuple);
		if (delete == NULL)
			return -1;
	}
	assert(delete != NULL);
	for (uint32_t i = 1; i < space->index_count; ++i) {
		index = vy_index(space->index[i]);
		if (vy_is_committed_one(env, space, index))
			continue;
		if (vy_tx_set(tx, index, delete) != 0)
			goto error;
		if (vy_insert_secondary(env, tx, space, index,
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
	struct vy_index *pk = vy_index_find(space, 0);
	if (pk == NULL)
		/* The space hasn't the primary index. */
		return -1;
	assert(pk->id == 0);
	if (tuple_validate_raw(pk->mem_format, request->tuple))
		return -1;
	/* First insert into the primary index. */
	stmt->new_tuple =
		vy_stmt_new_replace(pk->mem_format, request->tuple,
				    request->tuple_end);
	if (stmt->new_tuple == NULL)
		return -1;
	if (vy_insert_primary(env, tx, space, pk, stmt->new_tuple) != 0)
		return -1;

	for (uint32_t iid = 1; iid < space->index_count; ++iid) {
		struct vy_index *index = vy_index(space->index[iid]);
		if (vy_insert_secondary(env, tx, space, index,
					stmt->new_tuple) != 0)
			return -1;
	}
	return 0;
}

int
vy_replace(struct vy_env *env, struct vy_tx *tx, struct txn_stmt *stmt,
	   struct space *space, struct request *request)
{
	if (vy_is_committed(env, space))
		return 0;
	if (request->type == IPROTO_INSERT && env->status == VINYL_ONLINE)
		return vy_insert(env, tx, stmt, space, request);

	if (space->index_count == 1) {
		/* Replace in a space with a single index. */
		return vy_replace_one(env, tx, space, request, stmt);
	} else {
		/* Replace in a space with secondary indexes. */
		return vy_replace_impl(env, tx, space, request, stmt);
	}
}

struct vy_tx *
vy_begin(struct vy_env *env)
{
	return vy_tx_begin(env->xm);
}

int
vy_prepare(struct vy_env *env, struct vy_tx *tx)
{
	/*
	 * Reserve quota needed by the transaction before allocating
	 * memory. Since this may yield, which opens a time window for
	 * the transaction to be sent to read view or aborted, we call
	 * it before checking for conflicts.
	 */
	if (vy_quota_use(&env->quota, tx->write_size,
			 env->timeout) != 0) {
		diag_set(ClientError, ER_VY_QUOTA_TIMEOUT);
		return -1;
	}

	size_t mem_used_before = lsregion_used(&env->stmt_env.allocator);

	int rc = vy_tx_prepare(tx);

	size_t mem_used_after = lsregion_used(&env->stmt_env.allocator);
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

	rmean_collect(env->stat->rmean, VY_STAT_TX, 1);
	rmean_collect(env->stat->rmean, VY_STAT_TX_OPS, tx->write_count);
	rmean_collect(env->stat->rmean, VY_STAT_TX_WRITE, write_size);
	return 0;
}

void
vy_commit(struct vy_env *env, struct vy_tx *tx, int64_t lsn)
{
	/*
	 * vy_tx_commit() may trigger an upsert squash.
	 * If there is no memory for a created statement,
	 * it silently fails. But if it succeeds, we
	 * need to account the memory in the quota.
	 */
	size_t mem_used_before = lsregion_used(&env->stmt_env.allocator);

	vy_tx_commit(tx, lsn);

	size_t mem_used_after = lsregion_used(&env->stmt_env.allocator);
	assert(mem_used_after >= mem_used_before);
	/* We can't abort the transaction at this point, use force. */
	vy_quota_force_use(&env->quota, mem_used_after - mem_used_before);
}

void
vy_rollback(struct vy_env *env, struct vy_tx *tx)
{
	(void)env;
	vy_tx_rollback(tx);
}

void *
vy_savepoint(struct vy_env *env, struct vy_tx *tx)
{
	(void)env;
	return vy_tx_savepoint(tx);
}

void
vy_rollback_to_savepoint(struct vy_env *env, struct vy_tx *tx, void *svp)
{
	(void)env;
	vy_tx_rollback_to_savepoint(tx, svp);
}

/* }}} Public API of transaction control */

int
vy_get(struct vy_env *env, struct vy_tx *tx, struct vy_index *index,
       const char *key, uint32_t part_count, struct tuple **result)
{
	assert(tx == NULL || tx->state == VINYL_TX_READY);
	assert(result != NULL);
	struct tuple *vyresult = NULL;
	assert(part_count <= index->cmp_def->part_count);
	if (vy_index_full_by_key(env, tx, index, key, part_count, &vyresult))
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

static void
vy_env_quota_exceeded_cb(struct vy_quota *quota)
{
	struct vy_env *env = container_of(quota, struct vy_env, quota);
	fiber_cond_signal(&env->scheduler->scheduler_cond);
}

static struct vy_squash_queue *
vy_squash_queue_new(void);
static void
vy_squash_queue_delete(struct vy_squash_queue *q);
static void
vy_squash_schedule(struct vy_index *index, struct tuple *stmt,
		   void /* struct vy_env */ *arg);

struct vy_env *
vy_env_new(const char *path, size_t memory, size_t cache, int read_threads,
	   int write_threads, double timeout)
{
	struct vy_env *e = malloc(sizeof(*e));
	if (unlikely(e == NULL)) {
		diag_set(OutOfMemory, sizeof(*e), "malloc", "struct vy_env");
		return NULL;
	}
	memset(e, 0, sizeof(*e));
	e->status = VINYL_OFFLINE;
	e->memory = memory;
	e->timeout = timeout;
	e->read_threads = read_threads;
	e->write_threads = write_threads;
	e->path = strdup(path);
	if (e->path == NULL) {
		diag_set(OutOfMemory, strlen(path),
			 "malloc", "env->path");
		goto error_path;
	}
	e->xm = tx_manager_new();
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

	vy_stmt_env_create(&e->stmt_env, e->memory);

	if (vy_index_env_create(&e->index_env, e->path,
				&e->stmt_env.allocator,
				&e->scheduler->generation, vy_squash_schedule,
				e) != 0)
		goto error_index_env;

	struct slab_cache *slab_cache = cord_slab_cache();
	mempool_create(&e->cursor_pool, slab_cache,
	               sizeof(struct vy_cursor));
	vy_quota_create(&e->quota, vy_env_quota_exceeded_cb);
	ev_timer_init(&e->quota_timer, vy_env_quota_timer_cb, 0, 1.);
	e->quota_timer.data = e;
	ev_timer_start(loop(), &e->quota_timer);
	vy_cache_env_create(&e->cache_env, slab_cache, cache);
	vy_run_env_create(&e->run_env);
	vy_log_init(e->path);
	return e;
error_index_env:
	vy_stmt_env_destroy(&e->stmt_env);
	vy_squash_queue_delete(e->squash_queue);
error_squash_queue:
	vy_scheduler_delete(e->scheduler);
error_sched:
	vy_stat_delete(e->stat);
error_stat:
	tx_manager_delete(e->xm);
error_xm:
	free(e->path);
error_path:
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
	free(e->path);
	vy_stat_delete(e->stat);
	mempool_destroy(&e->cursor_pool);
	vy_run_env_destroy(&e->run_env);
	vy_index_env_destroy(&e->index_env);
	vy_stmt_env_destroy(&e->stmt_env);
	vy_cache_env_destroy(&e->cache_env);
	vy_quota_destroy(&e->quota);
	if (e->recovery != NULL)
		vy_recovery_delete(e->recovery);
	vy_log_free();
	TRASH(e);
	free(e);
}

void
vy_set_max_tuple_size(struct vy_env *env, size_t max_size)
{
	(void) env;
	vy_max_tuple_size = max_size;
}

void
vy_set_timeout(struct vy_env *env, double timeout)
{
	env->timeout = timeout;
}

/** }}} Environment */

/* {{{ Checkpoint */

int
vy_begin_checkpoint(struct vy_env *env)
{
	assert(env->status == VINYL_ONLINE);
	if (vy_scheduler_begin_checkpoint(env->scheduler) != 0)
		return -1;
	return 0;
}

int
vy_wait_checkpoint(struct vy_env *env, struct vclock *vclock)
{
	assert(env->status == VINYL_ONLINE);
	if (vy_scheduler_wait_checkpoint(env->scheduler) != 0)
		return -1;
	if (vy_log_rotate(vclock) != 0)
		return -1;
	return 0;
}

void
vy_commit_checkpoint(struct vy_env *env, struct vclock *vclock)
{
	(void)vclock;
	assert(env->status == VINYL_ONLINE);
	vy_scheduler_end_checkpoint(env->scheduler);
}

void
vy_abort_checkpoint(struct vy_env *env)
{
	assert(env->status == VINYL_ONLINE);
	vy_scheduler_end_checkpoint(env->scheduler);
}

/* }}} Checkpoint */

/** {{{ Recovery */

int
vy_bootstrap(struct vy_env *e)
{
	assert(e->status == VINYL_OFFLINE);
	if (vy_log_bootstrap() != 0)
		return -1;
	vy_quota_set_limit(&e->quota, e->memory);
	e->status = VINYL_ONLINE;
	return 0;
}

int
vy_begin_initial_recovery(struct vy_env *e,
			  const struct vclock *recovery_vclock)
{
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
		vy_quota_set_limit(&e->quota, e->memory);
		break;
	case VINYL_FINAL_RECOVERY_REMOTE:
		break;
	default:
		unreachable();
	}
	/*
	 * Do not start reader threads if no Vinyl index was
	 * recovered. The threads will be started lazily upon
	 * the first index creation, see vy_index_open().
	 */
	if (e->index_env.index_count > 0)
		vy_run_env_enable_coio(&e->run_env, e->read_threads);
	e->status = VINYL_ONLINE;
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
	/** Ordinal number of the index. */
	uint32_t index_id;
	/**
	 * Index key definition, as defined by the user.
	 * We only send the primary key, so the definition
	 * provided by the user is correct for compare.
	 */
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
	struct rlist fake_read_views;
	rlist_create(&fake_read_views);
	ctx->wi = vy_write_iterator_new(ctx->key_def,
					ctx->format, ctx->upsert_format,
					true, true, &fake_read_views);
	if (ctx->wi == NULL)
		goto out;

	struct vy_slice *slice;
	rlist_foreach_entry(slice, &ctx->slices, in_join) {
		if (vy_write_iterator_new_slice(ctx->wi, slice,
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
			tuple_format_unref(ctx->format);
		ctx->format = tuple_format_new(&vy_tuple_format_vtab,
					       &ctx->key_def, 1, 0, NULL, 0);
		if (ctx->format == NULL)
			return -1;
		tuple_format_ref(ctx->format);
		if (ctx->upsert_format != NULL)
			tuple_format_unref(ctx->upsert_format);
		ctx->upsert_format = vy_tuple_format_new_upsert(ctx->format);
		if (ctx->upsert_format == NULL)
			return -1;
		tuple_format_ref(ctx->upsert_format);
	}

	/*
	 * We are only interested in the primary index.
	 * Secondary keys will be rebuilt on the destination.
	 */
	if (ctx->index_id != 0)
		return 0;

	if (record->type == VY_LOG_INSERT_SLICE) {
		struct tuple_format *key_format = ctx->env->index_env.key_format;
		struct tuple *begin = NULL, *end = NULL;
		bool success = false;

		struct vy_run *run = vy_run_new(record->run_id);
		if (run == NULL)
			goto done_slice;
		if (vy_run_recover(run, ctx->env->path,
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
	rc = vy_recovery_iterate(recovery, vy_join_cb, ctx);
	vy_recovery_delete(recovery);
	/* Send the last range. */
	if (rc == 0)
		rc = vy_send_range(ctx);

	/* Cleanup. */
	if (ctx->key_def != NULL)
		free(ctx->key_def);
	if (ctx->format != NULL)
		tuple_format_unref(ctx->format);
	if (ctx->upsert_format != NULL)
		tuple_format_unref(ctx->upsert_format);
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
		vy_run_snprint_path(path, sizeof(path), arg->env->path,
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
	vy_recovery_iterate(recovery, vy_gc_cb, &arg);
}

void
vy_collect_garbage(struct vy_env *env, int64_t lsn)
{
	/* Cleanup old metadata log files. */
	vy_log_collect_garbage(lsn);

	/* Cleanup run files. */
	int64_t signature = checkpoint_last(NULL);
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

	if (record->type != VY_LOG_CREATE_RUN || record->is_dropped)
		goto out;

	char path[PATH_MAX];
	for (int type = 0; type < vy_file_MAX; type++) {
		vy_run_snprint_path(path, sizeof(path), arg->env->path,
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
	int rc = vy_recovery_iterate(recovery, vy_backup_cb, &arg);
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
	/** Index this request is for. */
	struct vy_index *index;
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
	      struct vy_index *index, struct tuple *stmt)
{
	struct vy_squash *squash;
	squash = mempool_alloc(pool);
	if (squash == NULL)
		return NULL;
	squash->env = env;
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
	struct vy_env *env = squash->env;
	/*
	 * vy_apply_upsert() is used for primary key only,
	 * so this is the same as index->key_def
	 */
	struct key_def *def = index->cmp_def;

	/* Upserts enabled only in the primary index. */
	assert(index->id == 0);

	struct vy_read_iterator itr;
	/*
	 * Use the committed read view to avoid squashing
	 * prepared, but not committed statements.
	 */
	vy_read_iterator_open(&itr, &env->run_env, index, NULL, ITER_EQ,
			      squash->stmt, &env->xm->p_committed_read_view);
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
		index->stat.upsert.applied++;
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

	index->stat.upsert.squashed++;

	/*
	 * Insert the resulting REPLACE statement to the mem
	 * and adjust the quota.
	 */
	size_t mem_used_before = lsregion_used(&env->stmt_env.allocator);
	const struct tuple *region_stmt = NULL;
	rc = vy_index_set(index, mem, result, &region_stmt);
	tuple_unref(result);
	size_t mem_used_after = lsregion_used(&env->stmt_env.allocator);
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
vy_squash_schedule(struct vy_index *index, struct tuple *stmt, void *arg)
{
	struct vy_env *env = arg;
	struct vy_squash_queue *sq = env->squash_queue;

	say_debug("optimize upsert slow: %"PRIu32"/%"PRIu32": %s",
		  index->space_id, index->id, vy_stmt_str(stmt));

	/* Start the upsert squashing fiber on demand. */
	if (sq->fiber == NULL) {
		sq->fiber = fiber_new("vinyl.squash_queue", vy_squash_queue_f);
		if (sq->fiber == NULL)
			goto fail;
		fiber_start(sq->fiber, sq);
	}

	struct vy_squash *squash = vy_squash_new(&sq->pool, env, index, stmt);
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
vy_cursor_on_tx_destroy(struct trigger *trigger, void *event)
{
	(void)event;
	struct vy_cursor *c = container_of(trigger, struct vy_cursor,
					   on_tx_destroy);
	c->tx = NULL;
}

struct vy_cursor *
vy_cursor_new(struct vy_env *env, struct vy_tx *tx, struct vy_index *index,
	      const char *key, uint32_t part_count, enum iterator_type type)
{
	struct vy_cursor *c = mempool_alloc(&env->cursor_pool);
	if (c == NULL) {
		diag_set(OutOfMemory, sizeof(*c), "cursor", "cursor pool");
		return NULL;
	}
	assert(part_count <= index->cmp_def->part_count);
	c->key = vy_stmt_new_select(index->env->key_format, key, part_count);
	if (c->key == NULL) {
		mempool_free(&env->cursor_pool, c);
		return NULL;
	}
	c->index = index;
	c->n_reads = 0;
	trigger_create(&c->on_tx_destroy, vy_cursor_on_tx_destroy, NULL, NULL);
	if (tx == NULL) {
		tx = &c->tx_autocommit;
		vy_tx_create(env->xm, tx);
	} else {
		/*
		 * Register a trigger that will abort this cursor
		 * when the transaction ends.
		 */
		trigger_add(&tx->on_destroy, &c->on_tx_destroy);
	}
	c->tx = tx;
	vy_read_iterator_open(&c->iterator, &env->run_env, index, tx,
			      type, c->key,
			      (const struct vy_read_view **)&tx->read_view);
	vy_index_ref(c->index);
	return c;
}

int
vy_cursor_next(struct vy_env *env, struct vy_cursor *c, struct tuple **result)
{
	struct tuple *vyresult = NULL;
	struct vy_index *index = c->index;
	*result = NULL;

	if (c->tx == NULL) {
		diag_set(ClientError, ER_CURSOR_NO_TRANSACTION);
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
	if (vyresult == NULL)
		return 0;
	if (index->id > 0 && vy_index_full_by_stmt(env, c->tx, index, vyresult,
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
vy_cursor_delete(struct vy_env *env, struct vy_cursor *c)
{
	vy_read_iterator_close(&c->iterator);
	if (c->tx != NULL) {
		if (c->tx == &c->tx_autocommit) {
			/*
			 * Rollback the automatic transaction,
			 * use vy_tx_destroy() to not spoil
			 * the statistics of rollbacks issued
			 * by user transactions.
			 */
			vy_tx_destroy(c->tx);
		} else {
			trigger_clear(&c->on_tx_destroy);
		}
	}
	if (c->key)
		tuple_unref(c->key);
	vy_index_unref(c->index);
	rmean_collect(env->stat->rmean, VY_STAT_CURSOR, 1);
	rmean_collect(env->stat->rmean, VY_STAT_CURSOR_OPS, c->n_reads);
	TRASH(c);
	mempool_free(&env->cursor_pool, c);
}

/*** }}} Cursor */
