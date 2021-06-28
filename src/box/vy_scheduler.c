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
#include <small/rlist.h>
#include <tarantool_ev.h>

#include "diag.h"
#include "errcode.h"
#include "errinj.h"
#include "fiber.h"
#include "fiber_cond.h"
#include "cbus.h"
#include "salad/stailq.h"
#include "say.h"
#include "txn.h"
#include "space.h"
#include "schema.h"
#include "xrow.h"
#include "vy_lsm.h"
#include "vy_log.h"
#include "vy_mem.h"
#include "vy_range.h"
#include "vy_run.h"
#include "vy_write_iterator.h"
#include "trivia/util.h"

/* Min and max values for vy_scheduler::timeout. */
#define VY_SCHEDULER_TIMEOUT_MIN	1
#define VY_SCHEDULER_TIMEOUT_MAX	60

static int vy_worker_f(va_list);
static int vy_scheduler_f(va_list);
static void vy_task_execute_f(struct cmsg *);
static void vy_task_complete_f(struct cmsg *);
static void vy_deferred_delete_batch_process_f(struct cmsg *);
static void vy_deferred_delete_batch_free_f(struct cmsg *);

static const struct cmsg_hop vy_task_execute_route[] = {
	{ vy_task_execute_f, NULL },
};

static const struct cmsg_hop vy_task_complete_route[] = {
	{ vy_task_complete_f, NULL },
};

struct vy_task;

/** Vinyl worker thread. */
struct vy_worker {
	struct cord cord;
	/** Pipe from tx to the worker thread. */
	struct cpipe worker_pipe;
	/** Pipe from the worker thread to tx. */
	struct cpipe tx_pipe;
	/** Pool this worker was allocated from. */
	struct vy_worker_pool *pool;
	/**
	 * Task that is currently being executed by the worker
	 * or NULL if the worker is idle.
	 */
	struct vy_task *task;
	/** Link in vy_worker_pool::idle_workers. */
	struct stailq_entry in_idle;
	/** Route for sending deferred DELETEs back to tx. */
	struct cmsg_hop deferred_delete_route[2];
};

/** Max number of statements in a batch of deferred DELETEs. */
enum { VY_DEFERRED_DELETE_BATCH_MAX = 100 };

/** Deferred DELETE statement. */
struct vy_deferred_delete_stmt {
	/** Overwritten tuple. */
	struct tuple *old_stmt;
	/** Statement that overwrote @old_stmt. */
	struct tuple *new_stmt;
};

/**
 * Batch of deferred DELETE statements generated during
 * a primary index compaction.
 */
struct vy_deferred_delete_batch {
	/** CBus messages for sending the batch to tx. */
	struct cmsg cmsg;
	/** Task that generated this batch. */
	struct vy_task *task;
	/** Set if the tx thread failed to process the batch. */
	bool is_failed;
	/** In case of failure the error is stored here. */
	struct diag diag;
	/** Number of elements actually stored in @stmt array. */
	int count;
	/** Array of deferred DELETE statements. */
	struct vy_deferred_delete_stmt stmt[VY_DEFERRED_DELETE_BATCH_MAX];
};

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
	 * the LSM tree when preparing the task.
	 */
	void (*abort)(struct vy_task *task);
};

struct vy_task {
	/**
	 * CBus message used for sending the task to/from
	 * a worker thread.
	 */
	struct cmsg cmsg;
	/** Virtual method table. */
	const struct vy_task_ops *ops;
	/** Pointer to the scheduler. */
	struct vy_scheduler *scheduler;
	/** Worker thread this task is assigned to. */
	struct vy_worker *worker;
	/**
	 * Fiber that is currently executing this task in
	 * a worker thread.
	 */
	struct fiber *fiber;
	/** Time of the task creation. */
	double start_time;
	/** Set if the task failed. */
	bool is_failed;
	/** In case of task failure the error is stored here. */
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
	 * Index options may be modified while a task is in
	 * progress so we save them here to safely access them
	 * from another thread.
	 */
	double bloom_fpr;
	int64_t page_size;
	/**
	 * Deferred DELETE handler passed to the write iterator.
	 * It sends deferred DELETE statements generated during
	 * primary index compaction back to tx.
	 */
	struct vy_deferred_delete_handler deferred_delete_handler;
	/** Batch of deferred deletes generated by this task. */
	struct vy_deferred_delete_batch *deferred_delete_batch;
	/**
	 * Number of batches of deferred DELETEs sent to tx
	 * and not yet processed.
	 */
	int deferred_delete_in_progress;
	/** Link in vy_scheduler::processed_tasks. */
	struct stailq_entry in_processed;
};

static const struct vy_deferred_delete_handler_iface
vy_task_deferred_delete_iface;

/**
 * Allocate a new task to be executed by a worker thread.
 * When preparing an asynchronous task, this function must
 * be called before yielding the current fiber in order to
 * pin the LSM tree the task is for so that a concurrent fiber
 * does not free it from under us.
 */
static struct vy_task *
vy_task_new(struct vy_scheduler *scheduler, struct vy_worker *worker,
	    struct vy_lsm *lsm, const struct vy_task_ops *ops)
{
	struct vy_task *task = calloc(1, sizeof(*task));
	if (task == NULL) {
		diag_set(OutOfMemory, sizeof(*task),
			 "malloc", "struct vy_task");
		return NULL;
	}
	memset(task, 0, sizeof(*task));
	task->ops = ops;
	task->scheduler = scheduler;
	task->worker = worker;
	task->start_time = ev_monotonic_now(loop());
	task->lsm = lsm;
	task->cmp_def = key_def_dup(lsm->cmp_def);
	if (task->cmp_def == NULL) {
		free(task);
		return NULL;
	}
	task->key_def = key_def_dup(lsm->key_def);
	if (task->key_def == NULL) {
		key_def_delete(task->cmp_def);
		free(task);
		return NULL;
	}
	vy_lsm_ref(lsm);
	diag_create(&task->diag);
	task->deferred_delete_handler.iface = &vy_task_deferred_delete_iface;
	return task;
}

/** Free a task allocated with vy_task_new(). */
static void
vy_task_delete(struct vy_task *task)
{
	assert(task->deferred_delete_batch == NULL);
	assert(task->deferred_delete_in_progress == 0);
	key_def_delete(task->cmp_def);
	key_def_delete(task->key_def);
	vy_lsm_unref(task->lsm);
	diag_destroy(&task->diag);
	free(task);
}

