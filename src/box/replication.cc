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

#include "replication.h"

#include <fiber.h> /* &cord->slabc */
#include <fiber_channel.h>
#include <scoped_guard.h>
#include <small/mempool.h>

#include "box.h"
#include "gc.h"
#include "applier.h"
#include "error.h"
#include "vclock.h" /* VCLOCK_MAX */

struct vclock replicaset_vclock;
uint32_t instance_id = REPLICA_ID_NIL;
/**
 * Globally unique identifier of this replica set.
 * A replica set is a set of appliers and their matching
 * relays, usually connected in full mesh.
 */
struct tt_uuid REPLICASET_UUID;

double replication_timeout = 1.0; /* seconds */

typedef rb_tree(struct replica) replicaset_t;
rb_proto(, replicaset_, replicaset_t, struct replica)

static int
replica_compare_by_uuid(const struct replica *a, const struct replica *b)
{
	return tt_uuid_compare(&a->uuid, &b->uuid);
}

rb_gen(, replicaset_, replicaset_t, struct replica, link,
       replica_compare_by_uuid);

#define replicaset_foreach_safe(set, item, next) \
	for (item = replicaset_first(set); \
	     item != NULL && ((next = replicaset_next(set, item)) || 1); \
	     item = next)

static struct mempool replica_pool;
static replicaset_t replicaset;

/**
 * List of replicas that haven't received a UUID.
 * It contains both replicas that are still trying
 * to connect and those that failed to connect.
 */
static RLIST_HEAD(anon_replicas);

void
replication_init(void)
{
	mempool_create(&replica_pool, &cord()->slabc,
		       sizeof(struct replica));
	memset(&replicaset, 0, sizeof(replicaset));
	replicaset_new(&replicaset);
	vclock_create(&replicaset_vclock);
}

void
replication_free(void)
{
	mempool_destroy(&replica_pool);
}

void
replica_check_id(uint32_t replica_id)
{
        if (replica_id == REPLICA_ID_NIL)
		tnt_raise(ClientError, ER_REPLICA_ID_IS_RESERVED,
			  (unsigned) replica_id);
	if (replica_id >= VCLOCK_MAX)
		tnt_raise(LoggedError, ER_REPLICA_MAX,
			  (unsigned) replica_id);
        if (replica_id == ::instance_id)
		tnt_raise(ClientError, ER_LOCAL_INSTANCE_ID_IS_READ_ONLY,
			  (unsigned) replica_id);
}

/* Return true if replica doesn't have id, relay and applier */
static bool
replica_is_orphan(struct replica *replica)
{
	return replica->id == REPLICA_ID_NIL && replica->applier == NULL &&
	       replica->relay == NULL;
}

static struct replica *
replica_new(void)
{
	struct replica *replica = (struct replica *) mempool_alloc(&replica_pool);
	if (replica == NULL)
		tnt_raise(OutOfMemory, sizeof(*replica), "malloc",
			  "struct replica");
	replica->id = 0;
	replica->uuid = uuid_nil;
	replica->applier = NULL;
	replica->relay = NULL;
	replica->gc = NULL;
	rlist_create(&replica->in_anon);
	trigger_create(&replica->on_connect, NULL, NULL, NULL);
	return replica;
}

static void
replica_delete(struct replica *replica)
{
	assert(replica_is_orphan(replica));
	if (replica->gc != NULL)
		gc_consumer_unregister(replica->gc);
	mempool_free(&replica_pool, replica);
}

struct replica *
replicaset_add(uint32_t replica_id, const struct tt_uuid *replica_uuid)
{
	assert(!tt_uuid_is_nil(replica_uuid));
	assert(replica_id != REPLICA_ID_NIL && replica_id < VCLOCK_MAX);

	assert(replica_by_uuid(replica_uuid) == NULL);
	struct replica *replica = replica_new();
	replica->uuid = *replica_uuid;
	replicaset_insert(&replicaset, replica);
	replica_set_id(replica, replica_id);
	return replica;
}

void
replica_set_id(struct replica *replica, uint32_t replica_id)
{
	assert(replica_id < VCLOCK_MAX);
	assert(replica->id == REPLICA_ID_NIL); /* replica id is read-only */
	replica->id = replica_id;

	if (tt_uuid_is_equal(&INSTANCE_UUID, &replica->uuid)) {
		/* Assign local replica id */
		assert(instance_id == REPLICA_ID_NIL);
		instance_id = replica_id;
	}
}

void
replica_clear_id(struct replica *replica)
{
	assert(replica->id != REPLICA_ID_NIL && replica->id != instance_id);
	/*
	 * Don't remove replicas from vclock here.
	 * The vclock_sum() must always grow, it is a core invariant of
	 * the recovery subsystem. Further attempts to register a replica
	 * with the removed replica_id will re-use LSN from the last value.
	 * Replicas with LSN == 0 also can't not be safely removed.
	 * Some records may arrive later on due to asynchronous nature of
	 * replication.
	 */
	replica->id = REPLICA_ID_NIL;
	if (replica_is_orphan(replica)) {
		replicaset_remove(&replicaset, replica);
		replica_delete(replica);
	}
}

