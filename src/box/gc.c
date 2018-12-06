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
#include "gc.h"

#include <trivia/util.h>

#include <assert.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#define RB_COMPACT 1
#include <small/rb.h>
#include <small/rlist.h>

#include "diag.h"
#include "errcode.h"
#include "fiber.h"
#include "fiber_cond.h"
#include "latch.h"
#include "say.h"
#include "vclock.h"
#include "cbus.h"
#include "schema.h"
#include "engine.h"		/* engine_collect_garbage() */
#include "wal.h"		/* wal_collect_garbage() */

struct gc_state gc;

static int
gc_fiber_f(va_list);

/**
 * Comparator used for ordering gc_consumer objects by signature
 * in a binary tree.
 */
static inline int
gc_consumer_cmp(const struct gc_consumer *a, const struct gc_consumer *b)
{
	if (vclock_sum(&a->vclock) < vclock_sum(&b->vclock))
		return -1;
	if (vclock_sum(&a->vclock) > vclock_sum(&b->vclock))
		return 1;
	if ((intptr_t)a < (intptr_t)b)
		return -1;
	if ((intptr_t)a > (intptr_t)b)
		return 1;
	return 0;
}

rb_gen(MAYBE_UNUSED static inline, gc_tree_, gc_tree_t,
       struct gc_consumer, node, gc_consumer_cmp);

/** Free a consumer object. */
static void
gc_consumer_delete(struct gc_consumer *consumer)
{
	TRASH(consumer);
	free(consumer);
}

/** Free a checkpoint object. */
static void
gc_checkpoint_delete(struct gc_checkpoint *checkpoint)
{
	TRASH(checkpoint);
	free(checkpoint);
}

void
gc_init(void)
{
	/* Don't delete any files until recovery is complete. */
	gc.min_checkpoint_count = INT_MAX;

	vclock_create(&gc.vclock);
	rlist_create(&gc.checkpoints);
	gc_tree_new(&gc.consumers);
	fiber_cond_create(&gc.cond);

	gc.fiber = fiber_new("gc", gc_fiber_f);
	if (gc.fiber == NULL)
		panic("failed to start garbage collection fiber");

	fiber_start(gc.fiber);
}

void
gc_free(void)
{
	/*
	 * Can't clear the WAL watcher as the event loop isn't
	 * running when this function is called.
	 */

	/* Free checkpoints. */
	struct gc_checkpoint *checkpoint, *next_checkpoint;
	rlist_foreach_entry_safe(checkpoint, &gc.checkpoints, in_checkpoints,
				 next_checkpoint) {
		gc_checkpoint_delete(checkpoint);
	}
	/* Free all registered consumers. */
	struct gc_consumer *consumer = gc_tree_first(&gc.consumers);
	while (consumer != NULL) {
		struct gc_consumer *next = gc_tree_next(&gc.consumers,
							consumer);
		gc_tree_remove(&gc.consumers, consumer);
		gc_consumer_delete(consumer);
		consumer = next;
	}
}

/**
 * Invoke garbage collection in order to remove files left
 * from old checkpoints. The number of checkpoints saved by
 * this function is specified by box.cfg.checkpoint_count.
 */
static void
gc_run(void)
{
	bool run_wal_gc = false;
	bool run_engine_gc = false;

	/*
	 * Find the oldest checkpoint that must be preserved.
	 * We have to preserve @min_checkpoint_count oldest
	 * checkpoints, plus we can't remove checkpoints that
	 * are still in use.
	 */
	struct gc_checkpoint *checkpoint = NULL;
	while (true) {
		checkpoint = rlist_first_entry(&gc.checkpoints,
				struct gc_checkpoint, in_checkpoints);
		if (gc.checkpoint_count <= gc.min_checkpoint_count)
			break;
		if (!rlist_empty(&checkpoint->refs))
			break; /* checkpoint is in use */
		rlist_del_entry(checkpoint, in_checkpoints);
		gc_checkpoint_delete(checkpoint);
		gc.checkpoint_count--;
		run_engine_gc = true;
	}

	/* At least one checkpoint must always be available. */
	assert(checkpoint != NULL);

	/*
	 * Find the vclock of the oldest WAL row to keep.
	 * Note, we must keep all WALs created after the
	 * oldest checkpoint, even if no consumer needs them.
	 */
	const struct vclock *vclock = (gc_tree_empty(&gc.consumers) ? NULL :
				       &gc_tree_first(&gc.consumers)->vclock);
	if (vclock == NULL ||
	    vclock_sum(vclock) > vclock_sum(&checkpoint->vclock))
		vclock = &checkpoint->vclock;

	if (vclock_sum(vclock) > vclock_sum(&gc.vclock)) {
		vclock_copy(&gc.vclock, vclock);
		run_wal_gc = true;
	}

	if (!run_engine_gc && !run_wal_gc)
		return; /* nothing to do */

	/*
	 * Run garbage collection.
	 *
	 * The order is important here: we must invoke garbage
	 * collection for memtx snapshots first and abort if it
	 * fails - see comment to memtx_engine_collect_garbage().
	 */
	int rc = 0;
	if (run_engine_gc)
		rc = engine_collect_garbage(&checkpoint->vclock);
	if (run_wal_gc && rc == 0)
		wal_collect_garbage(vclock);
}

