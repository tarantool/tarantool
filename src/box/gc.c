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
#include "tt_uuid.h"
#include "tt_static.h"
#include "box.h"
#include "txn.h"
#include "xrow.h"
#include "tuple.h"
#include "space_cache.h"

struct gc_state gc;

static int
gc_cleanup_fiber_f(va_list);
static int
gc_checkpoint_fiber_f(va_list);
static int
gc_sync_fiber_f(va_list);

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
       struct gc_consumer, in_active_consumers, gc_consumer_cmp);

/** Free a consumer object. */
static void
gc_consumer_delete(struct gc_consumer *consumer)
{
	TRASH(consumer);
	free(consumer);
}

static bool
gc_schema_supports_persistent_consumers(void)
{
	struct space *space = space_by_id(BOX_GC_CONSUMERS_ID);
	if (space == NULL)
		return false;
	return space_index(space, 0) != NULL;
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

	gc.wal_cleanup_delay = 4 * 3600;
	gc.delay_ref = 0;
	gc.is_paused = true;
	say_info("wal/engine cleanup is paused");

	vclock_create(&gc.vclock);
	rlist_create(&gc.checkpoints);
	rlist_create(&gc.consumers);
	gc_tree_new(&gc.active_consumers);
	fiber_cond_create(&gc.cleanup_cond);
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

	gc.sync_fiber = fiber_new_system("gc_sync",
					 gc_sync_fiber_f);
	if (gc.sync_fiber == NULL)
		panic("failed to start gc sync fiber");
	fiber_set_joinable(gc.sync_fiber, true);

	gc.on_garbage_collection = on_garbage_collection;

	fiber_start(gc.cleanup_fiber);
	fiber_start(gc.checkpoint_fiber);
	fiber_start(gc.sync_fiber);
}

