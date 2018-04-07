/*
 * Copyright 2010-2017, Tarantool AUTHORS, please see AUTHORS file.
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
 * THIS SOFTWARE IS PROVIDED BY AUTHORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * AUTHORS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include "vy_scheduler.h"

#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <small/mempool.h>
#include <small/rlist.h>
#include <tarantool_ev.h>

#include "checkpoint.h"
#include "diag.h"
#include "errcode.h"
#include "errinj.h"
#include "fiber.h"
#include "fiber_cond.h"
#include "salad/stailq.h"
#include "say.h"
#include "vy_lsm.h"
#include "vy_log.h"
#include "vy_mem.h"
#include "vy_range.h"
#include "vy_run.h"
#include "vy_write_iterator.h"
#include "trivia/util.h"
#include "tt_pthread.h"

/**
 * Yield after iterating over this many objects (e.g. ranges).
 * Yield more often in debug mode.
 */
#if defined(NDEBUG)
enum { VY_YIELD_LOOPS = 128 };
#else
enum { VY_YIELD_LOOPS = 2 };
#endif

/* Min and max values for vy_scheduler::timeout. */
#define VY_SCHEDULER_TIMEOUT_MIN	1
#define VY_SCHEDULER_TIMEOUT_MAX	60

static void *vy_worker_f(void *);
static int vy_scheduler_f(va_list);

struct vy_task;

struct vy_task_ops {
	/**
	 * This function is called from a worker. It is supposed to do work
	 * which is too heavy for the tx thread (like IO or compression).
	 * Returns 0 on success. On failure returns -1 and sets diag.
	 */
	int (*execute)(struct vy_scheduler *scheduler, struct vy_task *task);
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
	 * the LSM tree when preparing the task.
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
	/** LSM tree this task is for. */
	struct vy_lsm *lsm;
	/**
	 * Copies of lsm->key/cmp_def to protect from
	 * multithread read/write on alter.
	 */
	struct key_def *cmp_def;
	struct key_def *key_def;
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
	 * Link in the list of pending or processed tasks.
	 * See vy_scheduler::input_queue, output_queue.
	 */
	struct stailq_entry link;
	/**
	 * Index options may be modified while a task is in
	 * progress so we save them here to safely access them
	 * from another thread.
	 */
	double bloom_fpr;
	int64_t page_size;
};

/**
 * Allocate a new task to be executed by a worker thread.
 * When preparing an asynchronous task, this function must
 * be called before yielding the current fiber in order to
 * pin the LSM tree the task is for so that a concurrent fiber
 * does not free it from under us.
 */
static struct vy_task *
vy_task_new(struct mempool *pool, struct vy_lsm *lsm,
	    const struct vy_task_ops *ops)
{
	struct vy_task *task = mempool_alloc(pool);
	if (task == NULL) {
		diag_set(OutOfMemory, sizeof(*task),
			 "mempool", "struct vy_task");
		return NULL;
	}
	memset(task, 0, sizeof(*task));
	task->ops = ops;
	task->lsm = lsm;
	task->cmp_def = key_def_dup(lsm->cmp_def);
	if (task->cmp_def == NULL) {
		mempool_free(pool, task);
		return NULL;
	}
	task->key_def = key_def_dup(lsm->key_def);
	if (task->key_def == NULL) {
		key_def_delete(task->cmp_def);
		mempool_free(pool, task);
		return NULL;
	}
	vy_lsm_ref(lsm);
	diag_create(&task->diag);
	return task;
}

/** Free a task allocated with vy_task_new(). */
static void
vy_task_delete(struct mempool *pool, struct vy_task *task)
{
	key_def_delete(task->cmp_def);
	key_def_delete(task->key_def);
	vy_lsm_unref(task->lsm);
	diag_destroy(&task->diag);
	TRASH(task);
	mempool_free(pool, task);
}

static bool
vy_dump_heap_less(struct heap_node *a, struct heap_node *b)
{
	struct vy_lsm *i1 = container_of(a, struct vy_lsm, in_dump);
	struct vy_lsm *i2 = container_of(b, struct vy_lsm, in_dump);

	/*
	 * LSM trees that are currently being dumped or can't be
	 * scheduled for dump right now are moved off the top of
	 * the heap.
	 */
	if (i1->is_dumping != i2->is_dumping)
		return i1->is_dumping < i2->is_dumping;
	if (i1->pin_count != i2->pin_count)
		return i1->pin_count < i2->pin_count;

	/* Older LSM trees are dumped first. */
	int64_t i1_generation = vy_lsm_generation(i1);
	int64_t i2_generation = vy_lsm_generation(i2);
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
	return i1->index_id > i2->index_id;
}

#define HEAP_NAME vy_dump_heap
#define HEAP_LESS(h, l, r) vy_dump_heap_less(l, r)

#include "salad/heap.h"

