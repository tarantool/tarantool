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
#include "session.h"
#include "fiber.h"
#include "memory.h"

#include "user_def.h"
#include "assoc.h"
#include "trigger.h"
#include "random.h"
#include "user.h"

static struct mh_i64ptr_t *session_registry;

struct mempool session_pool;

RLIST_HEAD(session_on_connect);
RLIST_HEAD(session_on_disconnect);
RLIST_HEAD(session_on_auth);

static inline uint64_t
sid_max()
{
	static uint64_t sid_max = 0;
	/* Return the next sid rolling over the reserved value of 0. */
	while (++sid_max == 0)
		;
	return sid_max;
}

static void
session_on_stop(struct trigger *trigger, void * /* event */)
{
	/*
	 * Remove on_stop trigger from the fiber, otherwise the
	 * fiber will attempt to destroy the trigger eventually,
	 * after the trigger and its memory is long gone.
	 */
	trigger_clear(trigger);
	struct session *session = (struct session *)
		fiber_get_key(fiber(), FIBER_KEY_SESSION);
	/* Destroy the session */
	session_destroy(session);
}

struct session *
session_create(int fd)
{
	struct session *session =
		(struct session *) mempool_alloc(&session_pool);
	if (session == NULL) {
		diag_set(OutOfMemory, session_pool.objsize, "mempool",
			 "new slab");
		return NULL;
	}
	session->id = sid_max();
	session->fd =  fd;
	session->sync = 0;
	/* For on_connect triggers. */
	credentials_init(&session->credentials, guest_user->auth_token,
			 guest_user->def.uid);
	if (fd >= 0)
		random_bytes(session->salt, SESSION_SEED_SIZE);
	struct mh_i64ptr_node_t node;
	node.key = session->id;
	node.val = session;

	mh_int_t k = mh_i64ptr_put(session_registry, &node, NULL, NULL);

	if (k == mh_end(session_registry)) {
		mempool_free(&session_pool, session);
		diag_set(OutOfMemory, 0, "session hash", "new session");
		return NULL;
	}
	return session;
}

struct session *
session_create_on_demand(int fd)
{
	assert(fiber_get_session(fiber()) == NULL);

	/* Create session on demand */
	struct session *s = session_create(fd);
	if (s == NULL)
		return NULL;
	s->fiber_on_stop = {
		RLIST_LINK_INITIALIZER, session_on_stop, NULL, NULL
	};
	/* Add a trigger to destroy session on fiber stop */
	trigger_add(&fiber()->on_stop, &s->fiber_on_stop);
	credentials_init(&s->credentials, admin_user->auth_token,
			 admin_user->def.uid);
	fiber_set_session(fiber(), s);
	fiber_set_user(fiber(), &s->credentials);
	return s;
}

/**
 * To quickly switch to admin user when executing
 * on_connect/on_disconnect triggers in iproto.
 */
struct credentials admin_credentials;

static int
session_run_triggers(struct session *session, struct rlist *triggers)
{
	struct fiber *fiber = fiber();
	assert(session == current_session());

	/* Save original credentials */
	struct credentials orig_credentials;
	credentials_copy(&orig_credentials, &session->credentials);

	/* Run triggers with admin credentals */
	credentials_copy(&session->credentials, &admin_credentials);
	fiber_set_session(fiber, session);
	fiber_set_user(fiber, &session->credentials);

	int rc = 0;
	try {
		trigger_run(triggers, NULL);
	} catch (Exception *e) {
		rc = -1;
	}

	/* Restore original credentials */
	credentials_copy(&session->credentials, &orig_credentials);
	fiber_set_user(fiber, &session->credentials);

	return rc;
}

void
session_run_on_disconnect_triggers(struct session *session)
{
	if (session_run_triggers(session, &session_on_disconnect) != 0)
		error_log(diag_last_error(diag_get()));
	session_storage_cleanup(session->id);
}

int
session_run_on_connect_triggers(struct session *session)
{
	return session_run_triggers(session, &session_on_connect);
}

int
session_run_on_auth_triggers(const char *user_name)
{
	try {
		trigger_run(&session_on_auth, (void *)user_name);
		return 0;
	} catch(Exception *e) {
		return -1;
	}
}

void
session_destroy(struct session *session)
{
	struct mh_i64ptr_node_t node = { session->id, NULL };
	mh_i64ptr_remove(session_registry, &node, NULL);
	mempool_free(&session_pool, session);
}

struct session *
session_find(uint64_t sid)
{
	mh_int_t k = mh_i64ptr_find(session_registry, sid, NULL);
	if (k == mh_end(session_registry))
		return NULL;
	return (struct session *)
		mh_i64ptr_node(session_registry, k)->val;
}

void
session_init()
{
	session_registry = mh_i64ptr_new();
	if (session_registry == NULL)
		panic("out of memory");
	mempool_create(&session_pool, &cord()->slabc, sizeof(struct session));
	credentials_init(&admin_credentials, ADMIN, ADMIN);
	/**
	 * When session_init() is called, admin user access is not
	 * loaded yet (is 0), force global access.
	 */
	admin_credentials.universal_access = PRIV_ALL;
}

void
session_free()
{
	if (session_registry)
		mh_i64ptr_delete(session_registry);
}
