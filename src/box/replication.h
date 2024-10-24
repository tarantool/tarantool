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
#include "trigger.h"
#include <stdint.h>
#define RB_COMPACT 1
#include <small/rb.h> /* replicaset_t */
#include <small/rlist.h>
#include "applier.h"
#include "fiber_cond.h"
#include "node_name.h"
#include "tt_uuid.h"
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
 * If we haven't heard from an anonymous replica during this time, its WAL GC
 * state will be deleted.
 */
extern double replication_anon_gc_timeout;
/**
 * Fiber that deletes WAL GC state of replicas that haven't been in touch
 * for too long.
 */
extern struct fiber *replication_anon_gc_expiration_fiber;

enum { REPLICATION_THREADS_MAX = 1000 };

enum bootstrap_strategy {
	BOOTSTRAP_STRATEGY_INVALID = -1,
	BOOTSTRAP_STRATEGY_AUTO,
	BOOTSTRAP_STRATEGY_LEGACY,
	BOOTSTRAP_STRATEGY_CONFIG,
	BOOTSTRAP_STRATEGY_SUPERVISED,
};

/** Instance's bootstrap strategy. Controls replication reconfiguration. */
extern enum bootstrap_strategy bootstrap_strategy;

enum replicaset_state {
	REPLICASET_BOOTSTRAP,
	REPLICASET_JOIN,
	REPLICASET_RECOVERY,
	REPLICASET_READY,
};

/**
 * Whether this replica should be anonymous or not, e.g. be present in _cluster
 * table and have a non-zero id.
 */
extern bool cfg_replication_anon;

/**
 * The uuid of the bootstrap leader configured via the bootstrap_leader
 * configuration option.
 */
extern struct tt_uuid cfg_bootstrap_leader_uuid;

/**
 * The uri of the bootstrap leader configured via the bootstrap_leader
 * configuration option.
 */
extern struct uri cfg_bootstrap_leader_uri;

/**
 * The name of the bootstrap leader configured via the bootstrap_leader
 * configuration option.
 */
extern char cfg_bootstrap_leader_name[];

/**
 * Configured name of this instance. Might be different from the actual name if
 * the configuration is not fully applied yet.
 */
extern char cfg_instance_name[];

/** Replica set state. */
extern enum replicaset_state replicaset_state;

/**
 * Remote peer URIs. Set by box.cfg.replication_uris.
 */
extern struct uri_set replication_uris;

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
 * Part of internal.tweaks.replication_synchro_timeout_rollback_enabled.
 * Indicates whether the replication_synchro_timeout option rolls back
 * transactions or it only used to wait confirmation in box_(promote/demote)
 * and gc_do_checkpoint.
 */
extern bool replication_synchro_timeout_rollback_enabled;

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

/** How many threads to use for decoding incoming replication stream. */
extern int replication_threads;

/**
 * A list of triggers fired once quorum of "healthy" connections is acquired.
 */
extern struct rlist replicaset_on_quorum_gain;

/**
 * A list of triggers fired once the quorum of "healthy" connections is lost.
 */
extern struct rlist replicaset_on_quorum_loss;

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
 * This timeout might be different for Raft leader. Used in strict fencing,
 * to provide best effort to achieve leader uniqueness.
 */
double
replication_disconnect_timeout(void);

void
replication_init(int num_threads);

/**
 * Prepare for freeing resources in replication_free while TX event loop is
 * still running.
 */
void
replication_shutdown(void);

void
replication_free(void);

/** Instance id vclock identifier. */
extern uint32_t instance_id;
/** UUID of the instance. */
extern struct tt_uuid INSTANCE_UUID;
/** Name of the instance. */
extern char INSTANCE_NAME[];

/** UUID of the replicaset. */
extern struct tt_uuid REPLICASET_UUID;
/** Name of the replicaset. */
extern char REPLICASET_NAME[];
/** Name of the entire cluster with all its replicasets. */
extern char CLUSTER_NAME[];

typedef rb_tree(struct replica) replica_hash_t;