#undef HEAP_LESS
#undef HEAP_NAME

static bool
vy_compact_heap_less(struct heap_node *a, struct heap_node *b)
{
	struct vy_lsm *i1 = container_of(a, struct vy_lsm, in_compact);
	struct vy_lsm *i2 = container_of(b, struct vy_lsm, in_compact);
	/*
	 * Prefer LSM trees whose read amplification will be reduced
	 * most as a result of compaction.
	 */
	return vy_lsm_compact_priority(i1) > vy_lsm_compact_priority(i2);
}

#define HEAP_NAME vy_compact_heap
#define HEAP_LESS(h, l, r) vy_compact_heap_less(l, r)

#include "salad/heap.h"

#undef HEAP_LESS
#undef HEAP_NAME

static void
vy_scheduler_async_cb(ev_loop *loop, struct ev_async *watcher, int events)
{
	(void)loop;
	(void)events;
	struct vy_scheduler *scheduler = container_of(watcher,
			struct vy_scheduler, scheduler_async);
	fiber_cond_signal(&scheduler->scheduler_cond);
}

static void
vy_scheduler_start_workers(struct vy_scheduler *scheduler)
{
	assert(!scheduler->is_worker_pool_running);
	/* One thread is reserved for dumps, see vy_schedule(). */
	assert(scheduler->worker_pool_size >= 2);

	scheduler->is_worker_pool_running = true;
	scheduler->workers_available = scheduler->worker_pool_size;
	scheduler->worker_pool = calloc(scheduler->worker_pool_size,
					sizeof(struct cord));
	if (scheduler->worker_pool == NULL)
		panic("failed to allocate vinyl worker pool");

	ev_async_start(scheduler->scheduler_loop, &scheduler->scheduler_async);
	for (int i = 0; i < scheduler->worker_pool_size; i++) {
		char name[FIBER_NAME_MAX];
		snprintf(name, sizeof(name), "vinyl.writer.%d", i);
		if (cord_start(&scheduler->worker_pool[i], name,
				 vy_worker_f, scheduler) != 0)
			panic("failed to start vinyl worker thread");
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
	ev_async_stop(scheduler->scheduler_loop, &scheduler->scheduler_async);

	free(scheduler->worker_pool);
	scheduler->worker_pool = NULL;

	/* Abort all pending tasks. */
	struct vy_task *task, *next;
	stailq_concat(&task_queue, &scheduler->output_queue);
	stailq_foreach_entry_safe(task, next, &task_queue, link) {
		if (task->ops->abort != NULL)
			task->ops->abort(scheduler, task, true);
		vy_task_delete(&scheduler->task_pool, task);
	}
}

void
vy_scheduler_create(struct vy_scheduler *scheduler, int write_threads,
		    vy_scheduler_dump_complete_f dump_complete_cb,
		    struct vy_run_env *run_env, struct rlist *read_views)
{
	memset(scheduler, 0, sizeof(*scheduler));

	scheduler->dump_complete_cb = dump_complete_cb;
	scheduler->read_views = read_views;
	scheduler->run_env = run_env;

	scheduler->scheduler_fiber = fiber_new("vinyl.scheduler",
					       vy_scheduler_f);
	if (scheduler->scheduler_fiber == NULL)
		panic("failed to allocate vinyl scheduler fiber");

	scheduler->scheduler_loop = loop();
	fiber_cond_create(&scheduler->scheduler_cond);
	ev_async_init(&scheduler->scheduler_async, vy_scheduler_async_cb);

	scheduler->worker_pool_size = write_threads;
	mempool_create(&scheduler->task_pool, cord_slab_cache(),
		       sizeof(struct vy_task));
	stailq_create(&scheduler->input_queue);
	stailq_create(&scheduler->output_queue);

	tt_pthread_cond_init(&scheduler->worker_cond, NULL);
	tt_pthread_mutex_init(&scheduler->mutex, NULL);

	vy_dump_heap_create(&scheduler->dump_heap);
	vy_compact_heap_create(&scheduler->compact_heap);

	diag_create(&scheduler->diag);
	fiber_cond_create(&scheduler->dump_cond);

	fiber_start(scheduler->scheduler_fiber, scheduler);
}

void
vy_scheduler_destroy(struct vy_scheduler *scheduler)
{
	/* Stop scheduler fiber. */
	scheduler->scheduler_fiber = NULL;
	/* Sic: fiber_cancel() can't be used here. */
	fiber_cond_signal(&scheduler->dump_cond);
	fiber_cond_signal(&scheduler->scheduler_cond);

	if (scheduler->is_worker_pool_running)
		vy_scheduler_stop_workers(scheduler);

	tt_pthread_cond_destroy(&scheduler->worker_cond);
	tt_pthread_mutex_destroy(&scheduler->mutex);

	diag_destroy(&scheduler->diag);
	mempool_destroy(&scheduler->task_pool);
	fiber_cond_destroy(&scheduler->dump_cond);
	fiber_cond_destroy(&scheduler->scheduler_cond);
	vy_dump_heap_destroy(&scheduler->dump_heap);
	vy_compact_heap_destroy(&scheduler->compact_heap);

	TRASH(scheduler);
}

void
vy_scheduler_add_lsm(struct vy_scheduler *scheduler, struct vy_lsm *lsm)
{
	assert(lsm->in_dump.pos == UINT32_MAX);
	assert(lsm->in_compact.pos == UINT32_MAX);
	vy_dump_heap_insert(&scheduler->dump_heap, &lsm->in_dump);
	vy_compact_heap_insert(&scheduler->compact_heap, &lsm->in_compact);
}

void
vy_scheduler_remove_lsm(struct vy_scheduler *scheduler, struct vy_lsm *lsm)
{
	assert(lsm->in_dump.pos != UINT32_MAX);
	assert(lsm->in_compact.pos != UINT32_MAX);
	vy_dump_heap_delete(&scheduler->dump_heap, &lsm->in_dump);
	vy_compact_heap_delete(&scheduler->compact_heap, &lsm->in_compact);
	lsm->in_dump.pos = UINT32_MAX;
	lsm->in_compact.pos = UINT32_MAX;
}

static void
vy_scheduler_update_lsm(struct vy_scheduler *scheduler, struct vy_lsm *lsm)
{
	if (lsm->is_dropped) {
		/* Dropped LSM trees are exempted from scheduling. */
		assert(lsm->in_dump.pos == UINT32_MAX);
		assert(lsm->in_compact.pos == UINT32_MAX);
		return;
	}
	assert(lsm->in_dump.pos != UINT32_MAX);
	assert(lsm->in_compact.pos != UINT32_MAX);
	vy_dump_heap_update(&scheduler->dump_heap, &lsm->in_dump);
	vy_compact_heap_update(&scheduler->compact_heap, &lsm->in_compact);
}

static void
vy_scheduler_pin_lsm(struct vy_scheduler *scheduler, struct vy_lsm *lsm)
{
	assert(!lsm->is_dumping);
	if (lsm->pin_count++ == 0)
		vy_scheduler_update_lsm(scheduler, lsm);
}

static void
vy_scheduler_unpin_lsm(struct vy_scheduler *scheduler, struct vy_lsm *lsm)
{
	assert(!lsm->is_dumping);
	assert(lsm->pin_count > 0);
	if (--lsm->pin_count == 0)
		vy_scheduler_update_lsm(scheduler, lsm);
}

void
vy_scheduler_trigger_dump(struct vy_scheduler *scheduler)
{
	assert(scheduler->dump_generation <= scheduler->generation);
	if (scheduler->dump_generation < scheduler->generation) {
		/* Dump is already in progress, nothing to do. */
		return;
	}
	if (scheduler->checkpoint_in_progress) {
		/*
		 * Do not trigger another dump until checkpoint
		 * is complete so as to make sure no statements
		 * inserted after WAL rotation are written to
		 * the snapshot.
		 */
		scheduler->dump_pending = true;
		return;
	}
	scheduler->dump_start = ev_monotonic_now(loop());
	scheduler->generation++;
	scheduler->dump_pending = false;
	fiber_cond_signal(&scheduler->scheduler_cond);
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
		struct vy_lsm *lsm = container_of(pn, struct vy_lsm, in_dump);
		min_generation = vy_lsm_generation(lsm);
	}
	if (min_generation == scheduler->dump_generation) {
		/*
		 * There are still LSM trees that must be dumped
		 * during the current dump round.
		 */
		return;
	}

	/*
	 * The oldest LSM tree data is newer than @dump_generation,
	 * so the current dump round has been finished. Notify about
	 * dump completion.
	 */
	double now = ev_monotonic_now(loop());
	double dump_duration = now - scheduler->dump_start;
	scheduler->dump_start = now;
	scheduler->dump_generation = min_generation;
	scheduler->dump_complete_cb(scheduler,
			min_generation - 1, dump_duration);
	fiber_cond_signal(&scheduler->dump_cond);
}

