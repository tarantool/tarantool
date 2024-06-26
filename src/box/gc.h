#ifndef TARANTOOL_BOX_GC_H_INCLUDED
#define TARANTOOL_BOX_GC_H_INCLUDED
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

#include <stdbool.h>
#include <stddef.h>
#include <small/rlist.h>

#include "fiber_cond.h"
#include "vclock/vclock.h"
#include "trivia/util.h"
#include "checkpoint_schedule.h"
#include "tt_uuid.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct fiber;
struct gc_consumer;
struct trigger;

enum { GC_NAME_MAX = 64 };

typedef rb_node(struct gc_consumer) gc_node_t;

/**
 * Garbage collector keeps track of all preserved checkpoints.
 * The following structure represents a checkpoint.
 */
struct gc_checkpoint {
	/** Link in gc_state::checkpoints. */
	struct rlist in_checkpoints;
	/** VClock of the checkpoint. */
	struct vclock vclock;
	/**
	 * List of checkpoint references, linked by
	 * gc_checkpoint_ref::in_refs.
	 *
	 * We use a list rather than a reference counter so
	 * that we can list reference names in box.info.gc().
	 */
	struct rlist refs;
};

/**
 * The following structure represents a checkpoint reference.
 * See also gc_checkpoint::refs.
 */
struct gc_checkpoint_ref {
	/** Link in gc_checkpoint::refs. */
	struct rlist in_refs;
	/** Human-readable name of this checkpoint reference. */
	char name[GC_NAME_MAX];
};

/**
 * The object of this type is used to prevent garbage
 * collection from removing WALs that are still in use.
 */
struct gc_consumer {
	/** Link in gc_state::consumers_hash. */
	gc_node_t in_hash;
	/** Link in gc_state::active_consumers. */
	gc_node_t in_active;
	/**
	 * UUID of object associated with the consumer.
	 * Is uuid_nil if the consumer is anonymous.
	 */
	struct tt_uuid uuid;
	/** Human-readable name. */
	char name[GC_NAME_MAX];
	/**
	 * The vclock tracked by this consumer. Is consistent
	 * with persistent state.
	 */
	struct vclock vclock;
	/** See `is_async_updated` for details. */
	struct vclock volatile_vclock;
	/**
	 * This flag is set when the consumer was asynchronously updated.
	 * In this case background fiber will persist `volatile_vclock`
	 * when it isn't consistent with `vclock`.
	 */
	bool is_async_updated;
	/**
	 * This flag is set if a WAL needed by this consumer was
	 * deleted by the WAL thread on ENOSPC.
	 */
	bool is_inactive;
};

typedef rb_tree(struct gc_consumer) gc_tree_t;

typedef void
(*on_garbage_collection_f)(void);

/** Garbage collection state. */
struct gc_state {
	/** VClock of the oldest WAL row available on the instance. */
	struct vclock vclock;
	/** A callback invoked whenever gc.vclock is updated. */
	on_garbage_collection_f on_garbage_collection;
	/**
	 * Minimal number of checkpoints to preserve.
	 * Configured by box.cfg.checkpoint_count.
	 */
	int min_checkpoint_count;
	/**
	 * Number of preserved checkpoints. May be greater than
	 * @min_checkpoint_count, because some checkpoints may
	 * be in use by replication or backup.
	 */
	int checkpoint_count;
	/**
	 * List of preserved checkpoints. New checkpoints are added
	 * to the tail. Linked by gc_checkpoint::in_checkpoints.
	 */
	struct rlist checkpoints;
	/**
	 * All non-anonymous consumers indexed by uuid.
	 * Linked by gc_consumer::in_hash.
	 */
	gc_tree_t consumers_hash;
	/**
	 * Registered consumers indexed by vclock.
	 * Linked by gc_consumer::in_active.
	 */
	gc_tree_t active_consumers;
	/** Fiber responsible for periodic checkpointing. */
	struct fiber *checkpoint_fiber;
	/** Schedule of periodic checkpoints. */
	struct checkpoint_schedule checkpoint_schedule;
	/** Fiber that removes old files in the background. */
	struct fiber *cleanup_fiber;
	/**
	 * Condition variable signaled by the cleanup fiber
	 * whenever it completes a round of garbage collection.
	 * Used to wait for garbage collection to complete.
	 */
	struct fiber_cond cleanup_cond;
	/**
	 * The following two members are used for scheduling
	 * background garbage collection and waiting for it to
	 * complete. To trigger background garbage collection,
	 * @scheduled is incremented. Whenever a round of garbage
	 * collection completes, @completed is incremented. Thus
	 * to wait for background garbage collection scheduled
	 * at a particular moment of time to complete, one should
	 * sleep until @completed reaches the value of @scheduled
	 * taken at that moment of time.
	 */
	int64_t cleanup_completed, cleanup_scheduled;
	/**
	 * Fiber that updates persistent WAL GC state
	 * asynchronously.
	 */
	struct fiber *persist_fiber;
	/**
	 * Condition variable signaled by the persist fiber
	 * whenever it completes or fails a round. Used to wait for
	 * writes to complete.
	 */
	struct fiber_cond persist_cond;
	/**
	 * The following two members are used for scheduling
	 * fiber persisting gc consumers. To trigger the fiber,
	 * @scheduled is incremented. Whenever a round of garbage
	 * collection completes, @completed is incremented. On
	 * every failure @failed is incremented.
	 */
	uint64_t persist_completed, persist_scheduled, persist_failed;
	/**
	 * A counter to wait until all replicas are managed to
	 * subscribe so that we can enable cleanup fiber to
	 * remove old XLOGs. Otherwise some replicas might be
	 * far behind the master node and after the master
	 * node been restarted they will have to reread all
	 * data back due to XlogGapError, ie too early deleted
	 * XLOGs.
	 */
	int64_t delay_ref;
	/**
	 * Delay timeout in seconds.
	 */
	double wal_cleanup_delay;
	/**
	 * When set the cleanup fiber is paused.
	 */
	bool is_paused;
	/**
	 * Set if there's a fiber making a checkpoint right now.
	 */
	bool checkpoint_is_in_progress;
	/**
	 * If this flag is set, the checkpoint daemon should create
	 * a checkpoint as soon as possible despite the schedule.
	 */
	bool checkpoint_is_pending;
};
extern struct gc_state gc;