/** Ack which is passed to on ack triggers. */
struct replication_ack {
	/** Replica ID of the ACK source. */
	uint32_t source;
	/** Confirmed vclock. */
	const struct vclock *vclock;
	/** Vclock sync received with ACK. */
	uint64_t vclock_sync;
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
	/* A number of anonymous replicas following this instance. */
	int anon_count;
	/**
	 * Number of registered replicas. That includes all of them - connected,
	 * disconnected, connected not directly, just present in _cluster. If an
	 * instance has an ID, has the same replicaset UUID, then it is
	 * accounted here.
	 */
	int registered_count;
	/**
	 * Number of registered replicas, to which this node has a bidirectional
	 * connection, such that both relay and applier are in FOLLOW state.
	 * Used to notify various subsystems whether there is a quorum of
	 * followers connected.
	 */
	int healthy_count;
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
	/** Triggers invoked once relay thread becomes operational. */
	struct rlist on_relay_thread_start;
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
	/** Instance name. */
	char name[NODE_NAME_SIZE_MAX];
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
	/** Whether there is an established relay to this replica. */
	bool is_relay_healthy;
	/** Whether there is an applier subscribed to this replica. */
	bool is_applier_healthy;
	/** Applier fiber. */
	struct applier *applier;
	/** Relay thread. */
	struct relay *relay;
	/** Garbage collection state associated with the replica. */
	struct gc_consumer *gc;
	/** Reference to retained checkpoint, is set only on checkpoint join. */
	struct gc_checkpoint_ref *gc_checkpoint_ref;
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

/** Find a replica by its name. */
struct replica *
replica_by_name(const char *name);

/**
 * Find a replica by ID
 */
struct replica *
replica_by_id(uint32_t replica_id);

/** Find the smallest empty replica ID in the available range. */
int
replica_find_new_id(uint32_t *replica_id);

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

/** Give the replica a new name. */
void
replica_set_name(struct replica *replica, const char *name);

/*
 * Clear the numeric replica-set-local id of a replica.
 *
 * The replica is removed from the replication vector clock.
 */
void
replica_clear_id(struct replica *replica);

/**
 * See if the replica still has active connections or might be trying to make
 * new ones.
 */
bool
replica_has_connections(const struct replica *replica);

/**
 * Remove associated WAL GC state including persistent one. Replica's relay
 * mustn't be connected. Note that the replica can be deleted if it becomes
 * orphan.
 */
int
replica_clear_gc(struct replica *replica);

/**
 * Check if there are enough "healthy" connections, and fire the appropriate
 * triggers. A replica connection is considered "healthy", when:
 * - it is a connection to a registered replica.
 * - it is bidirectional, e.g. there are both relay and applier for this
 *   replica.
 * - both relay and applier are in "FOLLOW" state, the normal state of operation
 *   during SUBSCRIBE.
 */
void
replicaset_on_health_change(void);

/** Return whether there are enough "healthy" connections to form a quorum. */
bool
replicaset_has_healthy_quorum(void);

/**
 * A special wrapper for replication_synchro_quorum, which lowers it to the
 * count of nodes registered in cluster.
 * The resulting value may differ from the configured synchro_quorum only
 * during bootstrap, when specified value is too high to operate.
 * Note, this value should never be used to commit synchronous transactions.
 * It's only used for leader elections and some extensions like pre-vote and
 * fencing.
 * See the comment in function body for more details.
 */
static inline int
replicaset_healthy_quorum(void)
{
	/*
	 * When the instance is started first time, it does not have an ID, so
	 * the registered count is 0. But the quorum can never be 0. At least
	 * the current instance should participate in the quorum.
	 */
	int max = MAX(replicaset.registered_count, 1);
	/*
	 * Election quorum is not strictly equal to synchronous replication
	 * quorum. Sometimes it can be lowered. That is about bootstrap.
	 *
	 * The problem with bootstrap is that when the replicaset boots, all the
	 * instances can't write to WAL and can't recover from their initial
	 * snapshot. They need one node which will boot first, and then they
	 * will replicate from it.
	 *
	 * This one node should boot from its zero snapshot, create replicaset
	 * UUID, register self with ID 1 in _cluster space, and then register
	 * all the other instances here. To do that the node must be writable.
	 * It should have read_only = false, connection quorum satisfied, and be
	 * a Raft leader if Raft is enabled.
	 *
	 * To be elected a Raft leader it needs to perform election. But it
	 * can't be done before at least synchronous quorum of the replicas is
	 * bootstrapped. And they can't be bootstrapped because wait for a
	 * leader to initialize _cluster. Cyclic dependency.
	 *
	 * This is resolved by truncation of the election quorum to the number
	 * of registered replicas, if their count is less than synchronous
	 * quorum. That helps to elect a first leader.
	 *
	 * It may seem that the first node could just declare itself a leader
	 * and then strictly follow the protocol from now on, but that won't
	 * work, because if the first node will restart after it is booted, but
	 * before quorum of replicas is booted, the cluster will stuck again.
	 *
	 * The current solution is totally safe because
	 *
	 * - after all the cluster will have node count >= quorum, if user used
	 *   a correct config (God help him if he didn't);
	 *
	 * - synchronous replication quorum is untouched - it is not truncated.
	 *   Only leader election quorum is affected. So synchronous data won't
	 *   be lost.
	 */
	return MIN(max, replication_synchro_quorum);
}

/** Track an established downstream connection in replica. */
void
replica_on_relay_follow(struct replica *replica);

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
 * \param uris           remote peer URIs
 * \param timeout        connection timeout
 * \param connect_quorum if this flag is set, fail unless at
 *                       least replication_connect_quorum
 *                       appliers have successfully connected.
 * \param keep_connect   if this flag is set do not force a reconnect if the
 *                       old connection to the replica is fine.
 */
void
replicaset_connect(const struct uri_set *uris,
		   bool connect_quorum, bool keep_connect);

/**
 * Reload replica URIs.
 *
 * Called on reconfiguration in case the remote peer URIs are the same.
 * A URI parameter may store a path to a file (for example, an SSL
 * certificate), which could change, so we need to recreate appliers'
 * IO stream contexts in this case.
 *
 * Throws an exception on failure.
 */
void
replicaset_reload_uris(void);

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
