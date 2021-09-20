#ifndef INCLUDES_TARANTOOL_BOX_VY_SCHEDULER_H
#define INCLUDES_TARANTOOL_BOX_VY_SCHEDULER_H
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

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <small/rlist.h>

#include "diag.h"
#include "fiber_cond.h"
#define HEAP_FORWARD_DECLARATION
#include "salad/heap.h"
#include "salad/stailq.h"
#include "vy_stat.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct fiber;
struct vy_lsm;
struct vy_quota;
struct vy_run_env;
struct vy_worker;
struct vy_scheduler;

typedef void
(*vy_scheduler_dump_complete_f)(struct vy_scheduler *scheduler,
				int64_t dump_generation, double dump_duration);

struct vy_worker_pool {
	/** Name of the pool. Used for naming threads. */
	const char *name;
	/** Number of worker threads in the pool. */
	int size;
	/** Array of all worker threads in the pool. */
	struct vy_worker *workers;
	/** List of workers that are currently idle. */
	struct stailq idle_workers;
};

struct vy_scheduler {
	/** Scheduler fiber. */
	struct fiber *scheduler_fiber;
	/** Used to wake up the scheduler fiber from TX. */
	struct fiber_cond scheduler_cond;
	/** Pool of threads for performing background dumps. */
	struct vy_worker_pool dump_pool;
	/** Pool of threads for performing background compactions. */
	struct vy_worker_pool compaction_pool;
	/** Queue of processed tasks, linked by vy_task::in_processed. */
	struct stailq processed_tasks;
	/**
	 * Heap of LSM trees, ordered by dump priority,
	 * linked by vy_lsm::in_dump.
	 */
	heap_t dump_heap;
	/**
	 * Heap of LSM trees, ordered by compaction priority,
	 * linked by vy_lsm::in_compaction.
	 */
	heap_t compaction_heap;
	/** Last error seen by the scheduler. */
	struct diag diag;
	/**
	 * Scheduler timeout. Grows exponentially with each
	 * successive failure. Reset on successful task completion.
	 */
	double timeout;
	/** Set if the scheduler is throttled due to errors. */
	bool is_throttled;
	/** Set if checkpoint is in progress. */
	bool checkpoint_in_progress;
	/**
	 * In order to guarantee checkpoint consistency, we must not
	 * dump in-memory trees created after checkpoint was started
	 * so we set this flag instead, which will make the scheduler
	 * schedule a dump as soon as checkpoint is complete.
	 */
	bool dump_pending;
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
	double dump_start;
	/** Signaled on dump round completion. */
	struct fiber_cond dump_cond;
	/** Scheduler statistics. */
	struct vy_scheduler_stat stat;
	/**
	 * Function called by the scheduler upon dump round
	 * completion. It is supposed to free memory released
	 * by the dump.
	 */
	vy_scheduler_dump_complete_f dump_complete_cb;
	/** List of read views, see vy_tx_manager::read_views. */
	struct rlist *read_views;
	/** Context needed for writing runs. */
	struct vy_run_env *run_env;
	/**
	 * Memory quota. Used for accounting deferred DELETE statements
	 * written to memory during compaction.
	 */
	struct vy_quota *quota;
};

/**
 * Return true if memory dump is in progress, i.e. there are
 * in-memory trees that are being dumped right now or should
 * be scheduled for dump as soon as possible.
 */
static inline bool
vy_scheduler_dump_in_progress(struct vy_scheduler *scheduler)
{
	assert(scheduler->dump_generation <= scheduler->generation);
	return scheduler->dump_generation < scheduler->generation;
}

/**
 * Create a scheduler instance.
 */
void
vy_scheduler_create(struct vy_scheduler *scheduler, int write_threads,
		    vy_scheduler_dump_complete_f dump_complete_cb,
		    struct vy_run_env *run_env, struct rlist *read_views,
		    struct vy_quota *quota);

/**
 * Start a scheduler fiber.
 */
void
vy_scheduler_start(struct vy_scheduler *scheduler);

/**
 * Destroy a scheduler instance.
 */
void
vy_scheduler_destroy(struct vy_scheduler *scheduler);

/**
 * Reset scheduler statistics (called by box.stat.reset).
 */
void
vy_scheduler_reset_stat(struct vy_scheduler *scheduler);

/**
 * Add an LSM tree to scheduler dump/compaction queues.
 * When the LSM tree is destroyed, it will be removed
 * from the queues automatically.
 */
int
vy_scheduler_add_lsm(struct vy_scheduler *, struct vy_lsm *);

/**
 * Trigger dump of all currently existing in-memory trees.
 */
void
vy_scheduler_trigger_dump(struct vy_scheduler *scheduler);

/**
 * Trigger dump of all currently existing in-memory trees
 * and wait until it is complete. Returns 0 on success.
 */
int
vy_scheduler_dump(struct vy_scheduler *scheduler);

/**
 * Force major compaction of an LSM tree.
 */
void
vy_scheduler_force_compaction(struct vy_scheduler *scheduler,
			      struct vy_lsm *lsm);

/**
 * Schedule a checkpoint. Please call vy_scheduler_wait_checkpoint()
 * after that.
 */
int
vy_scheduler_begin_checkpoint(struct vy_scheduler *, bool);

/**
 * Wait for checkpoint. Please call vy_scheduler_end_checkpoint()
 * after that.
 */
int
vy_scheduler_wait_checkpoint(struct vy_scheduler *);

/**
 * End checkpoint. Called on both checkpoint commit and abort.
 */
void
vy_scheduler_end_checkpoint(struct vy_scheduler *);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* INCLUDES_TARANTOOL_BOX_VY_SCHEDULER_H */
