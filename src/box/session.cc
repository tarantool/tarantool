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
#include "session.h"
#include "fiber.h"
#include "memory.h"

#include "assoc.h"
#include "trigger.h"
#include "random.h"
#include "user.h"

static struct mh_i32ptr_t *session_registry;

struct mempool session_pool;

RLIST_HEAD(session_on_connect);
RLIST_HEAD(session_on_disconnect);
RLIST_HEAD(session_on_auth);

static inline  uint32_t
sid_max()
{
	static uint32_t sid_max = 0;
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
	struct session *session = (struct session *)
		mempool_alloc_xc(&session_pool);
	session->id = sid_max();
	session->fd =  fd;
	session->sync = 0;
	/* For on_connect triggers. */
	credentials_init(&session->credentials, guest_user);
	if (fd >= 0)
		random_bytes(session->salt, SESSION_SEED_SIZE);
	struct mh_i32ptr_node_t node;
	node.key = session->id;
	node.val = session;

	mh_int_t k = mh_i32ptr_put(session_registry, &node, NULL, NULL);

	if (k == mh_end(session_registry)) {
		mempool_free(&session_pool, session);
		tnt_raise(ClientError, ER_MEMORY_ISSUE,
			  "session hash", "new session");
	}
	return session;
}

struct session *
session_create_on_demand()
{
	/* Create session on demand */
	struct session *s = session_create(-1);
	s->fiber_on_stop = {
		RLIST_LINK_INITIALIZER, session_on_stop, NULL, NULL
	};
	/* Add a trigger to destroy session on fiber stop */
	trigger_add(&fiber()->on_stop, &s->fiber_on_stop);
	credentials_init(&s->credentials, admin_user);
	fiber_set_session(fiber(), s);
	fiber_set_user(fiber(), &s->credentials);
	return s;
}

/**
 * To quickly switch to admin user when executing
 * on_connect/on_disconnect triggers in iproto.
 */
struct credentials admin_credentials;

void
session_run_on_disconnect_triggers(struct session *session)
{
	struct fiber *fiber = fiber();
	/* For triggers. */
	fiber_set_session(fiber, session);
	fiber_set_user(fiber, &admin_credentials);
	try {
		trigger_run(&session_on_disconnect, NULL);
	} catch (Exception *e) {
		e->log();
	}
	session_storage_cleanup(session->id);
}

void
session_run_on_connect_triggers(struct session *session)
{
	/* Run on_connect with admin credentals */
	struct fiber *fiber = fiber();
	fiber_set_session(fiber, session);
	fiber_set_user(fiber, &admin_credentials);
	trigger_run(&session_on_connect, NULL);
	/* Set session user to guest, until it is authenticated. */
}

void
session_run_on_auth_triggers(const char *user_name)
{
	trigger_run(&session_on_auth, (void *)user_name);
}

void
session_destroy(struct session *session)
{
	struct mh_i32ptr_node_t node = { session->id, NULL };
	mh_i32ptr_remove(session_registry, &node, NULL);
	mempool_free(&session_pool, session);
}

struct session *
session_find(uint32_t sid)
{
	mh_int_t k = mh_i32ptr_find(session_registry, sid, NULL);
	if (k == mh_end(session_registry))
		return NULL;
	return (struct session *)
		mh_i32ptr_node(session_registry, k)->val;
}

void
session_init()
{
	session_registry = mh_i32ptr_new();
	if (session_registry == NULL)
		panic("out of memory");
	mempool_create(&session_pool, &cord()->slabc, sizeof(struct session));
	credentials_init(&admin_credentials, admin_user);
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
		mh_i32ptr_delete(session_registry);
}
