#ifndef INCLUDES_BOX_CLUSTER_H
#define INCLUDES_BOX_CLUSTER_H
/*
 * Copyright 2010-2015, Tarantool AUTHORS, please see AUTHORS file.
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
#include <third_party/rb.h> /* serverset_t */

/**
 * @module cluster - global state of multi-master
 * replicated database.
 *
 * Right now the cluster can only consist of instances
 * connected with asynchronous master-master replication.
 *
 * Each cluster has a globally unique identifier. Each
 * server in the cluster is identified as well. A server
 * which is part of one cluster can not join another
 * cluster.
 *
 * Cluster and server identifiers are stored in a system
 * space _cluster on all servers. The server identifier
 * is also stored in each snapshot header, this is how
 * the server knows which server id in the _cluster space is
 * its own id.
 *
 * Cluster and server identifiers are globally unique
 * (UUID, universally unique identifiers). In addition
 * to these unique but long identifiers, a short integer id
 * is used for pervasive server identification in a replication
 * stream, a snapshot, or internal data structures.
 * The mapping between 16-byte globally unique id and
 * 4 byte cluster local id is stored in _cluster space. When
 * a server joins the cluster, it sends its globally unique
 * identifier to one of the masters, and gets its cluster
 * local identifier as part of the reply to the JOIN request
 * (in fact, it gets it as a REPLACE request in _cluster
 * system space along with the rest of the replication
 * stream).
 *
 * Cluster state on each server is represented by a table
 * like below:
 *
 *   ----------------------------------
 *  | server id        | confirmed lsn |
 *   ----------------------------------
 *  | 1                |  1258         | <-- changes of the first server
 *   ----------------------------------
 *  | 2                |  1292         | <-- changes of the local server
 *   ----------------------------------
 *
 * This table is called in the code "cluster vector clock".
 * and is implemented in @file vclock.h
 */

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

void
cluster_init(void);

void
cluster_free(void);

/** {{{ Global cluster identifier API **/

/** UUID of the cluster. */
extern struct tt_uuid cluster_id;

struct vclock *
cluster_clock();

/* }}} */

/** {{{ Cluster server API **/

/**
 * Summary information about server in the cluster.
 */
struct server {
	rb_node(struct server) link;
	struct tt_uuid uuid;
	struct applier *applier;
	struct relay *relay;
	uint32_t id;
};

enum {
	/**
	 * Reserved id used for local requests, snapshot rows and in cases
	 * where id is unknown.
	 */
	SERVER_ID_NIL = 0,
};

static inline bool
server_id_is_reserved(uint32_t id)
{
        return id == SERVER_ID_NIL;
}

/**
 * Find a server by UUID
 */
struct server *
server_by_uuid(const struct tt_uuid *uuid);

struct server *
server_first(void);

struct server *
server_next(struct server *server);

#define server_foreach(var) \
	for (struct server *var = server_first(); \
	     var != NULL; var = server_next(var))
/**
 * Set numeric cluster-local id of remote server.
 * table. Add server to the cluster lsn table with LSN = 0.
 */
void
server_set_id(struct server *server, uint32_t id);

/*
 * Clear numeric cluster-local id of remote server.
 *
 * The server is removed from the cluster lsn table.
 */
void
server_clear_id(struct server *server);

/**
 * Register \a relay within the \a server.
 * \pre the only one relay can be registered.
 * \pre server->id != SERVER_ID_NIL
 */
void
server_set_relay(struct server *server, struct relay *relay);

/**
 * Unregister \a relay from the \a server.
 */
void
server_clear_relay(struct server *server);

#if defined(__cplusplus)
} /* extern "C" */

/**
 * Register the universally unique identifier of a remote server and
 * a matching cluster-local identifier in the  cluster registry.
 * Called from on_replace_dd_cluster() when a remote master joins the cluster.
 */
struct server *
cluster_add_server(uint32_t server_id, const struct tt_uuid *server_uuid);

void
cluster_set_appliers(struct applier **appliers, int count);

/** }}} **/

#endif /* defined(__cplusplus) */

#endif