int
vy_scheduler_begin_checkpoint(struct vy_scheduler *scheduler)
{
	assert(!scheduler->checkpoint_in_progress);

	/*
	 * If the scheduler is throttled due to errors, do not wait
	 * until it wakes up as it may take quite a while. Instead
	 * fail checkpoint immediately with the last error seen by
	 * the scheduler.
	 */
	if (scheduler->is_throttled) {
		struct error *e = diag_last_error(&scheduler->diag);
		diag_add_error(diag_get(), e);
		say_error("cannot checkpoint vinyl, "
			  "scheduler is throttled with: %s", e->errmsg);
		return -1;
	}

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
	scheduler->checkpoint_in_progress = true;
	fiber_cond_signal(&scheduler->scheduler_cond);
	say_info("vinyl checkpoint started");
	return 0;
}

int
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
			struct error *e = diag_last_error(&scheduler->diag);
			diag_add_error(diag_get(), e);
			say_error("vinyl checkpoint failed: %s", e->errmsg);
			return -1;
		}
		fiber_cond_wait(&scheduler->dump_cond);
	}
	say_info("vinyl checkpoint completed");
	return 0;
}

void
vy_scheduler_end_checkpoint(struct vy_scheduler *scheduler)
{
	if (!scheduler->checkpoint_in_progress)
		return;

	scheduler->checkpoint_in_progress = false;
	if (scheduler->dump_pending) {
		/*
		 * Dump was triggered while checkpoint was
		 * in progress and hence it was postponed.
		 * Schedule it now.
		 */
		vy_scheduler_trigger_dump(scheduler);
	}
}

