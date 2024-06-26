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
#include <time.h>

#define RB_COMPACT 1
#include <small/rb.h>
#include <small/rlist.h>
#include <tarantool_ev.h>

#include "diag.h"
#include "errcode.h"
#include "fiber.h"
#include "fiber_cond.h"
#include "latch.h"
#include "say.h"
#include "cbus.h"
#include "engine.h"		/* engine_collect_garbage() */
#include "wal.h"		/* wal_collect_garbage() */
#include "checkpoint_schedule.h"
#include "txn_limbo.h"
#include "txn.h"
#include "replication.h"
#include "box.h"
#include "tuple.h"
#include "space_cache.h"
#include "errinj.h"

struct gc_state gc;

static int
gc_cleanup_fiber_f(va_list);
static int
gc_checkpoint_fiber_f(va_list);
static int
gc_consumers_persist_fiber_f(va_list);

/**
 * Comparator used for ordering gc_consumer objects
 * lexicographically by their vclock in a binary tree.
 */
static inline int
gc_consumer_cmp(const struct gc_consumer *a, const struct gc_consumer *b)
{
	int rc = vclock_lex_compare(&a->vclock, &b->vclock);
	if (rc != 0)
		return rc;
	if ((intptr_t)a < (intptr_t)b)
		return -1;
	if ((intptr_t)a > (intptr_t)b)
		return 1;
	return 0;
}

rb_gen(MAYBE_UNUSED static inline, gc_tree_, gc_tree_t,
       struct gc_consumer, in_active, gc_consumer_cmp);

static int
gc_consumer_compare_by_uuid(const struct gc_consumer *a,
			    const struct gc_consumer *b)
{
	return tt_uuid_compare(&a->uuid, &b->uuid);
}

rb_gen(MAYBE_UNUSED static, gc_consumer_hash_, gc_tree_t,
       struct gc_consumer, in_hash, gc_consumer_compare_by_uuid);

static struct gc_consumer *
gc_consumer_by_uuid(const struct tt_uuid *uuid)
{
	struct gc_consumer key;
	key.uuid = *uuid;
	return gc_consumer_hash_search(&gc.consumers_hash, &key);
}

#define gc_consumer_hash_foreach(hash, var) \
	for (struct gc_consumer *(var) = gc_consumer_hash_first(hash); \
	     (var) != NULL; (var) = gc_consumer_hash_next(hash, var))

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
gc_init(on_garbage_collection_f on_garbage_collection)
{
	/* Don't delete any files until recovery is complete. */
	gc.min_checkpoint_count = INT_MAX;

	gc.wal_cleanup_delay = TIMEOUT_INFINITY;
	gc.delay_ref = 0;
	gc.is_paused = true;
	say_info("wal/engine cleanup is paused");

	vclock_create(&gc.vclock);
	rlist_create(&gc.checkpoints);
	gc_tree_new(&gc.active_consumers);
	gc_consumer_hash_new(&gc.consumers_hash);
	fiber_cond_create(&gc.cleanup_cond);
	fiber_cond_create(&gc.persist_cond);
	checkpoint_schedule_cfg(&gc.checkpoint_schedule, 0, 0);

	gc.cleanup_fiber = fiber_new_system("gc", gc_cleanup_fiber_f);
	if (gc.cleanup_fiber == NULL)
		panic("failed to start garbage collection fiber");
	fiber_set_joinable(gc.cleanup_fiber, true);

	gc.checkpoint_fiber = fiber_new_system("checkpoint_daemon",
					       gc_checkpoint_fiber_f);
	if (gc.checkpoint_fiber == NULL)
		panic("failed to start checkpoint daemon fiber");
	fiber_set_joinable(gc.checkpoint_fiber, true);

	gc.persist_fiber = fiber_new_system("gc_consumers_persist_daemon",
					    gc_consumers_persist_fiber_f);
	if (gc.persist_fiber == NULL)
		panic("failed to start gc consumers persist daemon fiber");
	fiber_set_joinable(gc.persist_fiber, true);

	gc.on_garbage_collection = on_garbage_collection;

	fiber_start(gc.cleanup_fiber);
	fiber_start(gc.checkpoint_fiber);
	fiber_start(gc.persist_fiber);
}

