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

uint32_t instance_id = REPLICA_ID_NIL;
struct tt_uuid INSTANCE_UUID;
struct tt_uuid REPLICASET_UUID;

double replication_timeout = 1.0; /* seconds */
double replication_connect_timeout = 4.0; /* seconds */
int replication_connect_quorum = REPLICATION_CONNECT_QUORUM_ALL;
double replication_sync_lag = 10.0; /* seconds */
bool replication_skip_conflict = false;

struct replicaset replicaset;

static int
replica_compare_by_uuid(const struct replica *a, const struct replica *b)
{
	return tt_uuid_compare(&a->uuid, &b->uuid);
}

rb_gen(MAYBE_UNUSED static, replica_hash_, replica_hash_t,
       struct replica, in_hash, replica_compare_by_uuid);

#define replica_hash_foreach_safe(hash, item, next) \
	for (item = replica_hash_first(hash); \
	     item != NULL && ((next = replica_hash_next(hash, item)) || 1); \
	     item = next)

/**
 * Return the number of replicas that have to be synchronized
 * in order to form a quorum in the replica set.
 */
static inline int
replicaset_quorum(void)
{
	return MIN(replication_connect_quorum, replicaset.applier.total);
}

void
replication_init(void)
{
	memset(&replicaset, 0, sizeof(replicaset));
	mempool_create(&replicaset.pool, &cord()->slabc,
		       sizeof(struct replica));
	replica_hash_new(&replicaset.hash);
	rlist_create(&replicaset.anon);
	vclock_create(&replicaset.vclock);
	fiber_cond_create(&replicaset.applier.cond);
}

void
replication_free(void)
{
	mempool_destroy(&replicaset.pool);
	fiber_cond_destroy(&replicaset.applier.cond);
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

static void
replica_on_applier_state_f(struct trigger *trigger, void *event);

static struct replica *
replica_new(void)
{
	struct replica *replica = (struct replica *)
			mempool_alloc(&replicaset.pool);
	if (replica == NULL)
		tnt_raise(OutOfMemory, sizeof(*replica), "malloc",
			  "struct replica");
	replica->id = 0;
	replica->uuid = uuid_nil;
	replica->applier = NULL;
	replica->relay = NULL;
	replica->gc = NULL;
	rlist_create(&replica->in_anon);
	trigger_create(&replica->on_applier_state,
		       replica_on_applier_state_f, NULL, NULL);
	replica->state = REPLICA_DISCONNECTED;
	return replica;
}

static void
replica_delete(struct replica *replica)
{
	assert(replica_is_orphan(replica));
	if (replica->gc != NULL)
		gc_consumer_unregister(replica->gc);
	mempool_free(&replicaset.pool, replica);
}

struct replica *
replicaset_add(uint32_t replica_id, const struct tt_uuid *replica_uuid)
{
	assert(!tt_uuid_is_nil(replica_uuid));
	assert(replica_id != REPLICA_ID_NIL && replica_id < VCLOCK_MAX);

	assert(replica_by_uuid(replica_uuid) == NULL);
	struct replica *replica = replica_new();
	replica->uuid = *replica_uuid;
	replica_hash_insert(&replicaset.hash, replica);
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
		replica_hash_remove(&replicaset.hash, replica);
		replica_delete(replica);
	}
}

static void
replica_set_applier(struct replica *replica, struct applier *applier)
{
	assert(replica->applier == NULL);
	replica->applier = applier;
	trigger_add(&replica->applier->on_state,
		    &replica->on_applier_state);
}

static void
replica_clear_applier(struct replica *replica)
{
	assert(replica->applier != NULL);
	replica->applier = NULL;
	trigger_clear(&replica->on_applier_state);
}

static void
replica_on_applier_sync(struct replica *replica)
{
	assert(replica->state == REPLICA_CONNECTED);

	replica->state = REPLICA_SYNCED;
	replicaset.applier.synced++;

	replicaset_check_quorum();
}

static void
replica_on_applier_connect(struct replica *replica)
{
	struct applier *applier = replica->applier;

	assert(tt_uuid_is_nil(&replica->uuid));
	assert(!tt_uuid_is_nil(&applier->uuid));
	assert(replica->state == REPLICA_DISCONNECTED);

	replica->uuid = applier->uuid;

	struct replica *orig = replica_hash_search(&replicaset.hash, replica);
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
		replica_set_applier(orig, applier);
		replica_clear_applier(replica);
		replica_delete(replica);
		replica = orig;
	} else {
		/* Add a new struct replica */
		replica_hash_insert(&replicaset.hash, replica);
	}

	replica->state = REPLICA_CONNECTED;
	replicaset.applier.connected++;
}