/**
 * Allocate a new run for an LSM tree and write the information
 * about it to the metadata log so that we could still find
 * and delete it in case a write error occured. This function
 * is called from dump/compaction task constructor.
 */
static struct vy_run *
vy_run_prepare(struct vy_run_env *run_env, struct vy_lsm *lsm)
{
	struct vy_run *run = vy_run_new(run_env, vy_log_next_id());
	if (run == NULL)
		return NULL;
	vy_log_tx_begin();
	vy_log_prepare_run(lsm->id, run->id);
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
	/*
	 * Leave the record in the vylog buffer on disk error.
	 * If we fail to flush it before restart, we will delete
	 * the run file upon recovery completion.
	 */
	vy_log_tx_try_commit();
}

static int
vy_task_write_run(struct vy_scheduler *scheduler, struct vy_task *task)
{
	struct vy_lsm *lsm = task->lsm;
	struct vy_stmt_stream *wi = task->wi;

	ERROR_INJECT(ERRINJ_VY_RUN_WRITE,
		     {diag_set(ClientError, ER_INJECTION,
			       "vinyl dump"); return -1;});

	struct errinj *inj = errinj(ERRINJ_VY_RUN_WRITE_TIMEOUT, ERRINJ_DOUBLE);
	if (inj != NULL && inj->dparam > 0)
		usleep(inj->dparam * 1000000);

	struct vy_run_writer writer;
	if (vy_run_writer_create(&writer, task->new_run, lsm->env->path,
				 lsm->space_id, lsm->index_id,
				 task->cmp_def, task->key_def,
				 task->page_size, task->bloom_fpr) != 0)
		goto fail;

	if (wi->iface->start(wi) != 0)
		goto fail_abort_writer;
	int rc;
	struct tuple *stmt = NULL;
	while ((rc = wi->iface->next(wi, &stmt)) == 0 && stmt != NULL) {
		inj = errinj(ERRINJ_VY_RUN_WRITE_STMT_TIMEOUT, ERRINJ_DOUBLE);
		if (inj != NULL && inj->dparam > 0)
			usleep(inj->dparam * 1000000);

		rc = vy_run_writer_append_stmt(&writer, stmt);
		if (rc != 0)
			break;

		if (!scheduler->is_worker_pool_running) {
			diag_set(FiberIsCancelled);
			rc = -1;
			break;
		}
	}
	wi->iface->stop(wi);

	if (rc == 0)
		rc = vy_run_writer_commit(&writer);
	if (rc != 0)
		goto fail_abort_writer;

	return 0;

fail_abort_writer:
	vy_run_writer_abort(&writer);
fail:
	return -1;
}

static int
vy_task_dump_execute(struct vy_scheduler *scheduler, struct vy_task *task)
{
	return vy_task_write_run(scheduler, task);
}

