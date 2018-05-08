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
#include "trigger.h"
#include <stdint.h>
#define RB_COMPACT 1
#include <small/rb.h> /* replicaset_t */
#include <small/rlist.h>
#include <small/mempool.h>
#include "fiber_cond.h"
#include "vclock.h"

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

#include <limits.h>

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct gc_consumer;

static const int REPLICATION_CONNECT_QUORUM_ALL = INT_MAX;

/**
 * Network timeout. Determines how often master and slave exchange
 * heartbeat messages. Set by box.cfg.replication_timeout.
 */
extern double replication_timeout;

/**
 * Maximal time box.cfg() may wait for connections to all configured
 * replicas to be established. If box.cfg() fails to connect to all
 * replicas within the timeout, it will either leave the instance in
 * the orphan mode (recovery) or fail (bootstrap, reconfiguration).
 */
extern double replication_connect_timeout;

/**
 * Minimal number of replicas to sync for this instance to switch
 * to the write mode. If set to REPLICATION_CONNECT_QUORUM_ALL,
 * wait for all configured masters.
 */
extern int replication_connect_quorum;

/**
 * Switch applier from "sync" to "follow" as soon as the replication
 * lag is less than the value of the following variable.
 */
extern double replication_sync_lag;

/**
 * Allows automatic skip of conflicting rows in replication (e.g. applying
 * the row throws ER_TUPLE_FOUND) based on box.cfg configuration option.
 */
extern bool replication_skip_conflict;

/**
 * Wait for the given period of time before trying to reconnect
 * to a master.
 */
static inline double
replication_reconnect_timeout(void)
{
	return replication_timeout;
}

/**
 * Disconnect a replica if no heartbeat message has been
 * received from it within the given period.
 */
static inline double
replication_disconnect_timeout(void)
{
	return replication_timeout * 4;
}

void
replication_init(void);

void
replication_free(void);

/** Instance id vclock identifier. */
extern uint32_t instance_id;
/** UUID of the instance. */
extern struct tt_uuid INSTANCE_UUID;
/** UUID of the replica set. */
extern struct tt_uuid REPLICASET_UUID;

typedef rb_tree(struct replica) replica_hash_t;

/**
 * Replica set state.
 *
 * A replica set is a set of appliers and their matching
 * relays, usually connected in full mesh.
 */
struct replicaset {
	/** Memory pool for struct replica allocations. */
	struct mempool pool;
	/** Hash of replicas indexed by UUID. */
	replica_hash_t hash;
	/**
	 * List of replicas that haven't received a UUID.
	 * It contains both replicas that are still trying
	 * to connect and those that failed to connect.
	 */
	struct rlist anon;
	/**
	 * TX thread local vclock reflecting the state
	 * of the cluster as maintained by appliers.
	 */
	struct vclock vclock;
	/** Applier state. */
	struct {
		/**
		 * Total number of replicas with attached
		 * appliers.
		 */
		int total;
		/**
		 * Number of appliers that have successfully
		 * connected and received their UUIDs.
		 */
		int connected;
		/**
		 * Number of appliers that have successfully
		 * synchronized and hence contribute to the
		 * quorum.
		 */
		int synced;
		/**
		 * Signaled whenever an applier changes its
		 * state.
		 */
		struct fiber_cond cond;
	} applier;
};
extern struct replicaset replicaset;

enum replica_state {
	/**
	 * Applier has not connected to the master yet
	 * or has disconnected.
	 */
	REPLICA_DISCONNECTED,
	/**
	 * Applier has connected to the master and
	 * received UUID.
	 */
	REPLICA_CONNECTED,
	/**
	 * Applier has synchronized with the master
	 * (left "sync" and entered "follow" state).
	 */
	REPLICA_SYNCED,
};

/**
 * Summary information about a replica in the replica set.
 */
struct replica {
	/** Link in replicaset::hash. */
	rb_node(struct replica) in_hash;
	/**
	 * Replica UUID or nil if the replica or nil if the
	 * applier has not received from the master yet.
	 */
	struct tt_uuid uuid;
	/**
	 * Replica ID or nil if the replica has not been
	 * registered in the _cluster space yet.
	 */
	uint32_t id;
	/** Applier fiber. */
	struct applier *applier;
	/** Relay thread. */
	struct relay *relay;
	/** Garbage collection state associated with the replica. */
	struct gc_consumer *gc;
	/** Link in the anon_replicas list. */
	struct rlist in_anon;
	/**
	 * Trigger invoked when the applier changes its state.
	 */
	struct trigger on_applier_state;
	/** Replica sync state. */
	enum replica_state state;
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

/**
 * Return the replica set leader.
 */
struct replica *
replicaset_leader(void);

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
 * Try to connect appliers to remote peers and receive UUID.
 * Appliers that did not connect will connect asynchronously.
 * On success, update the replica set with new appliers.
 * \post appliers are connected to remote hosts and paused.
 * Use replicaset_follow() to resume appliers.
 *
 * \param appliers the array of appliers
 * \param count size of appliers array
 * \param timeout connection timeout
 * \param connect_all if this flag is set, fail unless all
 *                    appliers have successfully connected
 */
void
replicaset_connect(struct applier **appliers, int count,
		   double timeout, bool connect_all);

/**
 * Resume all appliers registered with the replica set.
 */
void
replicaset_follow(void);

/**
 * Wait until a replication quorum is formed.
 * Return immediately if a quorum cannot be
 * formed because of errors.
 */
void
replicaset_sync(void);

/**
 * Check if a replication quorum has been formed and
 * switch the server to the write mode if so.
 */
void
replicaset_check_quorum(void);

#endif /* defined(__cplusplus) */

#endif