static int
gc_fiber_f(va_list ap)
{
	(void)ap;
	while (!fiber_is_cancelled()) {
		int delta = gc.scheduled - gc.completed;
		if (delta == 0) {
			/* No pending garbage collection. */
			fiber_sleep(TIMEOUT_INFINITY);
			continue;
		}
		assert(delta > 0);
		gc_run();
		gc.completed += delta;
		fiber_cond_signal(&gc.cond);
	}
	return 0;
}

/**
 * Trigger asynchronous garbage collection.
 */
static void
gc_schedule(void)
{
	/*
	 * Do not wake up the background fiber if it's executing
	 * the garbage collection procedure right now, because
	 * it may be waiting for a cbus message, which doesn't
	 * tolerate spurious wakeups. Just increment the counter
	 * then - it will rerun garbage collection as soon as
	 * the current round completes.
	 */
	if (gc.scheduled++ == gc.completed)
		fiber_wakeup(gc.fiber);
}

/**
 * Wait for background garbage collection scheduled prior
 * to this point to complete.
 */
static void
gc_wait(void)
{
	unsigned scheduled = gc.scheduled;
	while (gc.completed < scheduled)
		fiber_cond_wait(&gc.cond);
}

void
gc_advance(const struct vclock *vclock)
{
	/*
	 * In case of emergency ENOSPC, the WAL thread may delete
	 * WAL files needed to restore from backup checkpoints,
	 * which would be kept by the garbage collector otherwise.
	 * Bring the garbage collector vclock up to date.
	 */
	vclock_copy(&gc.vclock, vclock);

	struct gc_consumer *consumer = gc_tree_first(&gc.consumers);
	while (consumer != NULL &&
	       vclock_sum(&consumer->vclock) < vclock_sum(vclock)) {
		struct gc_consumer *next = gc_tree_next(&gc.consumers,
							consumer);
		assert(!consumer->is_inactive);
		consumer->is_inactive = true;
		gc_tree_remove(&gc.consumers, consumer);

		char *vclock_str = vclock_to_string(&consumer->vclock);
		say_crit("deactivated WAL consumer %s at %s",
			 consumer->name, vclock_str);
		free(vclock_str);

		consumer = next;
	}
	gc_schedule();
}

void
gc_set_min_checkpoint_count(int min_checkpoint_count)
{
	gc.min_checkpoint_count = min_checkpoint_count;
}

void
gc_add_checkpoint(const struct vclock *vclock)
{
	struct gc_checkpoint *last_checkpoint = gc_last_checkpoint();
	if (last_checkpoint != NULL &&
	    vclock_sum(&last_checkpoint->vclock) == vclock_sum(vclock)) {
		/*
		 * box.snapshot() doesn't create a new checkpoint
		 * if no rows has been written since the last one.
		 * Rerun the garbage collector in this case, just
		 * in case box.cfg.checkpoint_count has changed.
		 */
		gc_schedule();
		return;
	}
	assert(last_checkpoint == NULL ||
	       vclock_sum(&last_checkpoint->vclock) < vclock_sum(vclock));

	struct gc_checkpoint *checkpoint = calloc(1, sizeof(*checkpoint));
	/*
	 * This function is called after a checkpoint is written
	 * to disk so it can't fail.
	 */
	if (checkpoint == NULL)
		panic("out of memory");

	rlist_create(&checkpoint->refs);
	vclock_copy(&checkpoint->vclock, vclock);
	rlist_add_tail_entry(&gc.checkpoints, checkpoint, in_checkpoints);
	gc.checkpoint_count++;

	gc_schedule();
}