static void
replica_on_receive_uuid(struct trigger *trigger, void *event)
{
	struct replica *replica = container_of(trigger,
				struct replica, on_connect);
	struct applier *applier = (struct applier *)event;

	assert(tt_uuid_is_nil(&replica->uuid));
	assert(replica->applier == applier);

	if (applier->state != APPLIER_CONNECTED)
		return;

	trigger_clear(trigger);

	assert(!tt_uuid_is_nil(&applier->uuid));
	replica->uuid = applier->uuid;

	struct replica *orig = replicaset_search(&replicaset, replica);
	if (orig != NULL && orig->applier != NULL) {
		say_error("duplicate connection to the same replica: "
			  "instance uuid %s, addr1 %s, addr2 %s",
			  tt_uuid_str(&orig->uuid), applier->source,
			  orig->applier->source);
		fiber_cancel(fiber());
		/*
		 * Raise an exception to force the applier
		 * to disconnect.
		 */
		fiber_testcancel();
	}

	rlist_del_entry(replica, in_anon);

	if (orig != NULL) {
		/* Use existing struct replica */
		orig->applier = applier;
		replica->applier = NULL;
		replica_delete(replica);
	} else {
		/* Add a new struct replica */
		replicaset_insert(&replicaset, replica);
	}
}

/**
 * Update the replica set with new "applier" objects
 * upon reconfiguration of box.cfg.replication.
 */
static void
replicaset_update(struct applier **appliers, int count)
{
	replicaset_t uniq;
	memset(&uniq, 0, sizeof(uniq));
	replicaset_new(&uniq);
	RLIST_HEAD(anon_replicas_new);
	struct replica *replica, *next;

	auto uniq_guard = make_scoped_guard([&]{
		replicaset_foreach_safe(&uniq, replica, next) {
			replicaset_remove(&uniq, replica);
			replica_delete(replica);
		}
	});

	/* Check for duplicate UUID */
	for (int i = 0; i < count; i++) {
		struct applier *applier = appliers[i];
		replica = replica_new();
		replica->applier = applier;

		if (applier->state != APPLIER_CONNECTED) {
			/*
			 * The replica has not received its UUID from
			 * the master yet and thus cannot be added to
			 * the replica set. Instead, add it to the list
			 * of anonymous replicas and setup a trigger
			 * that will insert it into the replica set
			 * when it is finally connected.
			 */
			rlist_add_entry(&anon_replicas_new, replica, in_anon);
			trigger_create(&replica->on_connect,
				       replica_on_receive_uuid, NULL, NULL);
			trigger_add(&applier->on_state, &replica->on_connect);
			continue;
		}

		assert(!tt_uuid_is_nil(&applier->uuid));
		replica->uuid = applier->uuid;

		if (replicaset_search(&uniq, replica) != NULL) {
			tnt_raise(ClientError, ER_CFG, "replication",
				  "duplicate connection to the same replica");
		}
		replicaset_insert(&uniq, replica);
	}

	/*
	 * All invariants and conditions are checked, now it is safe to
	 * apply the new configuration. Nothing can fail after this point.
	 */

	/* Prune old appliers */
	replicaset_foreach(replica) {
		if (replica->applier == NULL)
			continue;
		applier_stop(replica->applier); /* cancels a background fiber */
		applier_delete(replica->applier);
		replica->applier = NULL;
	}
	rlist_foreach_entry_safe(replica, &anon_replicas, in_anon, next) {
		assert(replica->applier != NULL);
		applier_stop(replica->applier);
		applier_delete(replica->applier);
		replica->applier = NULL;
		replica_delete(replica);
	}
	rlist_create(&anon_replicas);

	/* Save new appliers */
	replicaset_foreach_safe(&uniq, replica, next) {
		replicaset_remove(&uniq, replica);

		struct replica *orig = replicaset_search(&replicaset, replica);
		if (orig != NULL) {
			/* Use existing struct replica */
			orig->applier = replica->applier;
			assert(tt_uuid_is_equal(&orig->uuid,
						&orig->applier->uuid));
			replica->applier = NULL;
			replica_delete(replica); /* remove temporary object */
			replica = orig;
		} else {
			/* Add a new struct replica */
			replicaset_insert(&replicaset, replica);
		}
	}
	rlist_swap(&anon_replicas, &anon_replicas_new);

	assert(replicaset_first(&uniq) == NULL);
	replicaset_foreach_safe(&replicaset, replica, next) {
		if (replica_is_orphan(replica)) {
			replicaset_remove(&replicaset, replica);
			replica_delete(replica);
		}
	}
}

/**
 * Replica set configuration state, shared among appliers.
 */