/**
 * Iterate over all checkpoints tracked by the garbage collector,
 * starting from the oldest and ending with the newest.
 */
#define gc_foreach_checkpoint(checkpoint) \
	rlist_foreach_entry(checkpoint, &gc.checkpoints, in_checkpoints)

/**
 * Iterate over all checkpoints tracked by the garbage collector
 * in the reverse order, that is starting from the newest and
 * ending with the oldest.
 */
#define gc_foreach_checkpoint_reverse(checkpoint) \
	rlist_foreach_entry_reverse(checkpoint, &gc.checkpoints, in_checkpoints)

/**
 * Iterate over all references to the given checkpoint.
 */
#define gc_foreach_checkpoint_ref(ref, checkpoint) \
	rlist_foreach_entry(ref, &(checkpoint)->refs, in_refs)

/**
 * Return the last (newest) checkpoint known to the garbage
 * collector. If there's no checkpoint, return NULL.
 */
static inline struct gc_checkpoint *
gc_last_checkpoint(void)
{
	if (rlist_empty(&gc.checkpoints))
		return NULL;

	return rlist_last_entry(&gc.checkpoints, struct gc_checkpoint,
				in_checkpoints);
}

/**
 * Initialize the garbage collection state.
 */
void
gc_init(on_garbage_collection_f on_garbage_collection);

/**
 * Prepare for freeing resources in gc_free while TX event loop is
 * still running.
 */
void
gc_shutdown(void);

/**
 * Destroy the garbage collection state.
 */
void
gc_free(void);

/**
 * Set a new delay value.
 */
void
gc_set_wal_cleanup_delay(double wal_cleanup_delay);

/**
 * Increment a reference to delay counter.
 */
void
gc_delay_ref(void);

/**
 * Decrement a reference from the delay counter.
 */
void
gc_delay_unref(void);

/**
 * Advance the garbage collector vclock to the given position.
 * Deactivate WAL consumers that need older data.
 */
void
gc_advance(const struct vclock *vclock);

/**
 * Update the minimal number of checkpoints to preserve.
 * Called when box.cfg.checkpoint_count is updated.
 *
 * Note, this function doesn't run garbage collector so
 * changes will take effect only after a new checkpoint
 * is created or a consumer is unregistered.
 */
void
gc_set_min_checkpoint_count(int min_checkpoint_count);

/**
 * Set the time interval between checkpoints, in seconds.
 * Setting the interval to 0 disables periodic checkpointing.
 */
void
gc_set_checkpoint_interval(double interval);

/**
 * Track an existing checkpoint in the garbage collector state.
 * Note, this function may trigger garbage collection to remove
 * old checkpoints.
 */
void
gc_add_checkpoint(const struct vclock *vclock);

