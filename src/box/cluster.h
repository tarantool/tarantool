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

void
cluster_init(void);

void
cluster_free(void);

/** {{{ Global cluster identifier API **/

/** UUID of the cluster. */
extern tt_uuid cluster_id;

extern "C" struct vclock *
cluster_clock();

/* }}} */

/** {{{ Cluster server id API **/

static inline bool
cserver_id_is_reserved(uint32_t id)
{
        return id == 0;
}

/**
 * Register the universally unique identifier of a remote server and
 * a matching cluster-local identifier in the  cluster registry.
 * Called when a remote master joins the cluster.
 *
 * The server is added to the cluster lsn table with LSN 0.
 */
void
cluster_add_server(uint32_t server_id, const struct tt_uuid *server_uuid);

/*
 * Update UUID of a remote server
 */
void
cluster_update_server(uint32_t server_id, const struct tt_uuid *server_uuid);

void
cluster_del_server(uint32_t server_id);

/** }}} **/

/** {{{ Cluster applier API **/

void
cluster_add_applier(struct applier *applier);

void
cluster_del_applier(struct applier *applier);

struct applier *
cluster_find_applier(const char *source);

struct applier *
cluster_applier_first(void);

struct applier *
cluster_applier_next(struct applier *applier);

#define cluster_foreach_applier(var) \
	for (struct applier *var = cluster_applier_first(); \
	     var != NULL; var = cluster_applier_next(var))
/** }}} **/

#endif
