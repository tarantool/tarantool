#ifndef INCLUDES_BOX_CLUSTER_H
#define INCLUDES_BOX_CLUSTER_H
/*
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
 * node in the cluster is identified as well. A node
 * which is part of one cluster can not join another
 * cluster.
 *
 * Cluster and node identifiers are stored in a system
 * space _cluster on all nodes. The node identifier
 * is also stored in each snapshot header, this is how
 * the node knows which node id in the cluster belongs
 * to it.
 *
 * Cluster and node identifiers are globally unique
 * (UUID, universally unique identifiers). In addition
 * to a long UUID, which is stored in _cluster system
 * space for each node, a short integer id is used for
 * pervasive node identification in a replication stream,
 * a snapshot, or internal data structures.
 * The mapping between 16-byte node globally unique id and
 * 4 byte cluster local id is stored in _cluster table. When
 * a node joins the cluster, it sends its globally unique
 * identifier to one of the masters, and gets its cluster
 * local identifier as part of the reply to the JOIN request
 * (in fact, it gets it as a REPLACE request in _cluster
 * system space along with the rest of the replication
 * stream).
 *
 * Cluster state on each node is represented by a table
 * like below:
 *
 *   ----------------------------------
 *  | node id          | confirmed lsn |
 *   ----------------------------------
 *  | 1                |  1258         | <-- changes of the local node
 *   ----------------------------------
 *  | 2                |  1292         | <-- changes received from
 *   ----------------------------------       a remote node
 */

/** Cluster-local node identifier. */
typedef uint32_t cnode_id_t;

static inline bool
cnode_id_is_reserved(cnode_id_t id)
{
	return id == 0;
}

/**
 * Bootstrap a new cluster consisting of this node by
 * assigning it a new globally unique cluster id. Used
 * during bootstrapping in an empty data directory when no
 * existing cluster for joining has been provided in the
 * database configuration.
 */
void
cluster_set_id(const tt_uuid *uu);

/**
 * Register the universally unique identifier of a remote node and
 * a matching cluster-local identifier in the  cluster registry.
 * Called when a remote master joins the cluster.
 *
 * The node is added to the cluster lsn table with LSN 0.
 */
void
cluster_add_node(const tt_uuid *node_uu, cnode_id_t id);

#endif