/**
 * Make a *manual* checkpoint.
 * This is entry point for box.snapshot() and SIGUSR1 signal
 * handler.
 *
 * This function runs engine/WAL callbacks to create a checkpoint
 * on disk, then tracks the new checkpoint in the garbage collector
 * state (see gc_add_checkpoint()).
 *
 * Returns 0 on success. On failure returns -1 and sets diag.
 */
int
gc_checkpoint(void);

/**
 * Trigger background checkpointing.
 *
 * The checkpoint will be created by the checkpoint daemon.
 */
void
gc_trigger_checkpoint(void);

/**
 * Get a reference to @checkpoint and store it in @ref.
 * This will block the garbage collector from deleting
 * the checkpoint files until the reference is released
 * with gc_put_checkpoint_ref().
 *
 * @format... specifies a human-readable name that will be
 * used for listing the reference in box.info.gc().
 */
CFORMAT(printf, 3, 4)
void
gc_ref_checkpoint(struct gc_checkpoint *checkpoint,
		  struct gc_checkpoint_ref *ref, const char *format, ...);

/**
 * Release a reference to a checkpoint previously taken
 * with gc_ref_checkpoint(). This function may trigger
 * garbage collection.
 */
void
gc_unref_checkpoint(struct gc_checkpoint_ref *ref);

/**
 * Synchronously register a dummy gc consumer (does not pin any xlogs).
 * On success, the function returns 0 and the created gc consumer is
 * persisted. Otherwise, returns -1, persistent state is not modified.
 *
 * NB: The function yields if is called outside active transaction.
 */
int
gc_consumer_register_dummy(const struct tt_uuid *uuid);

/**
 * Create a gc consumer not associated with any uuid.
 * Created consumer is returned, and caller must destroy it manually.
 * This method must be used only for short-living consumers.
 */
CFORMAT(printf, 2, 3)
struct gc_consumer *
gc_consumer_register_anonymous(const struct vclock *vclock,
			       const char *format, ...);

/**
 * Returns true if there is a registered consumer with such uuid.
 */
bool
gc_consumer_is_registered(const struct tt_uuid *uuid);

/**
 * Unregister anonymous consumer, it must not be NULL. Invoke
 * garbage collection if needed.
 */
void
gc_consumer_unregister_anonymous(struct gc_consumer *consumer);

/**
 * Synchronously unregister a gc consumer.
 * On success, the function returns 0 and the gc consumer is removed from
 * persistent state. Otherwise, returns -1, persistent state is not modified.
 *
 * NB: The function yields if is called outside active transaction.
 */
int
gc_consumer_unregister(const struct tt_uuid *uuid);

/**
 * Asynchronously update a gc consumer.
 * The function never yields. Any synchronous modification of consumer
 * discards all asynchronous ones.
 */
void
gc_consumer_update_async(const struct tt_uuid *uuid,
			 const struct vclock *vclock);

/**
 * Synchronously update a gc consumer - calls gc_consumer_update_async and waits
 * for its success or failure.
 * On success, the function returns 0 and the gc consumer is updated along with
 * persistent state. Otherwise, returns -1.
 *
 * NB: The function yields and cannot be called inside active transaction. Also,
 * if the function returns error, persistent state may be modified anyway.
 */
int
gc_consumer_update(const struct tt_uuid *uuid, const struct vclock *vclock);

/**
 * Synchronously deactivates a gc consumer - discards all asynchronous updates
 * and makes it dummy.
 * On success, the function returns 0 and the gc consumer is replaced with dummy
 * in persistent state. Otherwise, returns -1, persistent state is not modified.
 *
 * NB: The function yields if is called outside active transaction.
 */
int
gc_consumer_deactivate(const struct tt_uuid *uuid);

/**
 * Returns true if current schema supports persistent gc consumers.
 */
bool
gc_consumer_is_persistent(void);

/**
 * The trigger invoked on replace in space _gc_consumers.
 */
int
on_replace_dd_gc_consumers(struct trigger *trigger, void *event);

/**
 * A callback that should be invoked when the primary index of space
 * _gc_consumers is created.
 */
int
on_create_dd_gc_consumers_primary_index(void);

/**
 * Iterator over active registered consumers. The iterator is valid
 * as long as the caller doesn't yield.
 */
struct gc_consumer_iterator {
	struct gc_consumer *curr;
};

/** Init an iterator over active consumers. */
static inline void
gc_consumer_iterator_init(struct gc_consumer_iterator *it)
{
	it->curr = NULL;
}

/**
 * Iterate to the next active registered consumer. Return a pointer
 * to the next consumer object or NULL if there is no more
 * consumers.
 */
struct gc_consumer *
gc_consumer_iterator_next(struct gc_consumer_iterator *it);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_BOX_GC_H_INCLUDED */
