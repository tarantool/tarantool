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
#include "say.h"
#include "latch.h"
#include "vclock.h"
#include "cbus.h"
#include "engine.h"		/* engine_collect_garbage() */
#include "wal.h"		/* wal_collect_garbage() */

struct gc_state gc;

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
	latch_create(&gc.latch);
}

static void
gc_process_wal_event(struct wal_watcher_msg *);

void
gc_set_wal_watcher(void)
{
	/*
	 * Since the function is called from box_cfg() it is
	 * important that we do not pass a message processing
	 * callback to wal_set_watcher(). Doing so would cause
	 * credentials corruption in the fiber executing
	 * box_cfg() in case it processes some iproto messages.
	 * Besides, by the time the function is called
	 * tx_fiber_pool is already set up and it will process
	 * all the messages directed to "tx" endpoint safely.
	 */
	wal_set_watcher(&gc.wal_watcher, "tx", gc_process_wal_event,
			NULL, WAL_EVENT_GC);
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
	 * Engine callbacks may sleep, because they use coio for
	 * removing files. Make sure we won't try to remove the
	 * same file multiple times by serializing concurrent gc
	 * executions.
	 */
	latch_lock(&gc.latch);
	/*
	 * Run garbage collection.
	 *
	 * It may occur that we proceed to deletion of WAL files
	 * and other engine files after having failed to delete
	 * a memtx snap file. If this happens, the corresponding
	 * checkpoint won't be removed from box.info.gc(), because
	 * we use snap files to build the checkpoint list, but
	 * it won't be possible to back it up or recover from it.
	 * This is OK as unlink() shouldn't normally fail. Besides
	 * we never remove the last checkpoint and the following
	 * WALs so this may only affect backup checkpoints.
	 */
	if (run_engine_gc)
		engine_collect_garbage(&checkpoint->vclock);
	/*
	 * Run wal_collect_garbage() even if we don't need to
	 * delete any WAL files, because we have to apprise
	 * the WAL thread of the oldest checkpoint signature.
	 */
	wal_collect_garbage(vclock, &checkpoint->vclock);
	latch_unlock(&gc.latch);
}

/**
 * Deactivate consumers that need files deleted by the WAL thread.
 */
static void
gc_process_wal_event(struct wal_watcher_msg *msg)
{
	assert((msg->events & WAL_EVENT_GC) != 0);

	struct gc_consumer *consumer = gc_tree_first(&gc.consumers);
	while (consumer != NULL &&
	       vclock_sum(&consumer->vclock) < vclock_sum(&msg->gc_vclock)) {
		struct gc_consumer *next = gc_tree_next(&gc.consumers,
							consumer);
		assert(!consumer->is_inactive);
		consumer->is_inactive = true;
		gc_tree_remove(&gc.consumers, consumer);

		say_crit("deactivated WAL consumer %s at %s", consumer->name,
			 vclock_to_string(&consumer->vclock));

		consumer = next;
	}
	gc_run();
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
		gc_run();
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

	gc_run();
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
	gc_run();
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
		gc_run();
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

	gc_run();
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
