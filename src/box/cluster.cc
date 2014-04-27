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
#include "cluster.h"
#include "recovery.h"
#include "exception.h"

void
cluster_set_id(const tt_uuid *uu)
{
	/* Set cluster UUID. */
	assert(tt_uuid_is_nil(&recovery_state->cluster_uuid));
	recovery_state->cluster_uuid = *uu;
}

void
cluster_add_node(const tt_uuid *node_uuid, cnode_id_t node_id)
{
	struct recovery_state *r = recovery_state;

	assert(!tt_uuid_is_nil(node_uuid));
	assert(!cnode_id_is_reserved(node_id));

	/* Add node */
	struct node *node = (struct node *) calloc(1, sizeof(*node));
	if (node == NULL) {
		tnt_raise(ClientError, ER_MEMORY_ISSUE, sizeof(*node),
			  "recovery", "r->cluster");
	}
	node->id = node_id;
	node->uuid = *node_uuid;
	uint32_t k = mh_cluster_put(recovery_state->cluster,
		(const struct node **) &node, NULL, NULL);
	if (k == mh_end(recovery_state->cluster)) {
		free(node);
		tnt_raise(ClientError, ER_MEMORY_ISSUE, sizeof(*node),
			  "recovery", "r->cluster");
	}

	say_debug("confirm node: {uuid = %s, id = %u}",
		  tt_uuid_str(node_uuid), node_id);

	/* Confirm Local node */
	if (tt_uuid_cmp(&r->node_uuid, node_uuid) == 0) {
		/* Confirm Local Node */
		say_info("synchronized with cluster");
		assert(r->local_node == NULL || r->local_node->id == 0);
		r->local_node = node;
	}
}