struct replicaset_connect_state {
	/** Number of successfully connected appliers. */
	int connected;
	/** Number of appliers that failed to connect. */
	int failed;
	/** Signaled when an applier connects or stops. */
	struct fiber_cond wakeup;
};

struct applier_on_connect {
	struct trigger base;
	struct replicaset_connect_state *state;
};

static void
applier_on_connect_f(struct trigger *trigger, void *event)
{
	struct applier_on_connect *on_connect = container_of(trigger,
					struct applier_on_connect, base);
	struct replicaset_connect_state *state = on_connect->state;
	struct applier *applier = (struct applier *)event;

	switch (applier->state) {
	case APPLIER_OFF:
	case APPLIER_STOPPED:
		state->failed++;
		break;
	case APPLIER_CONNECTED:
		state->connected++;
		break;
	default:
		return;
	}
	fiber_cond_signal(&state->wakeup);
	applier_pause(applier);
}

void
replicaset_connect(struct applier **appliers, int count, int quorum,
		   double timeout)
{
	if (count == 0) {
		/* Cleanup the replica set. */
		replicaset_update(appliers, count);
		return;
	}

	quorum = MIN(quorum, count);

	/*
	 * Simultaneously connect to remote peers to receive their UUIDs
	 * and fill the resulting set:
	 *
	 * - create a single control channel;
	 * - register a trigger in each applier to wake up our
	 *   fiber via this channel when the remote peer becomes
	 *   connected and a UUID is received;
	 * - wait up to CONNECT_TIMEOUT seconds for `count` messages;
	 * - on timeout, raise a CFG error, cancel and destroy
	 *   the freshly created appliers (done in a guard);
	 * - an success, unregister the trigger, check the UUID set
	 *   for duplicates, fill the result set, return.
	 */

	/* Memory for on_state triggers registered in appliers */
	struct applier_on_connect triggers[VCLOCK_MAX];

	struct replicaset_connect_state state;
	state.connected = state.failed = 0;
	fiber_cond_create(&state.wakeup);

	/* Add triggers and start simulations connection to remote peers */
	for (int i = 0; i < count; i++) {
		struct applier *applier = appliers[i];
		struct applier_on_connect *trigger = &triggers[i];
		/* Register a trigger to wake us up when peer is connected */
		trigger_create(&trigger->base, applier_on_connect_f, NULL, NULL);
		trigger->state = &state;
		trigger_add(&applier->on_state, &trigger->base);
		/* Start background connection */
		applier_start(applier);
	}

	while (state.connected < quorum && count - state.failed >= quorum) {
		double wait_start = ev_monotonic_now(loop());
		if (fiber_cond_wait_timeout(&state.wakeup, timeout) != 0)
			break;
		timeout -= ev_monotonic_now(loop()) - wait_start;
	}
	if (state.connected < quorum) {
		/* Timeout or connection failure. */
		goto error;
	}

	for (int i = 0; i < count; i++) {
		/* Unregister the temporary trigger used to wake us up */
		trigger_clear(&triggers[i].base);
		/*
		 * Stop appliers that failed to connect.
		 * They will be restarted once we proceed
		 * to 'subscribe', see replicaset_follow().
		 */
		struct applier *applier = appliers[i];
		if (applier->state != APPLIER_CONNECTED)
			applier_stop(applier);
	}

	/* Now all the appliers are connected, update the replica set. */
	replicaset_update(appliers, count);
	return;
error:
	/* Destroy appliers */
	for (int i = 0; i < count; i++) {
		trigger_clear(&triggers[i].base);
		applier_stop(appliers[i]);
	}

	/* ignore original error */
	tnt_raise(ClientError, ER_CFG, "replication",
		  "failed to connect to one or more replicas");
}

void
replicaset_follow(void)
{
	struct replica *replica;
	replicaset_foreach(replica) {
		/* Resume connected appliers. */
		if (replica->applier != NULL)
			applier_resume(replica->applier);
	}
	rlist_foreach_entry(replica, &anon_replicas, in_anon) {
		/* Restart appliers that failed to connect. */
		applier_start(replica->applier);
	}
}

void
replica_set_relay(struct replica *replica, struct relay *relay)
{
	assert(replica->id != REPLICA_ID_NIL);
	assert(replica->relay == NULL);
	replica->relay = relay;
}

void
replica_clear_relay(struct replica *replica)
{
	assert(replica->relay != NULL);
	replica->relay = NULL;
	if (replica_is_orphan(replica)) {
		replicaset_remove(&replicaset, replica);
		replica_delete(replica);
	}
}

struct replica *
replicaset_first(void)
{
	return replicaset_first(&replicaset);
}

struct replica *
replicaset_next(struct replica *replica)
{
	return replicaset_next(&replicaset, replica);
}

struct replica *
replica_by_uuid(const struct tt_uuid *uuid)
{
	struct replica key;
	key.uuid = *uuid;
	return replicaset_search(&replicaset, &key);
}
