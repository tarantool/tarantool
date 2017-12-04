#ifndef INCLUDES_BOX_REPLICATION_H
#define INCLUDES_BOX_REPLICATION_H
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
#include "tt_uuid.h"
#include <stdint.h>
#define RB_COMPACT 1
#include <small/rb.h> /* replicaset_t */

/**
 * @module replication - global state of multi-master
 * replicated database.
 *
 * Right now we only support asynchronous master-master
 * replication.
 *
 * Each replica set has a globally unique identifier. Each
 * replica in the replica set is identified as well.
 * A replica which is part of one replica set can not join
 * another replica set.
 *
 * Replica set and instance identifiers are stored in a system
 * space _cluster on all replicas. The instance identifier
 * is also stored in each snapshot header, this is how
 * the instance knows which instance id in the _cluster space
 * is its own id.
 *
 * Replica set and instance identifiers are globally unique
 * (UUID, universally unique identifiers). In addition
 * to these unique but long identifiers, a short integer id
 * is used for pervasive replica identification in a replication
 * stream, a snapshot, or internal data structures.
 * The mapping between 16-byte globally unique id and
 * 4 byte replica set local id is stored in _cluster space. When
 * a replica joins the replica set, it sends its globally unique
 * identifier to one of the masters, and gets its replica set
 * local identifier as part of the reply to the JOIN request
 * (in fact, it gets it as a REPLACE request in _cluster
 * system space along with the rest of the replication
 * stream).
 *
 * Replica set state on each replica is represented by a
 * table like below:
 *
 *   ----------------------------------
 *  | replica id       | confirmed lsn |
 *   ----------------------------------
 *  | 1                |  1258         | <-- changes of the first replica
 *   ----------------------------------
 *  | 2                |  1292         | <-- changes of the local instance
 *   ----------------------------------
 *
 * This table is called in the code "vector clock".
 * and is implemented in @file vclock.h
 */

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct gc_consumer;

void
replication_init(void);

void
replication_free(void);

/** Instance id vclock identifier */
extern uint32_t instance_id;
/**
 * tx-thread local vclock reflecting the
 * state of the cluster, as maintained by the appliers.
 */
extern struct vclock replicaset_vclock;

/** UUID of the instance. */
extern struct tt_uuid INSTANCE_UUID;
/** UUID of the replica set. */
extern struct tt_uuid REPLICASET_UUID;

/**
 * Summary information about a replica in the replica set.
 */
struct replica {
	rb_node(struct replica) link;
	struct tt_uuid uuid;
	struct applier *applier;
	struct relay *relay;
	struct gc_consumer *gc;
	uint32_t id;
};

enum {
	/**
	 * Reserved id used for local requests, checkpoint rows
	 * and in cases where id is unknown.
	 */
	REPLICA_ID_NIL = 0,
};

/**
 * Find a replica by UUID
 */
struct replica *
replica_by_uuid(const struct tt_uuid *uuid);

struct replica *
replicaset_first(void);

struct replica *
replicaset_next(struct replica *replica);

#define replicaset_foreach(var) \
	for (struct replica *var = replicaset_first(); \
	     var != NULL; var = replicaset_next(var))

/**
 * Set numeric replica-set-local id of remote replica.
 * table. Add replica to the replica set vclock with LSN = 0.
 */
void
replica_set_id(struct replica *replica, uint32_t id);

/*
 * Clear the numeric replica-set-local id of a replica.
 *
 * The replica is removed from the replication vector clock.
 */
void
replica_clear_id(struct replica *replica);

/**
 * Register \a relay of a \a replica.
 * \pre a replica can have only one relay
 * \pre replica->id != REPLICA_ID_NIL
 */
void
replica_set_relay(struct replica *replica, struct relay *relay);

/**
 * Unregister \a relay from the \a replica.
 */
void
replica_clear_relay(struct replica *replica);

#if defined(__cplusplus)
} /* extern "C" */

void
replica_check_id(uint32_t replica_id);

/**
 * Register the universally unique identifier of a remote replica and
 * a matching replica-set-local identifier in the  _cluster registry.
 * Called from on_replace_dd_cluster() when a remote master joins the
 * replica set.
 */
struct replica *
replicaset_add(uint32_t replica_id, const struct tt_uuid *instance_uuid);

/**
 * Connect all appliers to remote peers and receive UUID.
 * \post appliers are connected to remote hosts and paused.
 * Use applier_resume(applier) to resume applier.
 *
 * \param appliers the array of appliers
 * \param count size of appliers array
 * \param timeout connection timeout
 */
void
replicaset_connect(struct applier **appliers, int count, double timeout);

/**
 * Update a replica set with new "applier" objects
 * upon reconfiguration of box.cfg.replication.
 */
void
replicaset_update(struct applier **appliers, int count);

#endif /* defined(__cplusplus) */

#endif
