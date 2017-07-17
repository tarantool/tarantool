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

void
replication_init(void)
{
	mempool_create(&replica_pool, &cord()->slabc,
		       sizeof(struct replica));
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
replica_new(const struct tt_uuid *uuid)
{
	struct replica *replica = (struct replica *) mempool_alloc(&replica_pool);
	if (replica == NULL)
		tnt_raise(OutOfMemory, sizeof(*replica), "malloc",
			  "struct replica");
	replica->id = 0;
	replica->uuid = *uuid;
	replica->applier = NULL;
	replica->relay = NULL;
	replica->gc = NULL;
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
	struct replica *replica = replica_new(replica_uuid);
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

void
replicaset_update(struct applier **appliers, int count)
{
	replicaset_t uniq;
	replicaset_new(&uniq);
	struct replica *replica, *next;

	auto uniq_guard = make_scoped_guard([&]{
		replicaset_foreach_safe(&uniq, replica, next) {
			replicaset_remove(&uniq, replica);
			replica_delete(replica);
		}
	});

	/* Check for duplicate UUID */
	for (int i = 0; i < count; i++) {
		replica = replica_new(&appliers[i]->uuid);
		if (replicaset_search(&uniq, replica) != NULL) {
			tnt_raise(ClientError, ER_CFG, "replication",
				  "duplicate connection to the same replica");
		}

		replica->applier = appliers[i];
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

	assert(replicaset_first(&uniq) == NULL);
	replicaset_foreach_safe(&replicaset, replica, next) {
		if (replica_is_orphan(replica)) {
			replicaset_remove(&replicaset, replica);
			replica_delete(replica);
		}
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