static void
replica_on_applier_reconnect(struct replica *replica)
{
	struct applier *applier = replica->applier;

	assert(!tt_uuid_is_nil(&replica->uuid));
	assert(!tt_uuid_is_nil(&applier->uuid));
	assert(replica->state == REPLICA_DISCONNECTED);

	if (!tt_uuid_is_equal(&replica->uuid, &applier->uuid)) {
		/*
		 * Master's UUID changed, most likely because it was
		 * rebootstrapped. Try to look up a replica matching
		 * the new UUID and reassign the applier to it.
		 */
		struct replica *orig = replica_by_uuid(&applier->uuid);
		if (orig == NULL) {
			orig = replica_new();
			orig->uuid = applier->uuid;
			replica_hash_insert(&replicaset.hash, orig);
		}

		if (orig->applier != NULL) {
			tnt_raise(ClientError, ER_CFG, "replication",
				  "duplicate connection to the same replica");
		}

		replica_set_applier(orig, applier);
		replica_clear_applier(replica);
		replica->state = REPLICA_DISCONNECTED;
		replica = orig;
	}

	replica->state = REPLICA_CONNECTED;
	replicaset.applier.connected++;
}

static void
replica_on_applier_disconnect(struct replica *replica)
{
	switch (replica->state) {
	case REPLICA_SYNCED:
		assert(replicaset.applier.synced > 0);
		replicaset.applier.synced--;
		FALLTHROUGH;
	case REPLICA_CONNECTED:
		assert(replicaset.applier.connected > 0);
		replicaset.applier.connected--;
		break;
	case REPLICA_DISCONNECTED:
		break;
	default:
		unreachable();
	}
	replica->state = REPLICA_DISCONNECTED;
}

static void
replica_on_applier_state_f(struct trigger *trigger, void *event)
{
	(void)event;
	struct replica *replica = container_of(trigger,
			struct replica, on_applier_state);
	switch (replica->applier->state) {
	case APPLIER_CONNECTED:
		if (tt_uuid_is_nil(&replica->uuid))
			replica_on_applier_connect(replica);
		else
			replica_on_applier_reconnect(replica);
		break;
	case APPLIER_DISCONNECTED:
		replica_on_applier_disconnect(replica);
		break;
	case APPLIER_FOLLOW:
		replica_on_applier_sync(replica);
		break;
	case APPLIER_OFF:
		/*
		 * Connection to self, duplicate connection
		 * to the same master, or the applier fiber
		 * has been cancelled. Assume synced.
		 */
		replica_on_applier_sync(replica);
		break;
	case APPLIER_STOPPED:
		/* Unrecoverable error. */
		replica_on_applier_disconnect(replica);
		break;
	default:
		break;
	}
	fiber_cond_signal(&replicaset.applier.cond);
}

/**
 * Update the replica set with new "applier" objects
 * upon reconfiguration of box.cfg.replication.
 */
