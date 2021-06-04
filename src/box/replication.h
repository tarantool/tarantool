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
#include "uuid/tt_uuid.h"
#include "trigger.h"
#include <stdint.h>
#define RB_COMPACT 1
#include <small/rb.h> /* replicaset_t */
#include <small/rlist.h>
#include "applier.h"
#include "fiber_cond.h"
#include "vclock/vclock.h"
#include "latch.h"

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
 * Minimal number of replicas which should ACK a synchronous
 * transaction to be able to confirm it and commit.
 */
extern int replication_synchro_quorum;

/**
 * Time in seconds which the master node is able to wait for ACKs
 * for a synchronous transaction until it is rolled back.
 */
extern double replication_synchro_timeout;

/**
 * Max time to wait for appliers to synchronize before entering
 * the orphan mode.
 */
extern double replication_sync_timeout;

/*
 * Allows automatic skip of conflicting rows in replication (e.g. applying
 * the row throws ER_TUPLE_FOUND) based on box.cfg configuration option.
 */
extern bool replication_skip_conflict;

/**
 * Whether this replica will be anonymous or not, e.g. be preset
 * in _cluster table and have a non-zero id.
 */
extern bool replication_anon;

/**
 * Wait for the given period of time before trying to reconnect
 * to a master.
 */
static inline double
replication_reconnect_interval(void)
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

/** Ack which is passed to on ack triggers. */
struct replication_ack {
	/** Replica ID of the ACK source. */
	uint32_t source;
	/** Confirmed vclock. */
	const struct vclock *vclock;
};

/**
 * Replica set state.
 *
 * A replica set is a set of appliers and their matching
 * relays, usually connected in full mesh.
 */
struct replicaset {
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
	/**
	 * This flag is set while the instance is bootstrapping
	 * from a remote master.
	 */
	bool is_joining;
	/* A number of anonymous replicas following this instance. */
	int anon_count;
	/**
	 * Number of registered replicas. That includes all of them - connected,
	 * disconnected, connected not directly, just present in _cluster. If an
	 * instance has an ID, has the same replicaset UUID, then it is
	 * accounted here.
	 */
	int registered_count;
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
		 * Number of appliers that are disconnected,
		 * because replica is loading.
		 */
		int loading;
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
		/*
		 * The latch is used to order replication requests
		 * running on behalf of all dead replicas
		 * (replicas which have a server id but don't have
		 * struct replica object).
		 */
		struct latch order_latch;
		/*
		 * A vclock of the last transaction wich was read
		 * by applier and processed by tx.
		 */
		struct vclock vclock;
		/* Trigger to fire when a replication request failed to apply. */
		struct rlist on_rollback;
		/* Trigget to fire a replication request when WAL write happens. */
		struct rlist on_wal_write;
		/* Shared applier diagnostic area. */
		struct diag diag;
	} applier;
	/** Triggers are invoked on each ACK from each replica. */
	struct rlist on_ack;
	/** Map of all known replica_id's to correspponding replica's. */
	struct replica *replica_by_id[VCLOCK_MAX];
};
extern struct replicaset replicaset;

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
	/**
	 * Whether this is an anonymous replica, e.g. a read-only
	 * replica that doesn't have an id and isn't present in
	 * _cluster table.
	 */
	bool anon;
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
	/**
	 * During initial connect or reconnect we require applier
	 * to sync with the master before the replica can leave
	 * read-only mode. This enum reflects the state of the
	 * state machine for applier sync. Technically it is a
	 * subset of the applier state machine, but since it's
	 * much simpler and is used for a different purpose
	 * (achieving replication connect quorum), we keep it
	 * separate from applier.
	 */
	enum applier_state applier_sync_state;
	/**
	 * Applier's last written to WAL transaction timestamp.
	 * Needed for relay lagging statistics.
	 */
	double applier_txn_last_tm;
	/* The latch is used to order replication requests. */
	struct latch order_latch;
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
 * Find a replica by ID
 */
struct replica *
replica_by_id(uint32_t replica_id);

/**
 * Find a node in the replicaset on which the instance can try to register to
 * join the replicaset.
 */
struct replica *
replicaset_find_join_master(void);

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

void
replica_clear_applier(struct replica *replica);

void
replica_set_applier(struct replica * replica, struct applier * applier);

/**
 * Unregister \a relay from the \a replica.
 */
void
replica_on_relay_stop(struct replica *replica);

#if defined(__cplusplus)
} /* extern "C" */

int
replica_check_id(uint32_t replica_id);

/**
 * Register the universally unique identifier of a remote replica and
 * a matching replica-set-local identifier in the  _cluster registry.
 * Called from on_replace_dd_cluster() when a remote master joins the
 * replica set.
 */
struct replica *
replicaset_add(uint32_t replica_id, const struct tt_uuid *instance_uuid);

struct replica *
replicaset_add_anon(const struct tt_uuid *replica_uuid);

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
 * \param connect_quorum if this flag is set, fail unless at
 *                       least replication_connect_quorum
 *                       appliers have successfully connected.
 */
void
replicaset_connect(struct applier **appliers, int count,
		   bool connect_quorum);

/**
 * Check if the current instance fell too much behind its
 * peers in the replica set and needs to be rebootstrapped.
 * If it does, return true and set @master to the instance
 * to use for rebootstrap, otherwise return false.
 */
bool
replicaset_needs_rejoin(struct replica **master);

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