static bool
vy_dump_heap_less(struct vy_lsm *i1, struct vy_lsm *i2)
{
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
#define heap_value_t struct vy_lsm
#define heap_value_attr in_dump

#include "salad/heap.h"

static bool
vy_compaction_heap_less(struct vy_lsm *i1, struct vy_lsm *i2)
{
	/*
	 * Prefer LSM trees whose read amplification will be reduced
	 * most as a result of compaction.
	 */
	return vy_lsm_compaction_priority(i1) > vy_lsm_compaction_priority(i2);
}

#define HEAP_NAME vy_compaction_heap
#define HEAP_LESS(h, l, r) vy_compaction_heap_less(l, r)
#define heap_value_t struct vy_lsm
#define heap_value_attr in_compaction

#include "salad/heap.h"

static void
vy_worker_pool_start(struct vy_worker_pool *pool)
{
	assert(pool->workers == NULL);

	pool->workers = calloc(pool->size, sizeof(*pool->workers));
	if (pool->workers == NULL)
		panic("failed to allocate vinyl worker pool");

	for (int i = 0; i < pool->size; i++) {
		char name[FIBER_NAME_MAX];
		snprintf(name, sizeof(name), "vinyl.%s.%d", pool->name, i);
		struct vy_worker *worker = &pool->workers[i];
		if (cord_costart(&worker->cord, name, vy_worker_f, worker) != 0)
			panic("failed to start vinyl worker thread");

		worker->pool = pool;
		cpipe_create(&worker->worker_pipe, name);
		stailq_add_tail_entry(&pool->idle_workers, worker, in_idle);

		struct cmsg_hop *route = worker->deferred_delete_route;
		route[0].f = vy_deferred_delete_batch_process_f;
		route[0].pipe = &worker->worker_pipe;
		route[1].f = vy_deferred_delete_batch_free_f;
		route[1].pipe = NULL;
	}
}

static void
vy_worker_pool_stop(struct vy_worker_pool *pool)
{
	assert(pool->workers != NULL);
	for (int i = 0; i < pool->size; i++) {
		struct vy_worker *worker = &pool->workers[i];
		tt_pthread_cancel(worker->cord.id);
		tt_pthread_join(worker->cord.id, NULL);
	}
	free(pool->workers);
	pool->workers = NULL;
}

static void
vy_worker_pool_create(struct vy_worker_pool *pool, const char *name, int size)
{
	pool->name = name;
	pool->size = size;
	pool->workers = NULL;
	stailq_create(&pool->idle_workers);
}

static void
vy_worker_pool_destroy(struct vy_worker_pool *pool)
{
	if (pool->workers != NULL)
		vy_worker_pool_stop(pool);
}

/**
 * Get an idle worker from a pool.
 */
static struct vy_worker *
vy_worker_pool_get(struct vy_worker_pool *pool)
{
	/*
	 * Start worker threads only when a task is scheduled
	 * so that they are not dangling around if vinyl is
	 * not used.
	 */
	if (pool->workers == NULL)
		vy_worker_pool_start(pool);

	struct vy_worker *worker = NULL;
	if (!stailq_empty(&pool->idle_workers)) {
		worker = stailq_shift_entry(&pool->idle_workers,
					    struct vy_worker, in_idle);
		assert(worker->pool == pool);
	}
	return worker;
}

/**
 * Put a worker back to the pool it was allocated from once
 * it's done its job.
 */
static void
vy_worker_pool_put(struct vy_worker *worker)
{
	struct vy_worker_pool *pool = worker->pool;
	stailq_add_entry(&pool->idle_workers, worker, in_idle);
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

	fiber_cond_create(&scheduler->scheduler_cond);

	/*
	 * Dump tasks must be scheduled as soon as possible,
	 * otherwise we may run out of memory quota and have
	 * to stall transactions. To avoid unpredictably long
	 * stalls caused by ongoing compaction tasks blocking
	 * scheduling of dump tasks, we use separate thread
	 * pools for dump and compaction tasks.
	 *
	 * Since a design based on LSM trees typically implies
	 * high write amplification, we allocate only 1/4th of
	 * all available threads to dump tasks while the rest
	 * is used exclusively for compaction.
	 */
	assert(write_threads > 1);
	int dump_threads = MAX(1, write_threads / 4);
	int compaction_threads = write_threads - dump_threads;
	vy_worker_pool_create(&scheduler->dump_pool,
			      "dump", dump_threads);
	vy_worker_pool_create(&scheduler->compaction_pool,
			      "compaction", compaction_threads);

	stailq_create(&scheduler->processed_tasks);

	vy_dump_heap_create(&scheduler->dump_heap);
	vy_compaction_heap_create(&scheduler->compaction_heap);

	diag_create(&scheduler->diag);
	fiber_cond_create(&scheduler->dump_cond);
}

void
vy_scheduler_start(struct vy_scheduler *scheduler)
{
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

	vy_worker_pool_destroy(&scheduler->dump_pool);
	vy_worker_pool_destroy(&scheduler->compaction_pool);
	diag_destroy(&scheduler->diag);
	fiber_cond_destroy(&scheduler->dump_cond);
	fiber_cond_destroy(&scheduler->scheduler_cond);
	vy_dump_heap_destroy(&scheduler->dump_heap);
	vy_compaction_heap_destroy(&scheduler->compaction_heap);

	TRASH(scheduler);
}

void
vy_scheduler_reset_stat(struct vy_scheduler *scheduler)
{
	struct vy_scheduler_stat *stat = &scheduler->stat;
	stat->tasks_completed = 0;
	stat->tasks_failed = 0;
	stat->dump_count = 0;
	stat->dump_time = 0;
	stat->dump_input = 0;
	stat->dump_output = 0;
	stat->compaction_time = 0;
	stat->compaction_input = 0;
	stat->compaction_output = 0;
}

static int
vy_scheduler_on_delete_lsm(struct trigger *trigger, void *event)
{
	struct vy_lsm *lsm = event;
	struct vy_scheduler *scheduler = trigger->data;
	assert(! heap_node_is_stray(&lsm->in_dump));
	assert(! heap_node_is_stray(&lsm->in_compaction));
	vy_dump_heap_delete(&scheduler->dump_heap, lsm);
	vy_compaction_heap_delete(&scheduler->compaction_heap, lsm);
	trigger_clear(trigger);
	free(trigger);
	return 0;
}

int
vy_scheduler_add_lsm(struct vy_scheduler *scheduler, struct vy_lsm *lsm)
{
	assert(heap_node_is_stray(&lsm->in_dump));
	assert(heap_node_is_stray(&lsm->in_compaction));
	/*
	 * Register a trigger that will remove this LSM tree from
	 * the scheduler queues on destruction.
	 */
	struct trigger *trigger = malloc(sizeof(*trigger));
	if (trigger == NULL) {
		diag_set(OutOfMemory, sizeof(*trigger), "malloc", "trigger");
		return -1;
	}
	trigger_create(trigger, vy_scheduler_on_delete_lsm, scheduler, NULL);
	trigger_add(&lsm->on_destroy, trigger);
	/*
	 * Add this LSM tree to the scheduler queues so that it
	 * can be dumped and compacted in a timely manner.
	 */
	vy_dump_heap_insert(&scheduler->dump_heap, lsm);
	vy_compaction_heap_insert(&scheduler->compaction_heap, lsm);
	return 0;
}

static void
vy_scheduler_update_lsm(struct vy_scheduler *scheduler, struct vy_lsm *lsm)
{
	assert(! heap_node_is_stray(&lsm->in_dump));
	assert(! heap_node_is_stray(&lsm->in_compaction));
	vy_dump_heap_update(&scheduler->dump_heap, lsm);
	vy_compaction_heap_update(&scheduler->compaction_heap, lsm);
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
	if (vy_scheduler_dump_in_progress(scheduler)) {
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

int
vy_scheduler_dump(struct vy_scheduler *scheduler)
{
	/*
	 * We must not start dump if checkpoint is in progress
	 * so first wait for checkpoint to complete.
	 */
	while (scheduler->checkpoint_in_progress)
		fiber_cond_wait(&scheduler->dump_cond);

	/* Trigger dump. */
	if (!vy_scheduler_dump_in_progress(scheduler))
		scheduler->dump_start = ev_monotonic_now(loop());
	scheduler->generation++;
	fiber_cond_signal(&scheduler->scheduler_cond);

	/* Wait for dump to complete. */
	while (vy_scheduler_dump_in_progress(scheduler)) {
		if (scheduler->is_throttled) {
			/* Dump error occurred. */
			struct error *e = diag_last_error(&scheduler->diag);
			diag_set_error(diag_get(), e);
			return -1;
		}
		fiber_cond_wait(&scheduler->dump_cond);
	}
	return 0;
}

void
vy_scheduler_force_compaction(struct vy_scheduler *scheduler,
			      struct vy_lsm *lsm)
{
	vy_lsm_force_compaction(lsm);
	vy_scheduler_update_lsm(scheduler, lsm);
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
	struct vy_lsm *lsm = vy_dump_heap_top(&scheduler->dump_heap);
	if (lsm != NULL)
		min_generation = vy_lsm_generation(lsm);
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
	scheduler->stat.dump_count++;
	scheduler->dump_complete_cb(scheduler,
			min_generation - 1, dump_duration);
	fiber_cond_signal(&scheduler->dump_cond);
}

int
vy_scheduler_begin_checkpoint(struct vy_scheduler *scheduler, bool is_scheduled)
{
	assert(!scheduler->checkpoint_in_progress);

	/*
	 * If checkpoint is manually launched (via box.snapshot())
	 * then ignore throttling and force dump process. Otherwise,
	 * if the scheduler is throttled due to errors, do not wait
	 * until it wakes up as it may take quite a while. Instead
	 * fail checkpoint immediately with the last error seen by
	 * the scheduler.
	 */
	if (scheduler->is_throttled) {
		if (is_scheduled) {
			struct error *e = diag_last_error(&scheduler->diag);
			diag_set_error(diag_get(), e);
			say_error("cannot checkpoint vinyl, "
				  "scheduler is throttled with: %s", e->errmsg);
			return -1;
		}
		say_info("scheduler is unthrottled due to manual checkpoint "
			 "process");
		scheduler->is_throttled = false;
	}

	if (!vy_scheduler_dump_in_progress(scheduler)) {
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
	while (vy_scheduler_dump_in_progress(scheduler)) {
		if (scheduler->is_throttled) {
			/* A dump error occurred, abort checkpoint. */
			struct error *e = diag_last_error(&scheduler->diag);
			diag_set_error(diag_get(), e);
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

/**
 * Encode and write a single deferred DELETE statement to
 * _vinyl_deferred_delete system space. The rest will be
 * done by the space trigger.
 */
static int
vy_deferred_delete_process_one(struct space *deferred_delete_space,
			       uint32_t space_id, struct tuple_format *format,
			       struct vy_deferred_delete_stmt *stmt)
{
	int64_t lsn = vy_stmt_lsn(stmt->new_stmt);

	struct tuple *delete;
	delete = vy_stmt_new_surrogate_delete(format, stmt->old_stmt);
	if (delete == NULL)
		return -1;

	uint32_t delete_data_size;
	const char *delete_data = tuple_data_range(delete, &delete_data_size);

	size_t buf_size = (mp_sizeof_array(3) + mp_sizeof_uint(space_id) +
			   mp_sizeof_uint(lsn) + delete_data_size);
	char *data = region_alloc(&fiber()->gc, buf_size);
	if (data == NULL) {
		diag_set(OutOfMemory, buf_size, "region", "buf");
		tuple_unref(delete);
		return -1;
	}

	char *data_end = data;
	data_end = mp_encode_array(data_end, 3);
	data_end = mp_encode_uint(data_end, space_id);
	data_end = mp_encode_uint(data_end, lsn);
	memcpy(data_end, delete_data, delete_data_size);
	data_end += delete_data_size;
	assert(data_end <= data + buf_size);

	struct request request;
	memset(&request, 0, sizeof(request));
	request.type = IPROTO_REPLACE;
	request.space_id = BOX_VINYL_DEFERRED_DELETE_ID;
	request.tuple = data;
	request.tuple_end = data_end;

	tuple_unref(delete);

	struct txn *txn = in_txn();
	if (txn_begin_stmt(txn, deferred_delete_space, request.type) != 0)
		return -1;

	struct tuple *unused;
	if (space_execute_dml(deferred_delete_space, txn,
			      &request, &unused) != 0) {
		txn_rollback_stmt(txn);
		return -1;
	}
	return txn_commit_stmt(txn, &request);
}

/**
 * Callback invoked by the tx thread to process deferred DELETE
 * statements generated during compaction. It writes deferred
 * DELETEs to a special system space, _vinyl_deferred_delete.
 * The system space has an on_replace trigger installed which
 * propagates the DELETEs to secondary indexes. This way, even
 * if a deferred DELETE isn't dumped to disk by vinyl, it still
 * can be recovered from WAL.
 */
static void
vy_deferred_delete_batch_process_f(struct cmsg *cmsg)
{
	struct vy_deferred_delete_batch *batch = container_of(cmsg,
				struct vy_deferred_delete_batch, cmsg);
	struct vy_task *task = batch->task;
	struct vy_lsm *pk = task->lsm;

	assert(pk->index_id == 0);
	/*
	 * A space can be dropped while a compaction task
	 * is in progress.
	 */
	if (pk->is_dropped)
		return;

	struct space *deferred_delete_space;
	deferred_delete_space = space_by_id(BOX_VINYL_DEFERRED_DELETE_ID);
	assert(deferred_delete_space != NULL);

	struct txn *txn = txn_begin();
	if (txn == NULL)
		goto fail;

	for (int i = 0; i < batch->count; i++) {
		if (vy_deferred_delete_process_one(deferred_delete_space,
						   pk->space_id, pk->mem_format,
						   &batch->stmt[i]) != 0) {
			goto fail_rollback;
		}
	}

	if (txn_commit(txn) != 0)
		goto fail;
	fiber_gc();
	return;

fail_rollback:
	txn_abort(txn);
	fiber_gc();
fail:
	batch->is_failed = true;
	diag_move(diag_get(), &batch->diag);
}

/**
 * Callback invoked by a worker thread to free processed deferred
 * DELETE statements. It must be done on behalf the worker thread
 * that generated those DELETEs, because a vinyl statement cannot
 * be allocated and freed in different threads.
 */
static void
vy_deferred_delete_batch_free_f(struct cmsg *cmsg)
{
	struct vy_deferred_delete_batch *batch = container_of(cmsg,
				struct vy_deferred_delete_batch, cmsg);
	struct vy_task *task = batch->task;
	for (int i = 0; i < batch->count; i++) {
		struct vy_deferred_delete_stmt *stmt = &batch->stmt[i];
		vy_stmt_unref_if_possible(stmt->old_stmt);
		vy_stmt_unref_if_possible(stmt->new_stmt);
	}
	/*
	 * Abort the task if the tx thread failed to process
	 * the batch unless it has already been aborted.
	 */
	if (batch->is_failed && !task->is_failed) {
		assert(!diag_is_empty(&batch->diag));
		diag_move(&batch->diag, &task->diag);
		task->is_failed = true;
		fiber_cancel(task->fiber);
	}
	diag_destroy(&batch->diag);
	free(batch);
	/* Notify the caller if this is the last batch. */
	assert(task->deferred_delete_in_progress > 0);
	if (--task->deferred_delete_in_progress == 0)
		fiber_wakeup(task->fiber);
}

/**
 * Send all deferred DELETEs accumulated by a vinyl task to
 * the tx thread where they will be processed.
 */
static void
vy_task_deferred_delete_flush(struct vy_task *task)
{
	struct vy_worker *worker = task->worker;
	struct vy_deferred_delete_batch *batch = task->deferred_delete_batch;

	if (batch == NULL)
		return;

	task->deferred_delete_batch = NULL;
	task->deferred_delete_in_progress++;

	cmsg_init(&batch->cmsg, worker->deferred_delete_route);
	cpipe_push(&worker->tx_pipe, &batch->cmsg);
}

/**
 * Add a deferred DELETE to a batch. Once the batch gets full,
 * submit it to tx where it will get processed.
 */
static int
vy_task_deferred_delete_process(struct vy_deferred_delete_handler *handler,
				struct tuple *old_stmt, struct tuple *new_stmt)
{
	enum { MAX_IN_PROGRESS = 10 };

	struct vy_task *task = container_of(handler, struct vy_task,
					    deferred_delete_handler);
	struct vy_deferred_delete_batch *batch = task->deferred_delete_batch;

	/*
	 * Throttle compaction task if there are too many batches
	 * being processed so as to limit memory consumption.
	 */
	while (task->deferred_delete_in_progress >= MAX_IN_PROGRESS)
		fiber_sleep(TIMEOUT_INFINITY);

	/* Allocate a new batch on demand. */
	if (batch == NULL) {
		batch = malloc(sizeof(*batch));
		if (batch == NULL) {
			diag_set(OutOfMemory, sizeof(*batch), "malloc",
				 "struct vy_deferred_delete_batch");
			return -1;
		}
		memset(batch, 0, sizeof(*batch));
		batch->task = task;
		diag_create(&batch->diag);
		task->deferred_delete_batch = batch;
	}

	assert(batch->count < VY_DEFERRED_DELETE_BATCH_MAX);
	struct vy_deferred_delete_stmt *stmt = &batch->stmt[batch->count++];
	stmt->old_stmt = old_stmt;
	vy_stmt_ref_if_possible(old_stmt);
	stmt->new_stmt = new_stmt;
	vy_stmt_ref_if_possible(new_stmt);

	if (batch->count == VY_DEFERRED_DELETE_BATCH_MAX)
		vy_task_deferred_delete_flush(task);
	return 0;
}

/**
 * Wait until all pending deferred DELETE statements have been
 * processed by tx. Called when the write iterator stops.
 */
static void
vy_task_deferred_delete_destroy(struct vy_deferred_delete_handler *handler)
{
	struct vy_task *task = container_of(handler, struct vy_task,
					    deferred_delete_handler);
	vy_task_deferred_delete_flush(task);
	while (task->deferred_delete_in_progress > 0)
		fiber_sleep(TIMEOUT_INFINITY);
}

static const struct vy_deferred_delete_handler_iface
vy_task_deferred_delete_iface = {
	.process = vy_task_deferred_delete_process,
	.destroy = vy_task_deferred_delete_destroy,
};

static int
vy_task_write_run(struct vy_task *task, bool no_compression)
{
	enum { YIELD_LOOPS = 32 };

	struct vy_lsm *lsm = task->lsm;
	struct vy_stmt_stream *wi = task->wi;

	ERROR_INJECT(ERRINJ_VY_RUN_WRITE,
		     {diag_set(ClientError, ER_INJECTION,
			       "vinyl dump"); return -1;});
	ERROR_INJECT_SLEEP(ERRINJ_VY_RUN_WRITE_DELAY);

	struct vy_run_writer writer;
	if (vy_run_writer_create(&writer, task->new_run, lsm->env->path,
				 lsm->space_id, lsm->index_id,
				 task->cmp_def, task->key_def,
				 task->page_size, task->bloom_fpr,
				 no_compression) != 0)
		goto fail;

	if (wi->iface->start(wi) != 0)
		goto fail_abort_writer;
	int rc;
	int loops = 0;
	struct vy_entry entry = vy_entry_none();
	while ((rc = wi->iface->next(wi, &entry)) == 0 && entry.stmt != NULL) {
		struct errinj *inj = errinj(ERRINJ_VY_RUN_WRITE_STMT_TIMEOUT,
					    ERRINJ_DOUBLE);
		if (inj != NULL && inj->dparam > 0)
			thread_sleep(inj->dparam);

		rc = vy_run_writer_append_stmt(&writer, entry);
		if (rc != 0)
			break;

		if (++loops % YIELD_LOOPS == 0)
			fiber_sleep(0);
		if (fiber_is_cancelled()) {
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
vy_task_dump_execute(struct vy_task *task)
{
	ERROR_INJECT_SLEEP(ERRINJ_VY_DUMP_DELAY);
	/*
	 * Don't compress L1 runs as they are most frequently read
	 * and smallest runs at the same time and so we would gain
	 * nothing by compressing them.
	 */
	return vy_task_write_run(task, true);
}

static int
vy_task_dump_complete(struct vy_task *task)
{
	struct vy_scheduler *scheduler = task->scheduler;
	struct vy_lsm *lsm = task->lsm;
	struct vy_run *new_run = task->new_run;
	int64_t dump_lsn = new_run->dump_lsn;
	double dump_time = ev_monotonic_now(loop()) - task->start_time;
	struct vy_disk_stmt_counter dump_output = new_run->count;
	struct vy_stmt_counter dump_input;
	struct vy_mem *mem, *next_mem;
	struct vy_slice **new_slices, *slice;
	struct vy_range *range, *begin_range, *end_range;
	int i;

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

	assert(new_run->info.max_lsn <= dump_lsn);

	/*
	 * Figure out which ranges intersect the new run.
	 */
	if (vy_lsm_find_range_intersection(lsm, new_run->info.min_key,
					   new_run->info.max_key,
					   &begin_range, &end_range) != 0)
		goto fail;

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
	     range = vy_range_tree_next(&lsm->range_tree, range), i++) {
		slice = vy_slice_new(vy_log_next_id(), new_run,
				     range->begin, range->end, lsm->cmp_def);
		if (slice == NULL)
			goto fail_free_slices;

		assert(i < lsm->range_count);
		new_slices[i] = slice;
	}

	/*
	 * Log change in metadata.
	 */
	vy_log_tx_begin();
	vy_log_create_run(lsm->id, new_run->id, dump_lsn, new_run->dump_count);
	for (range = begin_range, i = 0; range != end_range;
	     range = vy_range_tree_next(&lsm->range_tree, range), i++) {
		assert(i < lsm->range_count);
		slice = new_slices[i];
		vy_log_insert_slice(range->id, new_run->id, slice->id,
				    tuple_data_or_null(slice->begin.stmt),
				    tuple_data_or_null(slice->end.stmt));
	}
	vy_log_dump_lsm(lsm->id, dump_lsn);
	if (vy_log_tx_commit() < 0)
		goto fail_free_slices;

	/* Account the new run. */
	vy_lsm_add_run(lsm, new_run);
	/* Drop the reference held by the task. */
	vy_run_unref(new_run);

	/*
	 * Add new slices to ranges.
	 *
	 * Note, we must not yield after this point, because if we
	 * do, a concurrent read iterator may see an inconsistent
	 * LSM tree state, when the same statement is present twice,
	 * in memory and on disk.
	 */
	for (range = begin_range, i = 0; range != end_range;
	     range = vy_range_tree_next(&lsm->range_tree, range), i++) {
		assert(i < lsm->range_count);
		slice = new_slices[i];
		vy_lsm_unacct_range(lsm, range);
		vy_range_add_slice(range, slice);
		vy_range_update_compaction_priority(range, &lsm->opts);
		vy_range_update_dumps_per_compaction(range);
		vy_lsm_acct_range(lsm, range);
	}
	vy_range_heap_update_all(&lsm->range_heap);
	free(new_slices);

delete_mems:
	/*
	 * Delete dumped in-memory trees and account dump in
	 * LSM tree statistics.
	 */
	vy_stmt_counter_reset(&dump_input);
	rlist_foreach_entry_safe(mem, &lsm->sealed, in_sealed, next_mem) {
		if (mem->generation > scheduler->dump_generation)
			continue;
		vy_stmt_counter_add(&dump_input, &mem->count);
		vy_lsm_delete_mem(lsm, mem);
	}
	lsm->dump_lsn = MAX(lsm->dump_lsn, dump_lsn);
	vy_lsm_acct_dump(lsm, dump_time, &dump_input, &dump_output);
	/*
	 * Indexes of the same space share a memory level so we
	 * account dump input only when the primary index is dumped.
	 */
	if (lsm->index_id == 0)
		scheduler->stat.dump_input += dump_input.bytes;
	scheduler->stat.dump_output += dump_output.bytes;
	scheduler->stat.dump_time += dump_time;

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
	}
	free(new_slices);
fail:
	return -1;
}

static void
vy_task_dump_abort(struct vy_task *task)
{
	struct vy_scheduler *scheduler = task->scheduler;
	struct vy_lsm *lsm = task->lsm;

	assert(lsm->is_dumping);

	/* The iterator has been cleaned up in a worker thread. */
	task->wi->iface->close(task->wi);

	struct error *e = diag_last_error(&task->diag);
	error_log(e);
	say_error("%s: dump failed", vy_lsm_name(lsm));

	vy_run_discard(task->new_run);

	lsm->is_dumping = false;
	vy_scheduler_update_lsm(scheduler, lsm);

	if (lsm->index_id != 0)
		vy_scheduler_unpin_lsm(scheduler, lsm->pk);

	assert(scheduler->dump_task_count > 0);
	scheduler->dump_task_count--;
}

/**
 * Create a task to dump an LSM tree.
 *
 * On success the task is supposed to dump all in-memory
 * trees created at @scheduler->dump_generation.
 */
static int
vy_task_dump_new(struct vy_scheduler *scheduler, struct vy_worker *worker,
		 struct vy_lsm *lsm, struct vy_task **p_task)
{
	static struct vy_task_ops dump_ops = {
		.execute = vy_task_dump_execute,
		.complete = vy_task_dump_complete,
		.abort = vy_task_dump_abort,
	};

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
		dump_lsn = MAX(dump_lsn, mem->dump_lsn);
	}

	if (dump_lsn < 0) {
		/* Nothing to do, pick another LSM tree. */
		vy_scheduler_update_lsm(scheduler, lsm);
		vy_scheduler_complete_dump(scheduler);
		return 0;
	}

	struct vy_task *task = vy_task_new(scheduler, worker, lsm, &dump_ops);
	if (task == NULL)
		goto err;

	struct vy_run *new_run = vy_run_prepare(scheduler->run_env, lsm);
	if (new_run == NULL)
		goto err_run;

	new_run->dump_count = 1;
	new_run->dump_lsn = dump_lsn;

	/*
	 * Note, since deferred DELETE are generated on tx commit
	 * in case the overwritten tuple is found in-memory, no
	 * deferred DELETE statement should be generated during
	 * dump so we don't pass a deferred DELETE handler.
	 */
	struct vy_stmt_stream *wi;
	bool is_last_level = (lsm->run_count == 0);
	wi = vy_write_iterator_new(task->cmp_def, lsm->index_id == 0,
				   is_last_level, scheduler->read_views, NULL);
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
	vy_task_delete(task);
err:
	diag_log();
	say_error("%s: could not start dump", vy_lsm_name(lsm));
	return -1;
}

static int
vy_task_compaction_execute(struct vy_task *task)
{
	ERROR_INJECT_SLEEP(ERRINJ_VY_COMPACTION_DELAY);
	return vy_task_write_run(task, false);
}

static int
vy_task_compaction_complete(struct vy_task *task)
{
	struct vy_scheduler *scheduler = task->scheduler;
	struct vy_lsm *lsm = task->lsm;
	struct vy_range *range = task->range;
	struct vy_run *new_run = task->new_run;
	double compaction_time = ev_monotonic_now(loop()) - task->start_time;
	struct vy_disk_stmt_counter compaction_output = new_run->count;
	struct vy_disk_stmt_counter compaction_input;
	struct vy_slice *first_slice = task->first_slice;
	struct vy_slice *last_slice = task->last_slice;
	struct vy_slice *slice, *next_slice, *new_slice = NULL;
	struct vy_run *run;

	/*
	 * The LSM tree could have been dropped while we were writing the new
	 * run. In this case we should discard the run without committing to
	 * vylog, because all the information about the LSM tree and its runs
	 * could have already been garbage collected from vylog.
	 */
	if (lsm->is_dropped) {
		vy_run_unref(new_run);
		goto out;
	}

	/*
	 * Allocate a slice of the new run.
	 *
	 * If the run is empty, we don't need to allocate a new slice
	 * and insert it into the range, but we still need to delete
	 * compacted runs.
	 */
	if (!vy_run_is_empty(new_run)) {
		new_slice = vy_slice_new(vy_log_next_id(), new_run,
					 vy_entry_none(), vy_entry_none(),
					 lsm->cmp_def);
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
	rlist_foreach_entry(run, &unused_runs, in_unused)
		vy_log_drop_run(run->id, VY_LOG_GC_LSN_CURRENT);
	if (new_slice != NULL) {
		vy_log_create_run(lsm->id, new_run->id, new_run->dump_lsn,
				  new_run->dump_count);
		vy_log_insert_slice(range->id, new_run->id, new_slice->id,
				    tuple_data_or_null(new_slice->begin.stmt),
				    tuple_data_or_null(new_slice->end.stmt));
	}
	if (vy_log_tx_commit() < 0) {
		if (new_slice != NULL)
			vy_slice_delete(new_slice);
		return -1;
	}

	/*
	 * Remove compacted run files that were created after
	 * the last checkpoint (and hence are not referenced
	 * by any checkpoint) immediately to save disk space.
	 *
	 * Don't bother logging it to avoid a potential race
	 * with a garbage collection task, which may be cleaning
	 * up concurrently. The log will be cleaned up on the
	 * next checkpoint.
	 */
	rlist_foreach_entry(run, &unused_runs, in_unused) {
		if (run->dump_lsn > vy_log_signature())
			vy_run_remove_files(lsm->env->path, lsm->space_id,
					    lsm->index_id, run->id);
	}

	/*
	 * Account the new run if it is not empty,
	 * otherwise discard it.
	 */
	if (new_slice != NULL) {
		vy_lsm_add_run(lsm, new_run);
		/* Drop the reference held by the task. */
		vy_run_unref(new_run);
	} else
		vy_run_discard(new_run);

	/*
	 * Replace compacted slices with the resulting slice and
	 * account compaction in LSM tree statistics.
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
	vy_disk_stmt_counter_reset(&compaction_input);
	for (slice = first_slice; ; slice = next_slice) {
		next_slice = rlist_next_entry(slice, in_range);
		vy_range_remove_slice(range, slice);
		rlist_add_entry(&compacted_slices, slice, in_range);
		vy_disk_stmt_counter_add(&compaction_input, &slice->count);
		if (slice == last_slice)
			break;
	}
	range->n_compactions++;
	vy_range_update_compaction_priority(range, &lsm->opts);
	vy_range_update_dumps_per_compaction(range);
	vy_lsm_acct_range(lsm, range);
	vy_lsm_acct_compaction(lsm, compaction_time,
			       &compaction_input, &compaction_output);
	scheduler->stat.compaction_input += compaction_input.bytes;
	scheduler->stat.compaction_output += compaction_output.bytes;
	scheduler->stat.compaction_time += compaction_time;

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
out:
	/* The iterator has been cleaned up in worker. */
	task->wi->iface->close(task->wi);

	assert(heap_node_is_stray(&range->heap_node));
	vy_range_heap_insert(&lsm->range_heap, range);
	vy_scheduler_update_lsm(scheduler, lsm);

	say_info("%s: completed compacting range %s",
		 vy_lsm_name(lsm), vy_range_str(range));
	return 0;
}

static void
vy_task_compaction_abort(struct vy_task *task)
{
	struct vy_scheduler *scheduler = task->scheduler;
	struct vy_lsm *lsm = task->lsm;
	struct vy_range *range = task->range;

	/* The iterator has been cleaned up in worker. */
	task->wi->iface->close(task->wi);

	struct error *e = diag_last_error(&task->diag);
	error_log(e);
	say_error("%s: failed to compact range %s",
		  vy_lsm_name(lsm), vy_range_str(range));

	vy_run_discard(task->new_run);

	assert(heap_node_is_stray(&range->heap_node));
	vy_range_heap_insert(&lsm->range_heap, range);
	vy_scheduler_update_lsm(scheduler, lsm);
}

static int
vy_task_compaction_new(struct vy_scheduler *scheduler, struct vy_worker *worker,
		       struct vy_lsm *lsm, struct vy_task **p_task)
{
	static struct vy_task_ops compaction_ops = {
		.execute = vy_task_compaction_execute,
		.complete = vy_task_compaction_complete,
		.abort = vy_task_compaction_abort,
	};

	struct vy_range *range = vy_range_heap_top(&lsm->range_heap);
	assert(range != NULL);
	assert(range->compaction_priority > 1);

	if (vy_lsm_split_range(lsm, range) ||
	    vy_lsm_coalesce_range(lsm, range)) {
		vy_scheduler_update_lsm(scheduler, lsm);
		return 0;
	}

	struct vy_task *task = vy_task_new(scheduler, worker, lsm,
					   &compaction_ops);
	if (task == NULL)
		goto err_task;

	struct vy_run *new_run = vy_run_prepare(scheduler->run_env, lsm);
	if (new_run == NULL)
		goto err_run;

	struct vy_stmt_stream *wi;
	bool is_last_level = (range->compaction_priority == range->slice_count);
	wi = vy_write_iterator_new(task->cmp_def, lsm->index_id == 0,
				   is_last_level, scheduler->read_views,
				   lsm->index_id > 0 ? NULL :
				   &task->deferred_delete_handler);
	if (wi == NULL)
		goto err_wi;

	struct vy_slice *slice;
	int32_t dump_count = 0;
	int n = range->compaction_priority;
	rlist_foreach_entry(slice, &range->slices, in_range) {
		if (vy_write_iterator_new_slice(wi, slice,
						lsm->disk_format) != 0)
			goto err_wi_sub;
		new_run->dump_lsn = MAX(new_run->dump_lsn,
					slice->run->dump_lsn);
		dump_count += slice->run->dump_count;
		/* Remember the slices we are compacting. */
		if (task->first_slice == NULL)
			task->first_slice = slice;
		task->last_slice = slice;

		if (--n == 0)
			break;
	}
	assert(n == 0);
	assert(new_run->dump_lsn >= 0);
	if (range->compaction_priority == range->slice_count)
		dump_count -= slice->run->dump_count;
	/*
	 * Do not update dumps_per_compaction in case compaction
	 * was triggered manually to avoid unexpected side effects,
	 * such as splitting/coalescing ranges for no good reason.
	 */
	if (range->needs_compaction)
		new_run->dump_count = slice->run->dump_count;
	else
		new_run->dump_count = dump_count;

	range->needs_compaction = false;

	task->range = range;
	task->new_run = new_run;
	task->wi = wi;
	task->bloom_fpr = lsm->opts.bloom_fpr;
	task->page_size = lsm->opts.page_size;

	/*
	 * Remove the range we are going to compact from the heap
	 * so that it doesn't get selected again.
	 */
	vy_range_heap_delete(&lsm->range_heap, range);
	vy_scheduler_update_lsm(scheduler, lsm);

	say_info("%s: started compacting range %s, runs %d/%d",
		 vy_lsm_name(lsm), vy_range_str(range),
                 range->compaction_priority, range->slice_count);
	*p_task = task;
	return 0;

err_wi_sub:
	task->wi->iface->close(wi);
err_wi:
	vy_run_discard(new_run);
err_run:
	vy_task_delete(task);
err_task:
	diag_log();
	say_error("%s: could not start compacting range %s",
		  vy_lsm_name(lsm), vy_range_str(range));
	return -1;
}

/**
 * Fiber function that actually executes a vinyl task.
 * After finishing a task, it sends it back to tx.
 */
static int
vy_task_f(va_list va)
{
	struct vy_task *task = va_arg(va, struct vy_task *);
	struct vy_worker *worker = task->worker;

	assert(task->fiber == fiber());
	assert(worker->task == task);
	assert(&worker->cord == cord());

	if (task->ops->execute(task) != 0 && !task->is_failed) {
		struct diag *diag = diag_get();
		assert(!diag_is_empty(diag));
		task->is_failed = true;
		diag_move(diag, &task->diag);
	}
	cmsg_init(&task->cmsg, vy_task_complete_route);
	cpipe_push(&worker->tx_pipe, &task->cmsg);
	task->fiber = NULL;
	worker->task = NULL;
	return 0;
}

/**
 * Callback invoked by a worker thread upon receiving a task.
 * It schedules a fiber which actually executes the task, so
 * as not to block the event loop.
 */
static void
vy_task_execute_f(struct cmsg *cmsg)
{
	struct vy_task *task = container_of(cmsg, struct vy_task, cmsg);
	struct vy_worker *worker = task->worker;

	assert(task->fiber == NULL);
	assert(worker->task == NULL);
	assert(&worker->cord == cord());

	task->fiber = fiber_new("task", vy_task_f);
	if (task->fiber == NULL) {
		task->is_failed = true;
		diag_move(diag_get(), &task->diag);
		cmsg_init(&task->cmsg, vy_task_complete_route);
		cpipe_push(&worker->tx_pipe, &task->cmsg);
	} else {
		worker->task = task;
		fiber_start(task->fiber, task);
	}
}

/**
 * Callback invoked by the tx thread upon receiving an executed
 * task from a worker thread. It adds the task to the processed
 * task queue and wakes up the scheduler so that it can complete
 * it.
 */
static void
vy_task_complete_f(struct cmsg *cmsg)
{
	struct vy_task *task = container_of(cmsg, struct vy_task, cmsg);
	stailq_add_tail_entry(&task->scheduler->processed_tasks,
			      task, in_processed);
	fiber_cond_signal(&task->scheduler->scheduler_cond);
}

/**
 * Create a task for dumping an LSM tree. The new task is returned
 * in @ptask. If there's no LSM tree that needs to be dumped or all
 * workers are currently busy, @ptask is set to NULL.
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
	struct vy_worker *worker = NULL;
retry:
	*ptask = NULL;
	if (!vy_scheduler_dump_in_progress(scheduler)) {
		/*
		 * All memory trees of past generations have
		 * been dumped, nothing to do.
		 */
		goto no_task;
	}
	/*
	 * Look up the oldest LSM tree eligible for dump.
	 */
	struct vy_lsm *lsm = vy_dump_heap_top(&scheduler->dump_heap);
	if (lsm == NULL) {
		/*
		 * There is no LSM tree and so no task to schedule.
		 * Complete the current dump round.
		 */
		vy_scheduler_complete_dump(scheduler);
		goto no_task;
	}
	if (!lsm->is_dumping && lsm->pin_count == 0 &&
	    vy_lsm_generation(lsm) == scheduler->dump_generation) {
		/*
		 * Dump is in progress and there is an LSM tree that
		 * contains data that must be dumped at the current
		 * round. Try to create a task for it.
		 */
		if (worker == NULL) {
			worker = vy_worker_pool_get(&scheduler->dump_pool);
			if (worker == NULL)
				return 0; /* all workers are busy */
		}
		if (vy_task_dump_new(scheduler, worker, lsm, ptask) != 0) {
			vy_worker_pool_put(worker);
			return -1;
		}
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
	 * is complete. But if there's no any other tasks
	 * in progress, it may mean that dump_generation does
	 * not catch up with current generation. This may happen
	 * due to failed dump process (i.e. generation is bumped
	 * but dump_generation is not). In turn, after dump is failed,
	 * next dump will be throttled which opens a window for DDL
	 * operations. For instance, index dropping and creation of
	 * new one results in mentioned situation.
	 */
	if (scheduler->dump_task_count == 0) {
		assert(scheduler->dump_generation < vy_lsm_generation(lsm));
		scheduler->dump_generation = vy_lsm_generation(lsm);
		goto retry;
	}
no_task:
	if (worker != NULL)
		vy_worker_pool_put(worker);
	return 0;
}

/**
 * Create a task for compacting a range. The new task is returned
 * in @ptask. If there's no range that needs to be compacted or all
 * workers are currently busy, @ptask is set to NULL.
 *
 * We compact ranges that have more runs in a level than specified
 * by run_count_per_level configuration option. Among those runs we
 * give preference to those ranges whose compaction will reduce
 * read amplification most.
 *
 * Returns 0 on success, -1 on failure.
 */
static int
vy_scheduler_peek_compaction(struct vy_scheduler *scheduler,
			     struct vy_task **ptask)
{
	struct vy_worker *worker = NULL;
retry:
	*ptask = NULL;
	struct vy_lsm *lsm = vy_compaction_heap_top(&scheduler->compaction_heap);
	if (lsm == NULL)
		goto no_task; /* nothing to do */
	if (vy_lsm_compaction_priority(lsm) <= 1)
		goto no_task; /* nothing to do */
	if (worker == NULL) {
		worker = vy_worker_pool_get(&scheduler->compaction_pool);
		if (worker == NULL)
			return 0; /* all workers are busy */
	}
	if (vy_task_compaction_new(scheduler, worker, lsm, ptask) != 0) {
		vy_worker_pool_put(worker);
		return -1;
	}
	if (*ptask == NULL)
		goto retry; /* LSM tree dropped or range split/coalesced */
	return 0; /* new task */
no_task:
	if (worker != NULL)
		vy_worker_pool_put(worker);
	return 0;
}

static int
vy_schedule(struct vy_scheduler *scheduler, struct vy_task **ptask)
{
	*ptask = NULL;

	if (vy_scheduler_peek_dump(scheduler, ptask) != 0)
		goto fail;
	if (*ptask != NULL)
		goto found;

	if (vy_scheduler_peek_compaction(scheduler, ptask) != 0)
		goto fail;
	if (*ptask != NULL)
		goto found;

	/* no task to run */
	return 0;
found:
	scheduler->stat.tasks_inprogress++;
	return 0;
fail:
	assert(!diag_is_empty(diag_get()));
	diag_move(diag_get(), &scheduler->diag);
	return -1;

}

static int
vy_task_complete(struct vy_task *task)
{
	struct vy_scheduler *scheduler = task->scheduler;

	assert(scheduler->stat.tasks_inprogress > 0);
	scheduler->stat.tasks_inprogress--;

	struct diag *diag = &task->diag;
	if (task->is_failed) {
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
	scheduler->stat.tasks_completed++;
	return 0;
fail:
	if (task->ops->abort)
		task->ops->abort(task);
	diag_move(diag, &scheduler->diag);
	scheduler->stat.tasks_failed++;
	return -1;
}

static int
vy_scheduler_f(va_list va)
{
	struct vy_scheduler *scheduler = va_arg(va, struct vy_scheduler *);

	while (scheduler->scheduler_fiber != NULL) {
		struct stailq processed_tasks;
		struct vy_task *task, *next;
		int tasks_failed = 0, tasks_done = 0;

		/* Get the list of processed tasks. */
		stailq_create(&processed_tasks);
		stailq_concat(&processed_tasks, &scheduler->processed_tasks);

		/* Complete and delete all processed tasks. */
		stailq_foreach_entry_safe(task, next, &processed_tasks,
					  in_processed) {
			if (vy_task_complete(task) != 0)
				tasks_failed++;
			else
				tasks_done++;
			vy_worker_pool_put(task->worker);
			vy_task_delete(task);
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
			 * and recheck the processed_tasks in order not
			 * to lose a wakeup event and hang for good.
			 */
			continue;
		}
		/* Throttle for a while if a task failed. */
		if (tasks_failed > 0)
			goto error;
		/* Get a task to schedule. */
		if (vy_schedule(scheduler, &task) != 0)
			goto error;
		/* Nothing to do or all workers are busy. */
		if (task == NULL) {
			/* Wait for changes. */
			fiber_cond_wait(&scheduler->scheduler_cond);
			continue;
		}

		/* Queue the task for execution. */
		cmsg_init(&task->cmsg, vy_task_execute_route);
		cpipe_push(&task->worker->worker_pipe, &task->cmsg);

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
	}

	return 0;
}

static int
vy_worker_f(va_list ap)
{
	struct vy_worker *worker = va_arg(ap, struct vy_worker *);
	struct cbus_endpoint endpoint;

	cpipe_create(&worker->tx_pipe, "tx");
	cbus_endpoint_create(&endpoint, cord_name(&worker->cord),
			     fiber_schedule_cb, fiber());
	cbus_loop(&endpoint);
	/*
	 * Cancel the task that is currently being executed by
	 * this worker and join the fiber before destroying
	 * the pipe to make sure it doesn't access freed memory.
	 */
	if (worker->task != NULL) {
		struct fiber *fiber = worker->task->fiber;
		assert(fiber != NULL);
		assert(!fiber_is_dead(fiber));
		fiber_set_joinable(fiber, true);
		fiber_cancel(fiber);
		fiber_join(fiber);
	}
	cbus_endpoint_destroy(&endpoint, cbus_process);
	cpipe_destroy(&worker->tx_pipe);
	return 0;
}