static int
vy_task_dump_complete(struct vy_scheduler *scheduler, struct vy_task *task)
{
	struct vy_lsm *lsm = task->lsm;
	struct vy_run *new_run = task->new_run;
	int64_t dump_lsn = new_run->dump_lsn;
	struct tuple_format *key_format = lsm->env->key_format;
	struct vy_mem *mem, *next_mem;
	struct vy_slice **new_slices, *slice;
	struct vy_range *range, *begin_range, *end_range;
	struct tuple *min_key, *max_key;
	int i, loops = 0;

	assert(lsm->is_dumping);

	if (vy_run_is_empty(new_run)) {
		/*
		 * In case the run is empty, we can discard the run
		 * and delete dumped in-memory trees right away w/o
		 * inserting slices into ranges. However, we need
		 * to log LSM tree dump anyway.
		 */
		vy_log_tx_begin();
		vy_log_dump_lsm(lsm->id, dump_lsn);
		if (vy_log_tx_commit() < 0)
			goto fail;
		vy_run_discard(new_run);
		goto delete_mems;
	}

	assert(new_run->info.min_lsn > lsm->dump_lsn);
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
	begin_range = vy_range_tree_psearch(lsm->tree, min_key);
	end_range = vy_range_tree_nsearch(lsm->tree, max_key);
	tuple_unref(min_key);
	tuple_unref(max_key);

	/*
	 * For each intersected range allocate a slice of the new run.
	 */
	new_slices = calloc(lsm->range_count, sizeof(*new_slices));
	if (new_slices == NULL) {
		diag_set(OutOfMemory, lsm->range_count * sizeof(*new_slices),
			 "malloc", "struct vy_slice *");
		goto fail;
	}
	for (range = begin_range, i = 0; range != end_range;
	     range = vy_range_tree_next(lsm->tree, range), i++) {
		slice = vy_slice_new(vy_log_next_id(), new_run,
				     range->begin, range->end, lsm->cmp_def);
		if (slice == NULL)
			goto fail_free_slices;

		assert(i < lsm->range_count);
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
	vy_log_create_run(lsm->id, new_run->id, dump_lsn);
	for (range = begin_range, i = 0; range != end_range;
	     range = vy_range_tree_next(lsm->tree, range), i++) {
		assert(i < lsm->range_count);
		slice = new_slices[i];
		vy_log_insert_slice(range->id, new_run->id, slice->id,
				    tuple_data_or_null(slice->begin),
				    tuple_data_or_null(slice->end));

		if (++loops % VY_YIELD_LOOPS == 0)
			fiber_sleep(0); /* see comment above */
	}
	vy_log_dump_lsm(lsm->id, dump_lsn);
	if (vy_log_tx_commit() < 0)
		goto fail_free_slices;

	/*
	 * Account the new run.
	 */
	vy_lsm_add_run(lsm, new_run);
	vy_stmt_counter_add_disk(&lsm->stat.disk.dump.out, &new_run->count);

	/* Drop the reference held by the task. */
	vy_run_unref(new_run);

	/*
	 * Add new slices to ranges.
	 */
	for (range = begin_range, i = 0; range != end_range;
	     range = vy_range_tree_next(lsm->tree, range), i++) {
		assert(i < lsm->range_count);
		slice = new_slices[i];
		vy_lsm_unacct_range(lsm, range);
		vy_range_add_slice(range, slice);
		vy_lsm_acct_range(lsm, range);
		vy_range_update_compact_priority(range, &lsm->opts);
		if (!vy_range_is_scheduled(range))
			vy_range_heap_update(&lsm->range_heap,
					     &range->heap_node);
		range->version++;
		/*
		 * If we yield here, a concurrent fiber will see
		 * a range with a run slice containing statements
		 * present in the in-memory indexes of the LSM tree.
		 * This is OK, because read iterator won't use the
		 * new run slice until lsm->dump_lsn is bumped,
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
	rlist_foreach_entry_safe(mem, &lsm->sealed, in_sealed, next_mem) {
		if (mem->generation > scheduler->dump_generation)
			continue;
		vy_stmt_counter_add(&lsm->stat.disk.dump.in, &mem->count);
		vy_lsm_delete_mem(lsm, mem);
	}
	lsm->dump_lsn = dump_lsn;
	lsm->stat.disk.dump.count++;

	/* The iterator has been cleaned up in a worker thread. */
	task->wi->iface->close(task->wi);

	lsm->is_dumping = false;
	vy_scheduler_update_lsm(scheduler, lsm);

	if (lsm->index_id != 0)
		vy_scheduler_unpin_lsm(scheduler, lsm->pk);

	assert(scheduler->dump_task_count > 0);
	scheduler->dump_task_count--;

	say_info("%s: dump completed", vy_lsm_name(lsm));

	vy_scheduler_complete_dump(scheduler);
	return 0;

fail_free_slices:
	for (i = 0; i < lsm->range_count; i++) {
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
	struct vy_lsm *lsm = task->lsm;

	assert(lsm->is_dumping);

	/* The iterator has been cleaned up in a worker thread. */
	task->wi->iface->close(task->wi);

	/*
	 * It's no use alerting the user if the server is
	 * shutting down or the LSM tree was dropped.
	 */
	if (!in_shutdown && !lsm->is_dropped) {
		struct error *e = diag_last_error(&task->diag);
		error_log(e);
		say_error("%s: dump failed", vy_lsm_name(lsm));
	}

	/* The metadata log is unavailable on shutdown. */
	if (!in_shutdown)
		vy_run_discard(task->new_run);
	else
		vy_run_unref(task->new_run);

	lsm->is_dumping = false;
	vy_scheduler_update_lsm(scheduler, lsm);

	if (lsm->index_id != 0)
		vy_scheduler_unpin_lsm(scheduler, lsm->pk);

	assert(scheduler->dump_task_count > 0);
	scheduler->dump_task_count--;

	/*
	 * If the LSM tree was dropped during dump, we abort
	 * the dump task, but we should still poke the scheduler
	 * to check if the current dump round is complete.
	 * If we don't and this LSM tree happens to be the last
	 * one of the current generation, the scheduler will
	 * never be notified about dump completion and hence
	 * memory will never be released.
	 */
	if (lsm->is_dropped)
		vy_scheduler_complete_dump(scheduler);
}

/**
 * Create a task to dump an LSM tree.
 *
 * On success the task is supposed to dump all in-memory
 * trees created at @scheduler->dump_generation.
 */
static int
vy_task_dump_new(struct vy_scheduler *scheduler, struct vy_lsm *lsm,
		 struct vy_task **p_task)
{
	static struct vy_task_ops dump_ops = {
		.execute = vy_task_dump_execute,
		.complete = vy_task_dump_complete,
		.abort = vy_task_dump_abort,
	};

	assert(!lsm->is_dropped);
	assert(!lsm->is_dumping);
	assert(lsm->pin_count == 0);
	assert(vy_lsm_generation(lsm) == scheduler->dump_generation);
	assert(scheduler->dump_generation < scheduler->generation);

	struct errinj *inj = errinj(ERRINJ_VY_INDEX_DUMP, ERRINJ_INT);
	if (inj != NULL && inj->iparam == (int)lsm->index_id) {
		diag_set(ClientError, ER_INJECTION, "vinyl index dump");
		goto err;
	}

	/* Rotate the active tree if it needs to be dumped. */
	if (lsm->mem->generation == scheduler->dump_generation &&
	    vy_lsm_rotate_mem(lsm) != 0)
		goto err;

	/*
	 * Wait until all active writes to in-memory trees
	 * eligible for dump are over.
	 */
	int64_t dump_lsn = -1;
	struct vy_mem *mem, *next_mem;
	rlist_foreach_entry_safe(mem, &lsm->sealed, in_sealed, next_mem) {
		if (mem->generation > scheduler->dump_generation)
			continue;
		vy_mem_wait_pinned(mem);
		if (mem->tree.size == 0) {
			/*
			 * The tree is empty so we can delete it
			 * right away, without involving a worker.
			 */
			vy_lsm_delete_mem(lsm, mem);
			continue;
		}
		dump_lsn = MAX(dump_lsn, mem->max_lsn);
	}

	if (dump_lsn < 0) {
		/* Nothing to do, pick another LSM tree. */
		vy_scheduler_update_lsm(scheduler, lsm);
		vy_scheduler_complete_dump(scheduler);
		return 0;
	}

	struct vy_task *task = vy_task_new(&scheduler->task_pool,
					   lsm, &dump_ops);
	if (task == NULL)
		goto err;

	struct vy_run *new_run = vy_run_prepare(scheduler->run_env, lsm);
	if (new_run == NULL)
		goto err_run;

	new_run->dump_lsn = dump_lsn;

	struct vy_stmt_stream *wi;
	bool is_last_level = (lsm->run_count == 0);
	wi = vy_write_iterator_new(task->cmp_def, lsm->disk_format,
				   lsm->index_id == 0, is_last_level,
				   scheduler->read_views);
	if (wi == NULL)
		goto err_wi;
	rlist_foreach_entry(mem, &lsm->sealed, in_sealed) {
		if (mem->generation > scheduler->dump_generation)
			continue;
		if (vy_write_iterator_new_mem(wi, mem) != 0)
			goto err_wi_sub;
	}

	task->new_run = new_run;
	task->wi = wi;
	task->bloom_fpr = lsm->opts.bloom_fpr;
	task->page_size = lsm->opts.page_size;

	lsm->is_dumping = true;
	vy_scheduler_update_lsm(scheduler, lsm);

	if (lsm->index_id != 0) {
		/*
		 * The primary index LSM tree must be dumped after
		 * all secondary index LSM trees of the same space,
		 * see vy_dump_heap_less(). To make sure it isn't
		 * picked by the scheduler while all secondary index
		 * LSM trees are being dumped, temporarily remove
		 * it from the dump heap.
		 */
		vy_scheduler_pin_lsm(scheduler, lsm->pk);
	}

	scheduler->dump_task_count++;

	say_info("%s: dump started", vy_lsm_name(lsm));
	*p_task = task;
	return 0;

err_wi_sub:
	task->wi->iface->close(wi);
err_wi:
	vy_run_discard(new_run);
err_run:
	vy_task_delete(&scheduler->task_pool, task);
err:
	diag_log();
	say_error("%s: could not start dump", vy_lsm_name(lsm));
	return -1;
}

static int
vy_task_compact_execute(struct vy_scheduler *scheduler, struct vy_task *task)
{
	return vy_task_write_run(scheduler, task);
}

static int
vy_task_compact_complete(struct vy_scheduler *scheduler, struct vy_task *task)
{
	struct vy_lsm *lsm = task->lsm;
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
		new_slice = vy_slice_new(vy_log_next_id(), new_run,
					 NULL, NULL, lsm->cmp_def);
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
		if (run->compacted_slice_count == run->slice_count)
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
		vy_log_create_run(lsm->id, new_run->id, new_run->dump_lsn);
		vy_log_insert_slice(range->id, new_run->id, new_slice->id,
				    tuple_data_or_null(new_slice->begin),
				    tuple_data_or_null(new_slice->end));
	}
	if (vy_log_tx_commit() < 0) {
		if (new_slice != NULL)
			vy_slice_delete(new_slice);
		return -1;
	}

	if (gc_lsn < 0) {
		/*
		 * If there is no last snapshot, i.e. we are in
		 * the middle of join, we can delete compacted
		 * run files right away.
		 */
		vy_log_tx_begin();
		rlist_foreach_entry(run, &unused_runs, in_unused) {
			if (vy_run_remove_files(lsm->env->path, lsm->space_id,
						lsm->index_id, run->id) == 0) {
				vy_log_forget_run(run->id);
			}
		}
		vy_log_tx_try_commit();
	}

	/*
	 * Account the new run if it is not empty,
	 * otherwise discard it.
	 */
	if (new_slice != NULL) {
		vy_lsm_add_run(lsm, new_run);
		vy_stmt_counter_add_disk(&lsm->stat.disk.compact.out,
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
	vy_lsm_unacct_range(lsm, range);
	if (new_slice != NULL)
		vy_range_add_slice_before(range, new_slice, first_slice);
	for (slice = first_slice; ; slice = next_slice) {
		next_slice = rlist_next_entry(slice, in_range);
		vy_range_remove_slice(range, slice);
		rlist_add_entry(&compacted_slices, slice, in_range);
		vy_stmt_counter_add_disk(&lsm->stat.disk.compact.in,
					 &slice->count);
		if (slice == last_slice)
			break;
	}
	range->n_compactions++;
	range->version++;
	vy_lsm_acct_range(lsm, range);
	vy_range_update_compact_priority(range, &lsm->opts);
	lsm->stat.disk.compact.count++;

	/*
	 * Unaccount unused runs and delete compacted slices.
	 */
	rlist_foreach_entry(run, &unused_runs, in_unused)
		vy_lsm_remove_run(lsm, run);
	rlist_foreach_entry_safe(slice, &compacted_slices,
				 in_range, next_slice) {
		vy_slice_wait_pinned(slice);
		vy_slice_delete(slice);
	}

	/* The iterator has been cleaned up in worker. */
	task->wi->iface->close(task->wi);

	assert(range->heap_node.pos == UINT32_MAX);
	vy_range_heap_insert(&lsm->range_heap, &range->heap_node);
	vy_scheduler_update_lsm(scheduler, lsm);

	say_info("%s: completed compacting range %s",
		 vy_lsm_name(lsm), vy_range_str(range));
	return 0;
}

static void
vy_task_compact_abort(struct vy_scheduler *scheduler, struct vy_task *task,
		      bool in_shutdown)
{
	struct vy_lsm *lsm = task->lsm;
	struct vy_range *range = task->range;

	/* The iterator has been cleaned up in worker. */
	task->wi->iface->close(task->wi);

	/*
	 * It's no use alerting the user if the server is
	 * shutting down or the LSM tree was dropped.
	 */
	if (!in_shutdown && !lsm->is_dropped) {
		struct error *e = diag_last_error(&task->diag);
		error_log(e);
		say_error("%s: failed to compact range %s",
			  vy_lsm_name(lsm), vy_range_str(range));
	}

	/* The metadata log is unavailable on shutdown. */
	if (!in_shutdown)
		vy_run_discard(task->new_run);
	else
		vy_run_unref(task->new_run);

	assert(range->heap_node.pos == UINT32_MAX);
	vy_range_heap_insert(&lsm->range_heap, &range->heap_node);
	vy_scheduler_update_lsm(scheduler, lsm);
}

static int
vy_task_compact_new(struct vy_scheduler *scheduler, struct vy_lsm *lsm,
		    struct vy_task **p_task)
{
	static struct vy_task_ops compact_ops = {
		.execute = vy_task_compact_execute,
		.complete = vy_task_compact_complete,
		.abort = vy_task_compact_abort,
	};

	struct heap_node *range_node;
	struct vy_range *range;

	assert(!lsm->is_dropped);

	range_node = vy_range_heap_top(&lsm->range_heap);
	assert(range_node != NULL);
	range = container_of(range_node, struct vy_range, heap_node);
	assert(range->compact_priority > 1);

	if (vy_lsm_split_range(lsm, range) ||
	    vy_lsm_coalesce_range(lsm, range)) {
		vy_scheduler_update_lsm(scheduler, lsm);
		return 0;
	}

	struct vy_task *task = vy_task_new(&scheduler->task_pool,
					   lsm, &compact_ops);
	if (task == NULL)
		goto err_task;

	struct vy_run *new_run = vy_run_prepare(scheduler->run_env, lsm);
	if (new_run == NULL)
		goto err_run;

	struct vy_stmt_stream *wi;
	bool is_last_level = (range->compact_priority == range->slice_count);
	wi = vy_write_iterator_new(task->cmp_def, lsm->disk_format,
				   lsm->index_id == 0, is_last_level,
				   scheduler->read_views);
	if (wi == NULL)
		goto err_wi;

	struct vy_slice *slice;
	int n = range->compact_priority;
	rlist_foreach_entry(slice, &range->slices, in_range) {
		if (vy_write_iterator_new_slice(wi, slice) != 0)
			goto err_wi_sub;
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
	task->bloom_fpr = lsm->opts.bloom_fpr;
	task->page_size = lsm->opts.page_size;

	/*
	 * Remove the range we are going to compact from the heap
	 * so that it doesn't get selected again.
	 */
	vy_range_heap_delete(&lsm->range_heap, range_node);
	range_node->pos = UINT32_MAX;
	vy_scheduler_update_lsm(scheduler, lsm);

	say_info("%s: started compacting range %s, runs %d/%d",
		 vy_lsm_name(lsm), vy_range_str(range),
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
	diag_log();
	say_error("%s: could not start compacting range %s: %s",
		  vy_lsm_name(lsm), vy_range_str(range));
	return -1;
}

/**
 * Create a task for dumping an LSM tree. The new task is returned
 * in @ptask. If there's no LSM tree that needs to be dumped @ptask
 * is set to NULL.
 *
 * We only dump an LSM tree if it needs to be snapshotted or the quota
 * on memory usage is exceeded. In either case, the oldest LSM tree
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
	assert(scheduler->dump_generation <= scheduler->generation);
	if (scheduler->dump_generation == scheduler->generation) {
		/*
		 * All memory trees of past generations have
		 * been dumped, nothing to do.
		 */
		return 0;
	}
	/*
	 * Look up the oldest LSM tree eligible for dump.
	 */
	struct heap_node *pn = vy_dump_heap_top(&scheduler->dump_heap);
	if (pn == NULL) {
		/*
		 * There is no LSM tree and so no task to schedule.
		 * Complete the current dump round.
		 */
		vy_scheduler_complete_dump(scheduler);
		return 0;
	}
	struct vy_lsm *lsm = container_of(pn, struct vy_lsm, in_dump);
	if (!lsm->is_dumping && lsm->pin_count == 0 &&
	    vy_lsm_generation(lsm) == scheduler->dump_generation) {
		/*
		 * Dump is in progress and there is an LSM tree that
		 * contains data that must be dumped at the current
		 * round. Try to create a task for it.
		 */
		if (vy_task_dump_new(scheduler, lsm, ptask) != 0)
			return -1;
		if (*ptask != NULL)
			return 0; /* new task */
		/*
		 * All in-memory trees eligible for dump were empty
		 * and so were deleted without involving a worker
		 * thread. Check another LSM tree.
		 */
		goto retry;
	}
	/*
	 * Dump is in progress, but all eligible LSM trees are
	 * already being dumped. Wait until the current round
	 * is complete.
	 */
	assert(scheduler->dump_task_count > 0);
	return 0;
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
	struct vy_lsm *lsm = container_of(pn, struct vy_lsm, in_compact);
	if (vy_lsm_compact_priority(lsm) <= 1)
		return 0; /* nothing to do */
	if (vy_task_compact_new(scheduler, lsm, ptask) != 0)
		return -1;
	if (*ptask == NULL)
		goto retry; /* LSM tree dropped or range split/coalesced */
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
	if (task->lsm->is_dropped) {
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

	/*
	 * Yield immediately, until the quota watermark is reached
	 * for the first time or a checkpoint is made.
	 * Then start the worker threads: we know they will be
	 * needed. If quota watermark is never reached, workers
	 * are not started and the scheduler is idle until
	 * shutdown or checkpoint.
	 */
	fiber_cond_wait(&scheduler->scheduler_cond);
	if (scheduler->scheduler_fiber == NULL)
		return 0; /* destroyed */

	vy_scheduler_start_workers(scheduler);

	while (scheduler->scheduler_fiber != NULL) {
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

static void *
vy_worker_f(void *arg)
{
	struct vy_scheduler *scheduler = arg;
	struct vy_task *task = NULL;

	tt_pthread_mutex_lock(&scheduler->mutex);
	while (scheduler->is_worker_pool_running) {
		/* Wait for a task */
		if (stailq_empty(&scheduler->input_queue)) {
			/* Wake scheduler up if there are no more tasks */
			ev_async_send(scheduler->scheduler_loop,
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
		task->status = task->ops->execute(scheduler, task);
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
	return NULL;
}
