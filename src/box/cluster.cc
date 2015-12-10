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
#include <scoped_guard.h>
#include <fiber.h> /* &cord->slabc */
#include <small/mempool.h>

/**
 * Globally unique identifier of this cluster.
 * A cluster is a set of connected appliers.
 */
struct tt_uuid cluster_id;

typedef rb_tree(struct server) serverset_t;
rb_proto(, serverset_, serverset_t, struct server)

static int
server_compare_by_uuid(const struct server *a, const struct server *b)
{
	return memcmp(&a->uuid, &b->uuid, sizeof(a->uuid));
}

rb_gen(, serverset_, serverset_t, struct server, link,
       server_compare_by_uuid);

#define serverset_foreach_safe(set, item, next) \
	for (item = serverset_first(set); \
	     item != NULL && ((next = serverset_next(set, item)) || 1); \
	     item = next)

static struct mempool server_pool;
static serverset_t serverset;

void
cluster_init(void)
{
	mempool_create(&server_pool, &cord()->slabc,
		       sizeof(struct server));
	serverset_new(&serverset);
}

void
cluster_free(void)
{
	mempool_destroy(&server_pool);
}

extern "C" struct vclock *
cluster_clock()
{
        return &recovery->vclock;
}

/* Return true if server doesn't have id, relay and applier */
static bool
server_is_orphan(struct server *server)
{
	return server->id == SERVER_ID_NIL && server->applier == NULL &&
	       server->relay == NULL;
}

static struct server *
server_new(const struct tt_uuid *uuid)
{
	struct server *server = (struct server *) mempool_alloc(&server_pool);
	if (server == NULL)
		tnt_raise(OutOfMemory, sizeof(*server), "malloc",
			  "struct server");
	server->id = 0;
	server->uuid = *uuid;
	server->applier = NULL;
	server->relay = NULL;
	return server;
}

static void
server_delete(struct server *server)
{
	assert(server_is_orphan(server));
	mempool_free(&server_pool, server);
}

struct server *
cluster_add_server(uint32_t server_id, const struct tt_uuid *server_uuid)
{
	assert(!tt_uuid_is_nil(server_uuid));
	assert(!server_id_is_reserved(server_id) && server_id < VCLOCK_MAX);

	assert(server_by_uuid(server_uuid) == NULL);
	struct server *server = server_new(server_uuid);
	serverset_insert(&serverset, server);
	server_set_id(server, server_id);
	return server;
}

void
server_set_id(struct server *server, uint32_t server_id)
{
	assert(server->id == SERVER_ID_NIL); /* server id is read-only */
	server->id = server_id;

	/* Add server */
	struct recovery *r = ::recovery;
	assert(!vclock_has(&r->vclock, server_id));
	vclock_add_server_nothrow(&r->vclock, server_id);

	if (tt_uuid_is_equal(&r->server_uuid, &server->uuid)) {
		/* Assign local server id */
		assert(r->server_id == SERVER_ID_NIL);
		r->server_id = server_id;
		/*
		 * Leave read-only mode
		 * if this is a running server.
		 * Otherwise, read only is switched
		 * off after recovery_finalize().
		 */
		if (wal)
			box_set_ro(false);
	}
}

void
server_clear_id(struct server *server)
{
	assert(server->id != SERVER_ID_NIL);

	struct recovery *r = ::recovery;
	vclock_del_server(&r->vclock, server->id);
	if (r->server_id == server->id) {
		r->server_id = SERVER_ID_NIL;
		box_set_ro(true);
	}
	server->id = SERVER_ID_NIL;
	if (server_is_orphan(server)) {
		serverset_remove(&serverset, server);
		server_delete(server);
	}
}

void
cluster_set_appliers(struct applier **appliers, int count)
{
	serverset_t uniq;
	serverset_new(&uniq);
	struct server *server, *next;

	auto uniq_guard = make_scoped_guard([&]{
		serverset_foreach_safe(&uniq, server, next) {
			serverset_remove(&uniq, server);
			server_delete(server);
		}
	});

	/* Check for duplicate UUID */
	for (int i = 0; i < count; i++) {
		server = server_new(&appliers[i]->uuid);
		if (serverset_search(&uniq, server) != NULL) {
			tnt_raise(ClientError, ER_CFG, "replication_source",
				  "duplicate connection to the same server");
		}

		server->applier = appliers[i];
		serverset_insert(&uniq, server);
	}

	/*
	 * All invariants and conditions are checked, now it is safe to
	 * apply the new configuration. Nothing can fail after this point.
	 */

	/* Prune old appliers */
	server_foreach(server) {
		if (server->applier == NULL)
			continue;
		applier_stop(server->applier); /* cancels a background fiber */
		applier_delete(server->applier);
		server->applier = NULL;
	}

	/* Save new appliers */
	serverset_foreach_safe(&uniq, server, next) {
		serverset_remove(&uniq, server);

		struct server *orig = serverset_search(&serverset, server);
		if (orig != NULL) {
			/* Use existing struct server */
			orig->applier = server->applier;
			assert(tt_uuid_is_equal(&orig->uuid,
						&orig->applier->uuid));
			server->applier = NULL;
			server_delete(server); /* remove temporary object */
			server = orig;
		} else {
			/* Add a new struct server */
			serverset_insert(&serverset, server);
		}
	}

	assert(serverset_first(&uniq) == NULL);
	serverset_foreach_safe(&serverset, server, next) {
		if (server_is_orphan(server)) {
			serverset_remove(&serverset, server);
			server_delete(server);
		}
	}
}

void
server_set_relay(struct server *server, struct relay *relay)
{
	assert(!server_id_is_reserved(server->id));
	assert(server->relay == NULL);
	server->relay = relay;
}

void
server_clear_relay(struct server *server)
{
	assert(server->relay != NULL);
	server->relay = NULL;
	if (server_is_orphan(server)) {
		serverset_remove(&serverset, server);
		server_delete(server);
	}
}

struct server *
server_first(void)
{
	return serverset_first(&serverset);
}

struct server *
server_next(struct server *server)
{
	return serverset_next(&serverset, server);
}

struct server *
server_by_uuid(const struct tt_uuid *uuid)
{
	struct server key;
	key.uuid = *uuid;
	return serverset_search(&serverset, &key);
}
