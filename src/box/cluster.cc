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
#include "box.h"
#include "cluster.h"
#include "recovery.h"
#include "applier.h"

/**
 * Globally unique identifier of this cluster.
 * A cluster is a set of connected appliers.
 */
tt_uuid cluster_id;

typedef rb_tree(struct applier) applierset_t;
rb_proto(, applierset_, applierset_t, struct applier)

static int
applier_compare_by_uuid(const struct applier *a, const struct applier *b)
{
	return memcmp(&a->uuid, &b->uuid, sizeof(a->uuid));
}

rb_gen(, applierset_, applierset_t, struct applier, link,
       applier_compare_by_uuid);

static applierset_t applierset;

void
cluster_init(void)
{
	applierset_new(&applierset);
}

void
cluster_free(void)
{

}

extern "C" struct vclock *
cluster_clock()
{
        return &recovery->vclock;
}

void
cluster_add_server(uint32_t server_id, const struct tt_uuid *server_uuid)
{
	struct recovery *r = ::recovery;
	/** Checked in the before-commit trigger */
	assert(!tt_uuid_is_nil(server_uuid));
	assert(!cserver_id_is_reserved(server_id) && server_id < VCLOCK_MAX);
	assert(!vclock_has(&r->vclock, server_id));

	/* Add server */
	vclock_add_server_nothrow(&r->vclock, server_id);
	if (tt_uuid_is_equal(&r->server_uuid, server_uuid)) {
		/* Assign local server id */
		assert(r->server_id == 0);
		r->server_id = server_id;
		/*
		 * Leave read-only mode
		 * if this is a running server.
		 * Otherwise, read only is switched
		 * off after recovery_finalize().
		 */
		if (r->writer)
			box_set_ro(false);
	}
}

void
cluster_del_server(uint32_t server_id)
{
	struct recovery *r = ::recovery;
	assert(!cserver_id_is_reserved(server_id) && server_id < VCLOCK_MAX);

	vclock_del_server(&r->vclock, server_id);
	if (r->server_id == server_id) {
		r->server_id = 0;
		box_set_ro(true);
	}
}

int
cluster_set_appliers(struct applier **appliers, int count)
{
	applierset_t uniq;
	applierset_new(&uniq);

	/* Check for duplicate UUID */
	for (int i = 0; i < count; i++) {
		if (applierset_search(&uniq, appliers[i]) != NULL) {
			tnt_error(ClientError, ER_CFG, "replication_source",
				  "duplicate connection to the same server");
			/* Caveat: applier->link is inconsistent */
			return -1;
		}

		applierset_insert(&uniq, appliers[i]);
	}
	/* Ignore `uniq' set after the checking UUID for uniqueness */

	/*
	 * All invariants and conditions are checked, now it is safe to
	 * apply the new configuration. Nothing can fail after this point.
	 */

	/* Prube old appliers */
	struct applier *applier = applierset_first(&applierset);
	while (applier != NULL) {
		struct applier *next = applierset_next(&applierset, applier);
		applier_stop(applier); /* cancels a background fiber */
		applierset_remove(&applierset, applier);
		applier_delete(applier);
		applier = next;
	}

	/* Save new appliers */
	for (int i = 0; i < count; i++)
		applierset_insert(&applierset, appliers[i]);

	return 0;
}

struct applier *
cluster_applier_first(void)
{
	return applierset_first(&applierset);
}

struct applier *
cluster_applier_next(struct applier *applier)
{
	return applierset_next(&applierset, applier);
}