static void
replicaset_update(struct applier **appliers, int count)
{
	replica_hash_t uniq;
	memset(&uniq, 0, sizeof(uniq));
	replica_hash_new(&uniq);
	RLIST_HEAD(anon_replicas);
	struct replica *replica, *next;
	struct applier *applier;

	auto uniq_guard = make_scoped_guard([&]{
		replica_hash_foreach_safe(&uniq, replica, next) {
			replica_hash_remove(&uniq, replica);
			replica_delete(replica);
		}
	});

	/* Check for duplicate UUID */
	for (int i = 0; i < count; i++) {
		applier = appliers[i];
		replica = replica_new();
		replica_set_applier(replica, applier);

		if (applier->state != APPLIER_CONNECTED) {
			/*
			 * The replica has not received its UUID from
			 * the master yet and thus cannot be added to
			 * the replica set. Instead, add it to the list
			 * of anonymous replicas and setup a trigger
			 * that will insert it into the replica set
			 * when it is finally connected.
			 */
			rlist_add_entry(&anon_replicas, replica, in_anon);
			continue;
		}

		assert(!tt_uuid_is_nil(&applier->uuid));
		replica->uuid = applier->uuid;

		if (replica_hash_search(&uniq, replica) != NULL) {
			tnt_raise(ClientError, ER_CFG, "replication",
				  "duplicate connection to the same replica");
		}
		replica_hash_insert(&uniq, replica);
	}

	/*
	 * All invariants and conditions are checked, now it is safe to
	 * apply the new configuration. Nothing can fail after this point.
	 */

	/* Prune old appliers */
	replicaset_foreach(replica) {
		if (replica->applier == NULL)
			continue;
		applier = replica->applier;
		replica_clear_applier(replica);
		replica->state = REPLICA_DISCONNECTED;
		applier_stop(applier);
		applier_delete(applier);
	}
	rlist_foreach_entry_safe(replica, &replicaset.anon, in_anon, next) {
		applier = replica->applier;
		replica_clear_applier(replica);
		replica_delete(replica);
		applier_stop(applier);
		applier_delete(applier);
	}
	rlist_create(&replicaset.anon);

	/* Save new appliers */
	replicaset.applier.total = count;
	replicaset.applier.connected = 0;
	replicaset.applier.synced = 0;

	replica_hash_foreach_safe(&uniq, replica, next) {
		replica_hash_remove(&uniq, replica);

		struct replica *orig = replica_hash_search(&replicaset.hash,
							   replica);
		if (orig != NULL) {
			/* Use existing struct replica */
			replica_set_applier(orig, replica->applier);
			replica_clear_applier(replica);
			replica_delete(replica);
			replica = orig;
		} else {
			/* Add a new struct replica */
			replica_hash_insert(&replicaset.hash, replica);
		}

		replica->state = REPLICA_CONNECTED;
		replicaset.applier.connected++;
	}
	rlist_swap(&replicaset.anon, &anon_replicas);

	assert(replica_hash_first(&uniq) == NULL);
	replica_hash_foreach_safe(&replicaset.hash, replica, next) {
		if (replica_is_orphan(replica)) {
			replica_hash_remove(&replicaset.hash, replica);
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
replicaset_connect(struct applier **appliers, int count,
		   double timeout, bool connect_all)
{
	if (count == 0) {
		/* Cleanup the replica set. */
		replicaset_update(appliers, count);
		return;
	}

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

	while (state.connected < count) {
		double wait_start = ev_monotonic_now(loop());
		if (fiber_cond_wait_timeout(&state.wakeup, timeout) != 0)
			break;
		if (state.failed > 0 && connect_all)
			break;
		timeout -= ev_monotonic_now(loop()) - wait_start;
	}
	if (state.connected < count) {
		/* Timeout or connection failure. */
		if (connect_all)
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
	if (replicaset.applier.total == 0) {
		/*
		 * Replication is not configured.
		 */
		box_clear_orphan();
		return;
	}
	struct replica *replica;
	replicaset_foreach(replica) {
		/* Resume connected appliers. */
		if (replica->applier != NULL)
			applier_resume(replica->applier);
	}
	rlist_foreach_entry(replica, &replicaset.anon, in_anon) {
		/* Restart appliers that failed to connect. */
		applier_start(replica->applier);
	}
	if (replicaset_quorum() == 0) {
		/*
		 * Leaving orphan mode immediately since
		 * replication_connect_quorum is set to 0.
		 */
		box_clear_orphan();
	}
}

void
replicaset_sync(void)
{
	int quorum = replicaset_quorum();

	if (replicaset.applier.connected < quorum) {
		/*
		 * Not enough replicas connected to form a quorum.
		 * Do not stall configuration, leave the instance
		 * in 'orphan' state.
		 */
		return;
	}

	/*
	 * Wait until a quorum is formed. Abort waiting if
	 * a quorum cannot be formed because of errors.
	 */
	while (replicaset.applier.synced < quorum &&
	       replicaset.applier.connected >= quorum)
		fiber_cond_wait(&replicaset.applier.cond);
}

void
replicaset_check_quorum(void)
{
	if (replicaset.applier.synced >= replicaset_quorum())
		box_clear_orphan();
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
		replica_hash_remove(&replicaset.hash, replica);
		replica_delete(replica);
	}
}

struct replica *
replicaset_first(void)
{
	return replica_hash_first(&replicaset.hash);
}

struct replica *
replicaset_next(struct replica *replica)
{
	return replica_hash_next(&replicaset.hash, replica);
}

struct replica *
replicaset_leader(void)
{
	struct replica *leader = NULL;
	replicaset_foreach(replica) {
		if (replica->applier == NULL)
			continue;
		/**
		 * While bootstrapping a new cluster,
		 * read-only replicas shouldn't be considered
		 * as a leader.
		 */
		if (replica->applier->remote_is_ro &&
		    replica->applier->vclock.signature == 0)
			continue;
		if (leader == NULL) {
			leader = replica;
			continue;
		}
		/*
		 * Choose the replica with the most advanced
		 * vclock. If there are two or more replicas
		 * with the same vclock, prefer the one with
		 * the lowest uuid.
		 */
		int cmp = vclock_compare(&replica->applier->vclock,
					 &leader->applier->vclock);
		if (cmp < 0)
			continue;
		if (cmp == 0 && tt_uuid_compare(&replica->uuid,
						&leader->uuid) > 0)
			continue;
		leader = replica;
	}
	return leader;
}

struct replica *
replica_by_uuid(const struct tt_uuid *uuid)
{
	struct replica key;
	key.uuid = *uuid;
	return replica_hash_search(&replicaset.hash, &key);
}