void
gc_shutdown(void)
{
	fiber_cancel(gc.checkpoint_fiber);
	fiber_cancel(gc.cleanup_fiber);
	fiber_cancel(gc.sync_fiber);
	fiber_join(gc.checkpoint_fiber);
	gc.checkpoint_fiber = NULL;
	fiber_join(gc.cleanup_fiber);
	gc.cleanup_fiber = NULL;
	fiber_join(gc.sync_fiber);
	gc.sync_fiber = NULL;
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
	struct gc_consumer *consumer, *next_consumer;
	rlist_foreach_entry_safe(consumer, &gc.consumers, in_consumers,
				 next_consumer) {
		gc_consumer_delete(consumer);
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
	if (gc.cleanup_fiber == NULL)
		return;
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
		if (vclock_compare_ignore0(vclock, &consumer->vclock) <= 0) {
			consumer = next;
			continue;
		}
		assert(!consumer->is_inactive);
		consumer->is_inactive = true;
		gc_tree_remove(&gc.active_consumers, consumer);

		say_crit("deactivated WAL consumer %s at %s",
			 gc_consumer_name(consumer),
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

/**
 * Return the checkpoint with exact @vclock. NULL is returned if
 * checkpoint is not found.
 */
struct gc_checkpoint *
gc_checkpoint_at_vclock(const struct vclock *vclock)
{
	struct gc_checkpoint *checkpoint;
	gc_foreach_checkpoint(checkpoint) {
		int rc = vclock_compare(&checkpoint->vclock, vclock);
		if (rc > 0)
			break;
		if (rc == 0)
			return checkpoint;
	}
	return NULL;
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

static struct gc_consumer *
gc_consumer_by_uuid(const struct tt_uuid *uuid)
{
	struct gc_consumer *consumer;
	rlist_foreach_entry(consumer, &gc.consumers, in_consumers) {
		if (tt_uuid_is_equal(&consumer->uuid, uuid))
			return consumer;
	}
	return NULL;
}

static struct gc_consumer *
gc_consumer_register_impl(const struct vclock *vclock,
			  enum gc_consumer_type type,
			  const struct tt_uuid *uuid)
{
	assert(gc_consumer_by_uuid(uuid) == NULL);
	struct gc_consumer *consumer = xmalloc(sizeof(*consumer));
	memset(consumer, 0, sizeof(*consumer));

	consumer->type = type;
	consumer->uuid = *uuid;

	vclock_copy(&consumer->vclock, vclock);
	gc_tree_insert(&gc.active_consumers, consumer);
	rlist_add_entry(&gc.consumers, consumer, in_consumers);
	consumer->is_orphan = true;
	return consumer;
}

struct gc_consumer *
gc_consumer_register(const struct vclock *vclock, enum gc_consumer_type type,
		     const struct tt_uuid *uuid)
{
	struct gc_consumer *consumer = gc_consumer_by_uuid(uuid);
	if (consumer != NULL) {
		assert(consumer->is_orphan);
		consumer->is_orphan = false;
		if (!consumer->is_inactive) {
			/* Take vclock as a component-wise minimum. */
			gc_tree_remove(&gc.active_consumers, consumer);
			vclock_min(&consumer->vclock, vclock);
			gc_tree_insert(&gc.active_consumers, consumer);
		}
		return consumer;
	}
	consumer = gc_consumer_register_impl(vclock, type, uuid);
	consumer->is_orphan = false;
	return consumer;
}

static void
gc_consumer_unregister_impl(struct gc_consumer *consumer)
{
	assert(consumer->is_orphan);
	assert(!consumer->is_persistent);
	if (!consumer->is_inactive) {
		gc_tree_remove(&gc.active_consumers, consumer);
		gc_schedule_cleanup();
	}
	rlist_del_entry(consumer, in_consumers);
	gc_consumer_delete(consumer);
}

void
gc_consumer_unregister(struct gc_consumer *consumer)
{
	consumer->is_orphan = true;
	if (!consumer->is_persistent)
		gc_consumer_unregister_impl(consumer);
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
	struct gc_consumer *next = gc_tree_next(&gc.active_consumers, consumer);
	bool update_tree = (next != NULL &&
			    vclock_lex_compare(vclock, &next->vclock) >= 0);

	if (update_tree)
		gc_tree_remove(&gc.active_consumers, consumer);

	vclock_copy(&consumer->vclock, vclock);

	if (update_tree)
		gc_tree_insert(&gc.active_consumers, consumer);

	consumer->is_synced = false;
	gc.need_sync = true;
	fiber_wakeup(gc.sync_fiber);
	gc_schedule_cleanup();
}

/** String representation of enum gc_consumer_type. */
static const char * const gc_consumer_type_strs[] = {
	[GC_CONSUMER_REPLICA] = "replica",
};

const char *
gc_consumer_name(struct gc_consumer *consumer)
{
	return tt_sprintf("%s %s", gc_consumer_type_strs[consumer->type],
			  tt_uuid_str(&consumer->uuid));
}

/**
 * Write the consumer to persistent WAL GC state. The function must be called
 * only when schema supports persistent consumers. If consumer wasn't in space
 * _gc_consumers, the new row is created.
 */
static int
gc_consumer_persist_impl(struct gc_consumer *consumer)
{
	assert(gc_schema_supports_persistent_consumers());

	/* Mark consumer as synchronized with persistent state. */
	consumer->is_synced = true;
	char tuple_buf[UUID_STR_LEN + VCLOCK_STR_LEN_MAX + GC_NAME_MAX + 30];
	char *tuple_end = tuple_buf;
	tuple_end = mp_encode_array(tuple_end, 3);
	tuple_end = mp_encode_str0(tuple_end, tt_uuid_str(&consumer->uuid));
	tuple_end = mp_encode_vclock_ignore0(tuple_end, &consumer->vclock);
	tuple_end = mp_encode_map(tuple_end, 1);
	tuple_end = mp_encode_str0(tuple_end, "type");
	tuple_end = mp_encode_str0(tuple_end,
				   gc_consumer_type_strs[consumer->type]);
	assert((unsigned long)(tuple_end - tuple_buf) < sizeof(tuple_buf));

	return box_replace(BOX_GC_CONSUMERS_ID, tuple_buf, tuple_end, NULL);
}

int
gc_consumer_persist(struct gc_consumer *consumer)
{
	if (gc_schema_supports_persistent_consumers()) {
		struct txn *txn = txn_begin();
		if (txn == NULL)
			return -1;
		/*
		 * No need to wait limbo, so let's set TXN_FORCE_ASYNC.
		 * Moreover, the function can be called when quorum cannot be
		 * collected, (for example, on IPROTO_SUBSCRIBE), so we must set
		 * this flag here.
		 */
		txn_set_flags(txn, TXN_FORCE_ASYNC);
		if (gc_consumer_persist_impl(consumer) != 0) {
			txn_abort(txn);
			return -1;
		}
		if (txn_commit(txn) != 0)
			return -1;
	}
	/* Unref WAL GC delay if being persisted for the first time. */
	if (!consumer->is_persistent)
		gc_delay_unref();
	/*
	 * Mark consumer as persistent even if schema does not support
	 * persistent consumers - in this case it will be actually persisted
	 * on schema upgrade.
	 */
	consumer->is_persistent = true;
	return 0;
}

static void
gc_erase_consumer_finalize(struct gc_consumer *consumer)
{
	if (consumer != NULL) {
		consumer->is_persistent = false;
		if (consumer->is_orphan)
			gc_consumer_unregister_impl(consumer);
	}
}

/**
 * Mark consumer as not persistent and unregister it if unused.
 */
static int
gc_erase_consumer_on_commit(struct trigger *trigger, void *event)
{
	(void)event;
	gc_erase_consumer_finalize((struct gc_consumer *)trigger->data);
	return 0;
}

int
gc_erase_consumer(const struct tt_uuid *uuid)
{
	struct gc_consumer *consumer = gc_consumer_by_uuid(uuid);
	if (!gc_schema_supports_persistent_consumers()) {
		if (consumer != NULL)
			consumer->is_persistent = false;
		return 0;
	}

	if (boxk(IPROTO_DELETE, BOX_GC_CONSUMERS_ID, "[%s]",
		 tt_uuid_str(uuid)) != 0)
		return -1;

	if (in_txn() != NULL) {
		/* Do actual work on commit. */
		struct trigger *on_commit =
			xregion_alloc_object(&in_txn()->region, struct trigger);
		trigger_create(on_commit, gc_erase_consumer_on_commit, consumer,
			       NULL);
		txn_stmt_on_commit(txn_current_stmt(in_txn()), on_commit);
	} else {
		gc_erase_consumer_finalize(consumer);
	}
	return 0;
}

/**
 * Synchronize persistent WAL GC state with given consumer. The function
 * must be called only when schema supports persistent consumers. Note that
 * the function only updates row in a system space, so the new consumer
 * won't be created. This function is used in background fiber so it's
 * important to disallow insertions of new tuples - in this case background
 * fiber is unable to create a new row in space _gc_consumers and we don't
 * need to bother if it can insert a tuple when it's not supposed to (for
 * example, when we deleted consumer but hasn't committed the change yet, so
 * it's still marked as persistent).
 */
static int
gc_consumer_sync_impl(struct gc_consumer *consumer)
{
	assert(gc_schema_supports_persistent_consumers());

	/* Mark consumer as synchronized with persistent state. */
	consumer->is_synced = true;
	char key_buf[UUID_STR_LEN + 10];
	char *key_end = key_buf;
	key_end = mp_encode_array(key_buf, 1);
	key_end = mp_encode_str0(key_end, tt_uuid_str(&consumer->uuid));
	assert((unsigned long)(key_end - key_buf) < sizeof(key_buf));

	char ops_buf[VCLOCK_STR_LEN_MAX + 30];
	char *ops_end = ops_buf;
	ops_end = mp_encode_array(ops_end, 1);
	ops_end = mp_encode_array(ops_end, 3);
	ops_end = mp_encode_str0(ops_end, "=");
	ops_end = mp_encode_uint(ops_end, 1);
	ops_end = mp_encode_vclock_ignore0(ops_end, &consumer->vclock);
	assert((unsigned long)(ops_end - ops_buf) < sizeof(ops_buf));

	return box_update(BOX_GC_CONSUMERS_ID, 0, key_buf, key_end, ops_buf,
			  ops_end, 0, NULL);
}

/**
 * Writes all persistent gc consumers that are not marked as synchronized.
 * No-op if schema does not support persistent consumers.
 */
static int
gc_sync(void)
{
	if (!gc_schema_supports_persistent_consumers())
		return 0;

	int rc = 0;
	struct txn *txn = txn_begin();
	if (txn == NULL)
		return -1;
	/* No need to wait limbo, so let's set TXN_FORCE_ASYNC here. */
	txn_set_flags(txn, TXN_FORCE_ASYNC);

	struct gc_consumer *consumer;
	rlist_foreach_entry(consumer, &gc.consumers, in_consumers) {
		if (consumer->is_persistent &&
		    !consumer->is_synced &&
		    gc_consumer_sync_impl(consumer) != 0)
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

static int
gc_sync_fiber_f(va_list ap)
{
	(void)ap;
	while (!fiber_is_cancelled()) {
		gc.need_sync = false;
		int rc = gc_sync();
		if (rc != 0) {
			say_error("Failed to update persistent WAL GC state");
			diag_log();
		}
		fiber_check_gc();
		/* Another loop if we have consumers to be persisted. */
		if (gc.need_sync)
			continue;
		fiber_sleep(TIMEOUT_INFINITY);
	}
	return 0;
}

/** GC consumer definition. */
struct gc_consumer_def {
	/** Instance UUID. */
	struct tt_uuid uuid;
	/** Instance vclock. */
	struct vclock vclock;
	/** Description of object owning this consumer. */
	enum gc_consumer_type type;
};

/** Mapping from tuple.opts to fields of gc_consumer_def. */
const struct opt_def gc_consumer_def_opts_reg[] = {
	OPT_DEF_ENUM("type", gc_consumer_type, struct gc_consumer_def, type, NULL),
	OPT_END,
};

/**
 * Decode gc_consumer definition from a _gc_consumers' tuple.
 */
static int
gc_consumer_def_decode(struct tuple *tuple, struct gc_consumer_def *def)
{
	memset(def, 0, sizeof(*def));
	if (tuple_field_uuid(tuple, BOX_GC_CONSUMERS_FIELD_UUID, &def->uuid) != 0)
		return -1;
	if (tt_uuid_is_nil(&def->uuid)) {
		diag_set(ClientError, ER_INVALID_UUID, tt_uuid_str(&def->uuid));
		return -1;
	}
	const char *mp_vclock = tuple_field_with_type(
		tuple, BOX_GC_CONSUMERS_FIELD_VCLOCK, MP_MAP);
	if (mp_vclock == NULL)
		return -1;
	/* Save vclock to report an error. */
	const char *saved_vclock = mp_vclock;
	if (mp_decode_vclock_ignore0(&mp_vclock, &def->vclock) != 0) {
		diag_set(ClientError, ER_INVALID_VCLOCK, mp_str(saved_vclock));
		return -1;
	}
	const char *opts = NULL;
	opts = tuple_field_with_type(tuple, BOX_GC_CONSUMERS_FIELD_OPTS,
				     MP_MAP);
	if (opts == NULL)
		return -1;

	/* Initialize and decode opts. */
	def->type = gc_consumer_type_MAX;
	if (opts_decode(def, gc_consumer_def_opts_reg, &opts, NULL) != 0)
		return -1;
	if (def->type == gc_consumer_type_MAX) {
		diag_set(IllegalParams, "GC consumer %s has invalid type",
			 tt_uuid_str(&def->uuid));
		return -1;
	}
	return 0;
}

int
gc_load_consumers(void)
{
	if (!gc_schema_supports_persistent_consumers())
		return 0;
	struct space *space = space_by_id(BOX_GC_CONSUMERS_ID);
	assert(space != NULL);
	struct index *index = space_index(space, 0);
	assert(index != NULL);
	struct iterator *it = index_create_iterator(index, ITER_ALL, NULL, 0);
	struct tuple *tuple = NULL;
	struct gc_consumer_def def;
	int rc = 0;
	while ((rc = iterator_next(it, &tuple)) == 0 && tuple != NULL) {
		if (gc_consumer_def_decode(tuple, &def) != 0) {
			const char *uuid = tuple_field_cstr(
				tuple, BOX_GC_CONSUMERS_FIELD_UUID);
			say_error("Error while recovering GC consumer %s",
				  uuid);
			diag_log();
			continue;
		}
		struct gc_consumer *consumer = gc_consumer_register_impl(
			&def.vclock, def.type, &def.uuid);
		consumer->is_persistent = true;
		/* Unref WAL GC delay for each replica having a consumer. */
		gc_delay_unref();
	}
	iterator_delete(it);
	return rc;
}

int
gc_persist_consumers(void)
{
	assert(gc_schema_supports_persistent_consumers());
	struct gc_consumer *consumer;
	rlist_foreach_entry(consumer, &gc.consumers, in_consumers) {
		if (consumer->is_persistent &&
		    gc_consumer_persist_impl(consumer) != 0)
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