void
gc_shutdown(void)
{
	fiber_cancel(gc.checkpoint_fiber);
	fiber_cancel(gc.cleanup_fiber);
	fiber_cancel(gc.persist_fiber);
	fiber_join(gc.checkpoint_fiber);
	gc.checkpoint_fiber = NULL;
	fiber_join(gc.cleanup_fiber);
	gc.cleanup_fiber = NULL;
	fiber_join(gc.persist_fiber);
	gc.persist_fiber = NULL;
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
	struct gc_consumer *consumer = gc_tree_first(&gc.active_consumers);
	while (consumer != NULL) {
		struct gc_consumer *next = gc_tree_next(&gc.active_consumers,
							consumer);
		gc_tree_remove(&gc.active_consumers, consumer);
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
gc_run_cleanup(void)
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

	/* Find the vclock of the oldest WAL row to keep. */
	struct vclock min_vclock;
	struct gc_consumer *consumer = gc_tree_first(&gc.active_consumers);
	/*
	 * Vclock of the oldest WAL row to keep is a by-component
	 * minimum of all consumer vclocks and the oldest
	 * checkpoint vclock. This ensures that all rows needed by
	 * at least one consumer are kept.
	 * Note, we must keep all WALs created after the
	 * oldest checkpoint, even if no consumer needs them.
	 */
	vclock_copy(&min_vclock, &checkpoint->vclock);
	while (consumer != NULL) {
		/*
		 * Consumers will never need rows signed
		 * with a zero instance id (local rows).
		 */
		vclock_min_ignore0(&min_vclock, &consumer->vclock);
		consumer = gc_tree_next(&gc.active_consumers, consumer);
	}

	/*
	 * Acquire minimum vclock of a file, which is protected from garbage
	 * collection by wal_retention_period option.
	 */
	struct vclock retention_vclock;
	wal_get_retention_vclock(&retention_vclock);
	if (vclock_is_set(&retention_vclock))
		vclock_min(&min_vclock, &retention_vclock);

	if (vclock_sum(&min_vclock) > vclock_sum(&gc.vclock)) {
		vclock_copy(&gc.vclock, &min_vclock);
		run_wal_gc = true;
	}

	if (!run_engine_gc && !run_wal_gc)
		return; /* nothing to do */

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
	if (run_wal_gc)
		wal_collect_garbage(&min_vclock);
	gc.on_garbage_collection();
}

static int
gc_cleanup_fiber_f(va_list ap)
{
	(void)ap;

	/*
	 * Stage 1 (optional): in case if we're booting
	 * up with cleanup disabled lets do wait in a
	 * separate cycle to minimize branching on stage 2.
	 */
	if (gc.is_paused) {
		double start_time = fiber_clock();
		double timeout = gc.wal_cleanup_delay;
		while (!fiber_is_cancelled()) {
			if (fiber_yield_timeout(timeout)) {
				say_info("wal/engine cleanup is resumed "
					 "due to timeout expiration");
				gc.is_paused = false;
				gc.delay_ref = 0;
				break;
			}

			/*
			 * If a last reference is dropped
			 * we can exit out early.
			 */
			if (!gc.is_paused) {
				say_info("wal/engine cleanup is resumed");
				break;
			}

			/*
			 * Woken up to update the timeout.
			 */
			double elapsed = fiber_clock() - start_time;
			if (elapsed >= gc.wal_cleanup_delay) {
				say_info("wal/engine cleanup is resumed "
					 "due to timeout manual update");
				gc.is_paused = false;
				gc.delay_ref = 0;
				break;
			}
			timeout = gc.wal_cleanup_delay - elapsed;
		}
	}

	/*
	 * Stage 2: a regular cleanup cycle.
	 */
	while (!fiber_is_cancelled()) {
		fiber_check_gc();
		int64_t delta = gc.cleanup_scheduled - gc.cleanup_completed;
		if (delta == 0) {
			/* No pending garbage collection. */
			fiber_sleep(TIMEOUT_INFINITY);
			continue;
		}
		assert(delta > 0);
		gc_run_cleanup();
		gc.cleanup_completed += delta;
		fiber_cond_signal(&gc.cleanup_cond);
	}
	return 0;
}

void
gc_set_wal_cleanup_delay(double wal_cleanup_delay)
{
	gc.wal_cleanup_delay = wal_cleanup_delay;
	/*
	 * This routine may be called at arbitrary
	 * moment thus we must be sure the cleanup
	 * fiber is paused to not wake up it when
	 * it is already in a regular cleanup stage.
	 */
	if (gc.is_paused)
		fiber_wakeup(gc.cleanup_fiber);
}

void
gc_delay_ref(void)
{
	if (gc.is_paused) {
		assert(gc.delay_ref >= 0);
		gc.delay_ref++;
	}
}

void
gc_delay_unref(void)
{
	if (gc.is_paused) {
		assert(gc.delay_ref > 0);
		gc.delay_ref--;
		if (gc.delay_ref == 0) {
			gc.is_paused = false;
			fiber_wakeup(gc.cleanup_fiber);
		}
	}
}

/**
 * Trigger asynchronous garbage collection.
 */
static void
gc_schedule_cleanup(void)
{
	/*
	 * Do not wake up the background fiber if it's executing
	 * the garbage collection procedure right now, because
	 * it may be waiting for a cbus message, which doesn't
	 * tolerate spurious wakeups. Just increment the counter
	 * then - it will rerun garbage collection as soon as
	 * the current round completes.
	 */
	if (gc.cleanup_scheduled++ == gc.cleanup_completed)
		fiber_wakeup(gc.cleanup_fiber);
}

/**
 * Wait for background garbage collection scheduled prior
 * to this point to complete.
 */
static void
gc_wait_cleanup(void)
{
	int64_t scheduled = gc.cleanup_scheduled;
	while (gc.cleanup_completed < scheduled)
		fiber_cond_wait(&gc.cleanup_cond);
}

/**
 * Returns true when the consumer is outdated - its vclock is either
 * less than or incomparable with the wal gc vclock.
 */
static bool
gc_consumer_is_outdated(struct gc_consumer *consumer)
{
	return vclock_compare_ignore0(&gc.vclock, &consumer->vclock) > 0;
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

	struct gc_consumer *consumer = gc_tree_first(&gc.active_consumers);
	while (consumer != NULL) {
		struct gc_consumer *next = gc_tree_next(&gc.active_consumers,
							consumer);
		/*
		 * Remove all the consumers whose vclocks are
		 * either less than or incomparable with the wal
		 * gc vclock.
		 */
		if (!gc_consumer_is_outdated(consumer)) {
			consumer = next;
			continue;
		}
		assert(!consumer->is_inactive);
		consumer->is_inactive = true;
		gc_tree_remove(&gc.active_consumers, consumer);

		say_crit("deactivated WAL consumer %s at %s", consumer->name,
			 vclock_to_string(&consumer->vclock));

		consumer = next;
	}
	gc_schedule_cleanup();
	gc.on_garbage_collection();
}

void
gc_set_min_checkpoint_count(int min_checkpoint_count)
{
	gc.min_checkpoint_count = min_checkpoint_count;
}

void
gc_set_checkpoint_interval(double interval)
{
	/*
	 * Reconfigure the schedule and wake up the checkpoint
	 * daemon so that it can readjust.
	 *
	 * Note, we must not wake up the checkpoint daemon fiber
	 * if it's waiting for checkpointing to complete, because
	 * checkpointing code doesn't tolerate spurious wakeups.
	 */
	checkpoint_schedule_cfg(&gc.checkpoint_schedule,
				ev_monotonic_now(loop()), interval);
	if (!gc.checkpoint_is_in_progress)
		fiber_wakeup(gc.checkpoint_fiber);
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
		gc_schedule_cleanup();
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

	gc_schedule_cleanup();
}

static int
gc_do_checkpoint(bool is_scheduled)
{
	int rc;
	struct wal_checkpoint checkpoint;
	int64_t limbo_rollback_count = txn_limbo.rollback_count;

	assert(!gc.checkpoint_is_in_progress);
	gc.checkpoint_is_in_progress = true;

	/*
	 * Rotate WAL and call engine callbacks to create a checkpoint
	 * on disk for each registered engine.
	 */
	rc = engine_begin_checkpoint(is_scheduled);
	if (rc != 0)
		goto out;
	rc = wal_begin_checkpoint(&checkpoint);
	if (rc != 0)
		goto out;
	/*
	 * Check if the checkpoint contains rolled back data. That makes the
	 * checkpoint not self-sufficient - it needs the xlog file with
	 * ROLLBACK. Drop it.
	 */
	if (txn_limbo.rollback_count != limbo_rollback_count) {
		rc = -1;
		diag_set(ClientError, ER_SYNC_ROLLBACK);
		goto out;
	}
	/*
	 * Wait the confirms on all "sync" transactions before
	 * create a snapshot.
	 */
	rc = txn_limbo_wait_confirm(&txn_limbo);
	if (rc != 0)
		goto out;

	rc = engine_commit_checkpoint(&checkpoint.vclock);
	if (rc != 0)
		goto out;
	wal_commit_checkpoint(&checkpoint);

	/*
	 * Finally, track the newly created checkpoint in the garbage
	 * collector state.
	 */
	gc_add_checkpoint(&checkpoint.vclock);
out:
	if (rc != 0)
		engine_abort_checkpoint();

	gc.checkpoint_is_in_progress = false;
	return rc;
}

int
gc_checkpoint(void)
{
	if (gc.checkpoint_is_in_progress) {
		diag_set(ClientError, ER_CHECKPOINT_IN_PROGRESS);
		return -1;
	}

	/*
	 * Since a user invoked a snapshot manually, this may be
	 * because he may be not happy with the current randomized
	 * schedule. Randomize the schedule again and wake up the
	 * checkpoint daemon so that it * can readjust.
	 * It is also a good idea to randomize the interval, since
	 * otherwise many instances running on the same host will
	 * no longer run their checkpoints randomly after
	 * a sweeping box.snapshot() (gh-4432).
	 */
	checkpoint_schedule_cfg(&gc.checkpoint_schedule,
				ev_monotonic_now(loop()),
				gc.checkpoint_schedule.interval);
	fiber_wakeup(gc.checkpoint_fiber);

	if (gc_do_checkpoint(false) != 0)
		return -1;

	/*
	 * Wait for background garbage collection that might
	 * have been triggered by this checkpoint to complete.
	 * Strictly speaking, it isn't necessary, but it
	 * simplifies testing. Same time if GC is paused and
	 * waiting for old XLOGs to be read by replicas the
	 * cleanup won't happen immediately after the checkpoint.
	 */
	if (!gc.is_paused)
		gc_wait_cleanup();
	return 0;
}

void
gc_trigger_checkpoint(void)
{
	if (gc.checkpoint_is_in_progress || gc.checkpoint_is_pending)
		return;

	gc.checkpoint_is_pending = true;
	checkpoint_schedule_reset(&gc.checkpoint_schedule,
				  ev_monotonic_now(loop()));
	fiber_wakeup(gc.checkpoint_fiber);
}

static int
gc_checkpoint_fiber_f(va_list ap)
{
	(void)ap;

	struct checkpoint_schedule *sched = &gc.checkpoint_schedule;
	while (!fiber_is_cancelled()) {
		fiber_check_gc();
		double timeout = checkpoint_schedule_timeout(sched,
					ev_monotonic_now(loop()));
		if (timeout > 0) {
			char buf[128];
			struct tm tm;
			time_t time = (time_t)(ev_now(loop()) + timeout);
			localtime_r(&time, &tm);
			strftime(buf, sizeof(buf), "%c", &tm);
			say_info("scheduled next checkpoint for %s", buf);
		} else {
			/* Periodic checkpointing is disabled. */
			timeout = TIMEOUT_INFINITY;
		}
		bool timed_out = fiber_yield_timeout(timeout);
		if (fiber_is_cancelled())
			break;
		if (!timed_out && !gc.checkpoint_is_pending) {
			/*
			 * The checkpoint schedule has changed or the fiber has
			 * been woken up spuriously.
			 * Reschedule the next checkpoint.
			 */
			continue;
		}
		/* Time to make the next scheduled checkpoint. */
		gc.checkpoint_is_pending = false;
		if (gc.checkpoint_is_in_progress) {
			/*
			 * Another fiber is making a checkpoint.
			 * Skip this one.
			 */
			continue;
		}
		if (gc_do_checkpoint(true) != 0)
			diag_log();
	}
	return 0;
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
	gc_schedule_cleanup();
}

static void
gc_consumer_unregister_impl(struct gc_consumer *consumer);

static struct gc_consumer *
gc_consumer_register_impl(const struct tt_uuid *uuid,
			  const struct vclock *vclock,
			  const char *format, va_list ap)
{
	struct gc_consumer *consumer = xmalloc(sizeof(*consumer));
	vsnprintf(consumer->name, GC_NAME_MAX, format, ap);

	if (uuid != NULL && !tt_uuid_is_nil(uuid)) {
		/* Unregister old consumer, if any. */
		if (gc_consumer_is_registered(uuid))
			gc_consumer_unregister_impl(gc_consumer_by_uuid(uuid));
		consumer->uuid = *uuid;
		gc_consumer_hash_insert(&gc.consumers_hash, consumer);
	} else {
		consumer->uuid = uuid_nil;
	}

	if (vclock != NULL) {
		vclock_copy(&consumer->vclock, vclock);
		vclock_copy(&consumer->volatile_vclock, vclock);
	} else {
		vclock_create(&consumer->vclock);
		vclock_create(&consumer->volatile_vclock);
	}
	consumer->is_async_updated = false;
	consumer->is_inactive = vclock == NULL ||
		gc_consumer_is_outdated(consumer);
	if (!consumer->is_inactive)
		gc_tree_insert(&gc.active_consumers, consumer);
	return consumer;
}

static void
gc_consumer_register_internal(const struct tt_uuid *uuid,
			      const struct vclock *vclock,
			      const char *format, ...)
{
	assert(uuid != NULL);
	assert(!tt_uuid_is_nil(uuid));

	va_list ap;
	va_start(ap, format);
	gc_consumer_register_impl(uuid, vclock, format, ap);
	va_end(ap);
}

static void
gc_consumer_register_dummy_internal(const struct tt_uuid *uuid,
				    const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	gc_consumer_register_impl(uuid, NULL, format, ap);
	va_end(ap);
}

struct gc_consumer *
gc_consumer_register_anonymous(const struct vclock *vclock,
			       const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	struct gc_consumer *consumer =
		gc_consumer_register_impl(NULL, vclock, format, ap);
	va_end(ap);
	return consumer;
}

static void
gc_consumer_unregister_impl(struct gc_consumer *consumer)
{
	assert(consumer != NULL);
	if (!consumer->is_inactive) {
		gc_tree_remove(&gc.active_consumers, consumer);
		gc_schedule_cleanup();
	}
	if (!tt_uuid_is_nil(&consumer->uuid))
		gc_consumer_hash_remove(&gc.consumers_hash, consumer);
	gc_consumer_delete(consumer);
}

void
gc_consumer_unregister_anonymous(struct gc_consumer *consumer)
{
	assert(consumer != NULL);
	gc_consumer_unregister_impl(consumer);
}

bool
gc_consumer_is_registered(const struct tt_uuid *uuid)
{
	return gc_consumer_by_uuid(uuid) != NULL;
}

static void
gc_consumer_update_impl(struct gc_consumer *consumer, const struct vclock *vclock)
{
	assert(consumer != NULL);
	/* Update volatile vclock only if it is consistent with actual one. */
	bool update_volatile_vclock =
		vclock_compare(&consumer->vclock,
			       &consumer->volatile_vclock) == 0;
	/* If the consumer is inactive, activate it if needed. */
	if (consumer->is_inactive) {
		vclock_copy(&consumer->vclock, vclock);
		if (update_volatile_vclock)
			vclock_copy(&consumer->volatile_vclock, vclock);
		consumer->is_inactive = gc_consumer_is_outdated(consumer);
		if (!consumer->is_inactive)
			gc_tree_insert(&gc.active_consumers, consumer);
		/* Do not schedule cleanup - consumer didn't pin any xlogs. */
		return;
	}

	if (vclock_compare_ignore0(&consumer->vclock, vclock) == 0)
		return; /* nothing to do */

	struct vclock old_vclock;
	vclock_copy(&old_vclock, &consumer->vclock);

	/*
	 * Do not update the tree unless the tree invariant
	 * is violated.
	 */
	struct gc_consumer *n = gc_tree_next(&gc.active_consumers, consumer);
	struct gc_consumer *p = gc_tree_prev(&gc.active_consumers, consumer);
	bool update_tree =
		(n != NULL && vclock_lex_compare(vclock, &n->vclock) >= 0) ||
		(p != NULL && vclock_lex_compare(vclock, &p->vclock) <= 0);

	if (update_tree)
		gc_tree_remove(&gc.active_consumers, consumer);

	vclock_copy(&consumer->vclock, vclock);
	if (update_volatile_vclock)
		vclock_copy(&consumer->volatile_vclock, vclock);

	if (update_tree)
		gc_tree_insert(&gc.active_consumers, consumer);

	/* Schedule cleanup only when new vclock is greater or incomparable. */
	if (vclock_compare_ignore0(&consumer->vclock, &old_vclock) > 0)
		gc_schedule_cleanup();
}

int
gc_consumer_register_dummy(const struct tt_uuid *uuid)
{
	if (!gc_consumer_is_persistent()) {
		gc_consumer_register_dummy_internal(uuid, "replica %s",
						    tt_uuid_str(uuid));
		return 0;
	}

	return boxk(IPROTO_REPLACE, BOX_GC_CONSUMERS_ID, "[%s]",
		    tt_uuid_str(uuid));
}

int
gc_consumer_deactivate(const struct tt_uuid *uuid)
{
	struct gc_consumer *consumer = gc_consumer_by_uuid(uuid);
	if (consumer == NULL)
		return 0;
	/*
	 * Invalidate pending async updates. In-progress update will be written,
	 * but it will be serialized before deactivation so it does not do any
	 * harm.
	 */
	consumer->is_async_updated = false;
	return gc_consumer_register_dummy(uuid);
}

int
gc_consumer_unregister(const struct tt_uuid *uuid)
{
	if (!gc_consumer_is_persistent()) {
		struct gc_consumer *consumer = gc_consumer_by_uuid(uuid);
		if (consumer != NULL)
			gc_consumer_unregister_impl(consumer);
		return 0;
	}

	return boxk(IPROTO_DELETE, BOX_GC_CONSUMERS_ID, "[%s]",
		    tt_uuid_str(uuid));
}

int
gc_consumer_update(const struct tt_uuid *uuid, const struct vclock *vclock)
{
	if (!gc_consumer_is_registered(uuid))
		return 0; /* Do not wait if consumer is not registered. */
	gc_consumer_update_async(uuid, vclock);
	uint64_t scheduled = gc.persist_scheduled;
	/*
	 * Wait for two failures: one for in-progress update, if any, and
	 * the second for update with actual volatile vclock of the consumer.
	 */
	uint64_t failed = gc.persist_failed + 2;
	while (gc.persist_completed < scheduled && gc.persist_failed < failed)
		fiber_cond_wait(&gc.persist_cond);
	return gc.persist_completed >= scheduled ? 0 : -1;
}

bool
gc_consumer_is_persistent(void)
{
	return space_by_id(BOX_GC_CONSUMERS_ID) != NULL;
}

void
gc_consumer_update_async(const struct tt_uuid *uuid,
			 const struct vclock *vclock)
{
	struct gc_consumer *consumer = gc_consumer_by_uuid(uuid);
	if (consumer == NULL)
		return;

	if (!gc_consumer_is_persistent()) {
		gc_consumer_update_impl(consumer, vclock);
		return;
	}

	vclock_copy(&consumer->volatile_vclock, vclock);
	consumer->is_async_updated = true;
	/*
	 * Do not wake up the background fiber if it's persisting
	 * consumers right now because it may be waiting for a cbus
	 * message, which doesn't tolerate spurious wakeups.
	 */
	if (gc.persist_scheduled++ == gc.persist_completed)
		fiber_wakeup(gc.persist_fiber);
}

static int
gc_consumer_persistent_update_impl(const struct tt_uuid *uuid,
				   const struct vclock *vclock)
{
	char key_buf[UUID_STR_LEN + 10];
	char *key_end = key_buf;
	key_end = mp_encode_array(key_end, 1);
	key_end = mp_encode_str0(key_end, tt_uuid_str(uuid));
	assert((unsigned long)(key_end - key_buf) < sizeof(key_buf));

	char ops_buf[VCLOCK_STR_LEN_MAX + 20];
	char *ops_end = ops_buf;
	ops_end = mp_encode_array(ops_end, 1);
	ops_end = mp_encode_array(ops_end, 3);
	ops_end = mp_encode_str0(ops_end, "=");
	ops_end = mp_encode_uint(ops_end, 1);
	ops_end = mp_encode_vclock_ignore0(ops_end, vclock);
	assert((unsigned long)(ops_end - ops_buf) < sizeof(ops_buf));

	return box_update(BOX_GC_CONSUMERS_ID, 0, key_buf, key_end,
			  ops_buf, ops_end, 0, NULL);
}

static int
gc_consumers_persist_all(void)
{
	ERROR_INJECT(ERRINJ_WAL_GC_PERSIST_FIBER, {
		diag_set(ClientError, ER_INJECTION, "WAL GC persist fiber");
		return -1;
	});
	int rc = 0;
	struct txn *txn = txn_begin();
	if (txn == NULL)
		return -1;
	/* No need to wait limbo, so let's set TXN_FORCE_ASYNC here. */
	txn_set_flags(txn, TXN_FORCE_ASYNC);

	gc_consumer_hash_foreach(&gc.consumers_hash, consumer) {
		struct vclock *old_vclock = &consumer->vclock;
		struct vclock *new_vclock = &consumer->volatile_vclock;
		struct tt_uuid *uuid = &consumer->uuid;

		if (consumer->is_async_updated &&
		    vclock_compare(old_vclock, new_vclock) != 0 &&
		    gc_consumer_persistent_update_impl(uuid, new_vclock) != 0)
			goto fail;
	}
	rc = txn_commit(txn);
	goto out;
fail:
	rc = -1;
	txn_abort(txn);
out:
	return rc;
}

/**
 * Retry updating persistent WAL GC state every N seconds.
 * Retry timeout should be quite large since there is no point
 * to write to database too often if we fail to do it. In debug
 * mode retry more often for testing purposes.
 */
#ifdef NDEBUG
enum { WAL_GC_PERSIST_FIBER_RETRY_TIMEOUT = 10 };
#else
enum { WAL_GC_PERSIST_FIBER_RETRY_TIMEOUT = 1 };
#endif

static int
gc_consumers_persist_fiber_f(va_list ap)
{
	(void)ap;
	const double retry_timeout = WAL_GC_PERSIST_FIBER_RETRY_TIMEOUT;
	bool say_once = false;
	while (!fiber_is_cancelled()) {
		fiber_check_gc();

		int64_t delta = gc.persist_scheduled - gc.persist_completed;
		if (delta == 0) {
			/* No pending persist requests. */
			fiber_sleep(TIMEOUT_INFINITY);
			continue;
		}
		assert(delta > 0);

		int rc = gc_consumers_persist_all();
		if (rc == 0) {
			say_once = false;
		} else if (!fiber_is_cancelled()) {
			/* Retry in the case of fail. */
			if (!say_once) {
				say_once = true;
				say_error("Failed to advance WAL GC consumers, "
					  "will retry after %.2lf seconds",
					  retry_timeout);
				diag_log();
			}
			/* Wake all waiters on failure. */
			fiber_cond_broadcast(&gc.persist_cond);
			fiber_sleep(retry_timeout);
			continue;
		}
		/* Wake all waiters on success. */
		fiber_cond_broadcast(&gc.persist_cond);
		gc.persist_completed += delta;
	}
	return 0;
}

/** GC consumer definition. */
struct gc_consumer_def {
	/** Instance UUID. */
	struct tt_uuid uuid;
	/** Instance vclock. */
	struct vclock vclock;
	/** Is set if the consumer has vclock. */
	bool has_vclock;
};

/** Mapping from tuple.opts to fields of gc_consumer_def. */
const struct opt_def gc_consumer_def_opts_reg[] = {
	OPT_END,
};

/**
 * Fill gc_consumer_def with opts from the MsgPack map.
 * Argument map can be NULL - default options are set in this case.
 */
static int
gc_consumer_def_opts_decode(struct gc_consumer_def *def, const char *map,
			    struct region *region)
{
	if (map == NULL)
		return 0;
	return opts_decode(def, gc_consumer_def_opts_reg, &map, region);
}

/** Build gc_consumer definition from a _gc_consumers' tuple. */
static struct gc_consumer_def *
gc_consumer_def_new_from_tuple(struct tuple *tuple, struct region *region)
{
	struct gc_consumer_def *def =
		xregion_alloc_object(region, typeof(*def));
	memset(def, 0, sizeof(*def));
	if (tuple_field_uuid(tuple, BOX_GC_CONSUMERS_FIELD_UUID, &def->uuid) != 0)
		return NULL;
	if (tt_uuid_is_nil(&def->uuid)) {
		diag_set(ClientError, ER_INVALID_UUID, tt_uuid_str(&def->uuid));
		return NULL;
	}
	def->has_vclock =
		!tuple_field_is_nil(tuple, BOX_GC_CONSUMERS_FIELD_VCLOCK);
	if (def->has_vclock) {
		const char *mp_vclock = tuple_field_with_type(
			tuple, BOX_GC_CONSUMERS_FIELD_VCLOCK, MP_MAP);
		if (mp_vclock == NULL)
			return NULL;
		if (mp_decode_vclock_ignore0(&mp_vclock, &def->vclock) != 0) {
			diag_set(ClientError, ER_INVALID_VCLOCK);
			return NULL;
		}
	}
	const char *opts = NULL;
	if (tuple_field(tuple, BOX_GC_CONSUMERS_FIELD_OPTS) != NULL) {
		opts = tuple_field_with_type(tuple, BOX_GC_CONSUMERS_FIELD_OPTS,
					     MP_MAP);
	}
	if (gc_consumer_def_opts_decode(def, opts, region) != 0)
		return NULL;
	return def;
}

/**
 * Data passed to transactional triggers of replace in _gc_consumers.
 */
struct gc_consumers_txn_trigger_data {
	/*
	 * Replica UUID. Is used instead of replica object because it can be
	 * unregistered before the trigger is fired.
	 */
	struct tt_uuid uuid;
	/*
	 * Saved old definition of consumer.
	 */
	struct gc_consumer_def *old_def;
	/*
	 * Saved new definition of consumer.
	 */
	struct gc_consumer_def *new_def;
};

static int
on_replace_dd_gc_consumers_commit(struct trigger *trigger, void *event)
{
	(void)event;
	struct gc_consumers_txn_trigger_data *data =
		(struct gc_consumers_txn_trigger_data *)trigger->data;
	struct gc_consumer_def *old_def = data->old_def;
	struct gc_consumer_def *new_def = data->new_def;

	/* Unref GC delay on first non-dummy consumer. */
	if (new_def != NULL && new_def->has_vclock &&
	    (old_def == NULL || !old_def->has_vclock))
		gc_delay_unref();

	if (old_def == NULL) {
		/*
		 * INSERT
		 * Despite the consumer just appeared, it can be already
		 * registered. It can happen on schema upgrade - all replicas
		 * had in-memory consumers until the space _gc_consumers is
		 * created.
		 */
		if (!new_def->has_vclock) {
			gc_consumer_register_dummy_internal(
				&data->uuid, "replica %s",
				tt_uuid_str(&data->uuid));
		} else {
			gc_consumer_register_internal(
				&data->uuid, &new_def->vclock, "replica %s",
				tt_uuid_str(&data->uuid));
		}
	} else if (new_def == NULL) {
		/* DELETE */
		assert(gc_consumer_is_registered(&data->uuid));
		struct gc_consumer *consumer = gc_consumer_by_uuid(&data->uuid);
		gc_consumer_unregister_impl(consumer);
	} else {
		/* UPDATE */
		assert(gc_consumer_is_registered(&data->uuid));
		struct gc_consumer *consumer = gc_consumer_by_uuid(&data->uuid);
		if (new_def->has_vclock) {
			gc_consumer_update_impl(consumer, &new_def->vclock);
		} else {
			if (!consumer->is_inactive) {
				consumer->is_inactive = true;
				gc_tree_remove(&gc.active_consumers, consumer);
			}
		}
	}
	return 0;
}

/**
 * Note that due to concurrent nature of transactions it is unsafe
 * to modify replica->gc right here and in on_rollback, so it is
 * modified only in on_commit triggers - it is safe because they
 * are called in order of transactions' serialization.
 */
int
on_replace_dd_gc_consumers(struct trigger *trigger, void *event)
{
	(void)trigger;
	struct txn *txn = (struct txn *)event;
	struct txn_stmt *stmt = txn_current_stmt(txn);
	struct tuple *old_tuple = stmt->old_tuple;
	struct tuple *new_tuple = stmt->new_tuple;
	struct gc_consumer_def *old_def = NULL;
	struct gc_consumer_def *new_def = NULL;
	struct tt_uuid *replica_uuid = NULL;
	if (old_tuple != NULL) {
		old_def =
			gc_consumer_def_new_from_tuple(old_tuple,
						       &in_txn()->region);
		if (old_def == NULL)
			return -1;
		replica_uuid = &old_def->uuid;
	}
	if (new_tuple != NULL) {
		new_def =
			gc_consumer_def_new_from_tuple(new_tuple,
						       &in_txn()->region);
		if (new_def == NULL)
			return -1;
		replica_uuid = &new_def->uuid;
	}
	assert(old_def != NULL || new_def != NULL);

	/* Just making sure that both tuples have the same uuid. */
	assert(old_def == NULL || new_def == NULL ||
	       tt_uuid_is_equal(&old_def->uuid, &new_def->uuid));

	/*
	 * We cannot rely on the fact that the replica is still registered
	 * in-memory because it can be dropped in the same transaction, and
	 * replica_hash will be updated only on commit, so read row right
	 * from the _cluster. It's important to lookup by uuid, not id,
	 * to correctly handle the case when uuid of replica is updated
	 * and id is the same.
	 */
	char key[UUID_STR_LEN + 10];
	char *key_end = key;
	key_end = mp_encode_array(key_end, 1);
	key_end = mp_encode_str0(key_end, tt_uuid_str(replica_uuid));
	assert((size_t)(key_end - key) < sizeof(key));
	struct tuple *replica_row;
	if (box_index_get(BOX_CLUSTER_ID, 1, key, key_end, &replica_row) != 0)
		return -1;
	bool replica_is_registered = replica_row != NULL;

	if (replica_is_registered && new_def == NULL) {
		diag_set(ClientError, ER_UNSUPPORTED, "gc_consumer",
			 "delete while its replica is still registered");
		return -1;
	}

	struct gc_consumers_txn_trigger_data *trg_data =
		xregion_alloc_object(&in_txn()->region,
				     struct gc_consumers_txn_trigger_data);
	trg_data->uuid = *replica_uuid;
	trg_data->old_def = old_def;
	trg_data->new_def = new_def;

	/* Actual work will be done on commit. */
	struct trigger *on_commit = xregion_alloc_object(&in_txn()->region,
							 struct trigger);
	trigger_create(on_commit, on_replace_dd_gc_consumers_commit, trg_data,
		       NULL);
	txn_stmt_on_commit(stmt, on_commit);
	return 0;
}

int
on_create_dd_gc_consumers_primary_index(void)
{
	/*
	 * No-op on recovery (both local and remote) - fill the space
	 * only when it is created on upgrade.
	 */
	if (recovery_state != FINISHED_RECOVERY)
		return 0;
	/* Make sure we are in txn - yield won't happen and loop is safe. */
	assert(in_txn() != NULL);
	gc_consumer_hash_foreach(&gc.consumers_hash, consumer) {
		/* Insert the consumer to _gc_consumers. */
		char tuple_buf[VCLOCK_STR_LEN_MAX + UUID_STR_LEN + 30];
		char *data = tuple_buf;
		data = mp_encode_array(data, 2);
		data = mp_encode_str0(data, tt_uuid_str(&consumer->uuid));
		data = mp_encode_vclock_ignore0(data, &consumer->vclock);
		assert((size_t)(data - tuple_buf) < sizeof(tuple_buf));

		if (box_insert(BOX_GC_CONSUMERS_ID, tuple_buf, data, NULL) != 0)
			return -1;
	}
	return 0;
}

struct gc_consumer *
gc_consumer_iterator_next(struct gc_consumer_iterator *it)
{
	if (it->curr != NULL)
		it->curr = gc_tree_next(&gc.active_consumers, it->curr);
	else
		it->curr = gc_tree_first(&gc.active_consumers);
	return it->curr;
}