int
gc_checkpoint(void)
{
	int rc;
	struct vclock vclock;

	if (gc.checkpoint_is_in_progress) {
		diag_set(ClientError, ER_CHECKPOINT_IN_PROGRESS);
		return -1;
	}
	gc.checkpoint_is_in_progress = true;

	/*
	 * We don't support DDL operations while making a checkpoint.
	 * Lock them out.
	 */
	latch_lock(&schema_lock);

	/*
	 * Rotate WAL and call engine callbacks to create a checkpoint
	 * on disk for each registered engine.
	 */
	rc = engine_begin_checkpoint();
	if (rc != 0)
		goto out;
	rc = wal_begin_checkpoint(&vclock);
	if (rc != 0)
		goto out;
	rc = engine_commit_checkpoint(&vclock);
	if (rc != 0)
		goto out;
	wal_commit_checkpoint(&vclock);

	/*
	 * Finally, track the newly created checkpoint in the garbage
	 * collector state.
	 */
	gc_add_checkpoint(&vclock);
out:
	if (rc != 0)
		engine_abort_checkpoint();

	latch_unlock(&schema_lock);
	gc.checkpoint_is_in_progress = false;

	/*
	 * Wait for background garbage collection that might
	 * have been triggered by this checkpoint to complete.
	 * Strictly speaking, it isn't necessary, but it
	 * simplifies testing as it guarantees that by the
	 * time box.snapshot() returns, all outdated checkpoint
	 * files have been removed.
	 */
	if (rc == 0)
		gc_wait();

	return rc;
}

void
gc_ref_checkpoint(struct gc_checkpoint *checkpoint,
		  struct gc_checkpoint_ref *ref, const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	vsnprintf(ref->name, GC_NAME_MAX, format, ap);
	va_end(ap);

	rlist_add_tail_entry(&checkpoint->refs, ref, in_refs);
}

void
gc_unref_checkpoint(struct gc_checkpoint_ref *ref)
{
	rlist_del_entry(ref, in_refs);
	gc_schedule();
}

struct gc_consumer *
gc_consumer_register(const struct vclock *vclock, const char *format, ...)
{
	struct gc_consumer *consumer = calloc(1, sizeof(*consumer));
	if (consumer == NULL) {
		diag_set(OutOfMemory, sizeof(*consumer),
			 "malloc", "struct gc_consumer");
		return NULL;
	}

	va_list ap;
	va_start(ap, format);
	vsnprintf(consumer->name, GC_NAME_MAX, format, ap);
	va_end(ap);

	vclock_copy(&consumer->vclock, vclock);
	gc_tree_insert(&gc.consumers, consumer);
	return consumer;
}

void
gc_consumer_unregister(struct gc_consumer *consumer)
{
	if (!consumer->is_inactive) {
		gc_tree_remove(&gc.consumers, consumer);
		gc_schedule();
	}
	gc_consumer_delete(consumer);
}

void
gc_consumer_advance(struct gc_consumer *consumer, const struct vclock *vclock)
{
	if (consumer->is_inactive)
		return;

	int64_t signature = vclock_sum(vclock);
	int64_t prev_signature = vclock_sum(&consumer->vclock);

	assert(signature >= prev_signature);
	if (signature == prev_signature)
		return; /* nothing to do */

	/*
	 * Do not update the tree unless the tree invariant
	 * is violated.
	 */
	struct gc_consumer *next = gc_tree_next(&gc.consumers, consumer);
	bool update_tree = (next != NULL &&
			    signature >= vclock_sum(&next->vclock));

	if (update_tree)
		gc_tree_remove(&gc.consumers, consumer);

	vclock_copy(&consumer->vclock, vclock);

	if (update_tree)
		gc_tree_insert(&gc.consumers, consumer);

	gc_schedule();
}

struct gc_consumer *
gc_consumer_iterator_next(struct gc_consumer_iterator *it)
{
	if (it->curr != NULL)
		it->curr = gc_tree_next(&gc.consumers, it->curr);
	else
		it->curr = gc_tree_first(&gc.consumers);
	return it->curr;
}
