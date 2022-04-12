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
#include "sio.h"
#include "memory.h"
#include "assoc.h"
#include "trigger.h"
#include "user.h"
#include "error.h"
#include "tt_static.h"
#include "sql_stmt_cache.h"

const char *session_type_strs[] = {
	"background",
	"binary",
	"console",
	"repl",
	"applier",
	"unknown",
};

static struct session_vtab generic_session_vtab = {
	/* .push = */ generic_session_push,
	/* .fd = */ generic_session_fd,
	/* .sync = */ generic_session_sync,
};

struct session_vtab session_vtab_registry[] = {
	/* BACKGROUND */ generic_session_vtab,
	/* BINARY */ generic_session_vtab,
	/* CONSOLE */ generic_session_vtab,
	/* REPL */ generic_session_vtab,
	/* APPLIER */ generic_session_vtab,
};

static struct mh_i64ptr_t *session_registry;

uint32_t default_flags = 0;

struct mempool session_pool;

RLIST_HEAD(session_on_connect);
RLIST_HEAD(session_on_disconnect);
RLIST_HEAD(session_on_auth);

static inline uint64_t
sid_max(void)
{
	static uint64_t sid_max = 0;
	/* Return the next sid rolling over the reserved value of 0. */
	while (++sid_max == 0)
		;
	return sid_max;
}

static int
session_on_stop(struct trigger *trigger, void * /* event */)
{
	/*
	 * Remove on_stop trigger from the fiber, otherwise the
	 * fiber will attempt to destroy the trigger eventually,
	 * after the trigger and its memory is long gone.
	 */
	trigger_clear(trigger);
	/* Destroy the session */
	session_destroy(fiber_get_session(fiber()));
	return 0;
}

static int
closed_session_push(struct session *session, struct port *port)
{
	(void) session;
	(void) port;
	diag_set(ClientError, ER_SESSION_CLOSED);
	return -1;
}

static struct session_vtab closed_session_vtab = {
	/* .push = */ closed_session_push,
	/* .fd = */ generic_session_fd,
	/* .sync = */ generic_session_sync,
};

void
session_close(struct session *session)
{
	session->vtab = &closed_session_vtab;
}

void
session_set_type(struct session *session, enum session_type type)
{
	assert(type < session_type_MAX);
	assert(session->vtab != &closed_session_vtab);
	session->type = type;
	session->vtab = &session_vtab_registry[type];
}

struct session *
session_create(enum session_type type)
{
	struct session *session =
		(struct session *) mempool_alloc(&session_pool);
	if (session == NULL) {
		diag_set(OutOfMemory, session_pool.objsize, "mempool",
			 "new slab");
		return NULL;
	}

	session->id = sid_max();
	memset(&session->meta, 0, sizeof(session->meta));
	session_set_type(session, type);
	session->sql_flags = default_flags;
	session->sql_default_engine = SQL_STORAGE_ENGINE_MEMTX;
	session->sql_stmts = NULL;

	/* For on_connect triggers. */
	credentials_create(&session->credentials, guest_user);
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
session_create_on_demand(void)
{
	assert(fiber_get_session(fiber()) == NULL);

	/* Create session on demand */
	struct session *s = session_create(SESSION_TYPE_BACKGROUND);
	if (s == NULL)
		return NULL;
	s->fiber_on_stop = {
		RLIST_LINK_INITIALIZER, session_on_stop, NULL, NULL
	};
	/* Add a trigger to destroy session on fiber stop */
	trigger_add(&fiber()->on_stop, &s->fiber_on_stop);
	credentials_reset(&s->credentials, admin_user);
	fiber_set_session(fiber(), s);
	fiber_set_user(fiber(), &s->credentials);
	return s;
}

bool
session_check_stmt_id(struct session *session, uint32_t stmt_id)
{
	if (session->sql_stmts == NULL)
		return false;
	mh_int_t i = mh_i32ptr_find(session->sql_stmts, stmt_id, NULL);
	return i != mh_end(session->sql_stmts);
}

int
session_add_stmt_id(struct session *session, uint32_t id)
{
	if (session->sql_stmts == NULL) {
		session->sql_stmts = mh_i32ptr_new();
		if (session->sql_stmts == NULL) {
			diag_set(OutOfMemory, 0, "mh_i32ptr_new",
				 "session stmt hash");
			return -1;
		}
	}
	return sql_session_stmt_hash_add_id(session->sql_stmts, id);
}

void
session_remove_stmt_id(struct session *session, uint32_t stmt_id)
{
	assert(session->sql_stmts != NULL);
	mh_int_t i = mh_i32ptr_find(session->sql_stmts, stmt_id, NULL);
	assert(i != mh_end(session->sql_stmts));
	mh_i32ptr_del(session->sql_stmts, i, NULL);
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

	/* Run triggers with admin credentials */
	fiber_set_user(fiber, &admin_credentials);

	int rc = trigger_run(triggers, NULL);

	/* Restore original credentials */
	fiber_set_user(fiber, &session->credentials);

	return rc;
}

void
session_run_on_disconnect_triggers(struct session *session)
{
	if (session_run_triggers(session, &session_on_disconnect) != 0)
		diag_log();
}

int
session_run_on_connect_triggers(struct session *session)
{
	return session_run_triggers(session, &session_on_connect);
}

int
session_run_on_auth_triggers(const struct on_auth_trigger_ctx *result)
{
	return trigger_run(&session_on_auth, (void *)result);
}

void
session_destroy(struct session *session)
{
	session_storage_cleanup(session->id);
	struct mh_i64ptr_node_t node = { session->id, NULL };
	mh_i64ptr_remove(session_registry, &node, NULL);
	credentials_destroy(&session->credentials);
	sql_session_stmt_hash_erase(session->sql_stmts);
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

const char *
session_peer(const struct session *session)
{
	if (session->meta.peer.addrlen == 0)
		return NULL;
	return sio_strfaddr(&session->meta.peer.addr,
			    session->meta.peer.addrlen);
}

extern "C" void
session_settings_init(void);

void
session_init(void)
{
	session_registry = mh_i64ptr_new();
	if (session_registry == NULL)
		panic("out of memory");
	mempool_create(&session_pool, &cord()->slabc, sizeof(struct session));
	credentials_create(&admin_credentials, admin_user);
	session_settings_init();
}

void
session_free(void)
{
	if (session_registry)
		mh_i64ptr_delete(session_registry);
	credentials_destroy(&admin_credentials);
}

int
access_check_session(struct user *user)
{
	/*
	 * Can't use here access_check_universe
	 * as current_user is not assigned yet
	 */
	if (!(universe.access[user->auth_token].effective & PRIV_S)) {
		diag_set(AccessDeniedError, priv_name(PRIV_S),
			 schema_object_name(SC_UNIVERSE), "",
			 user->def->name);
		return -1;
	}
	return 0;
}

int
access_check_universe_object(user_access_t access,
			     enum schema_object_type object_type,
			     const char *object_name)
{
	struct credentials *credentials = effective_user();
	access |= PRIV_U;
	if ((credentials->universal_access & access) ^ access) {
		/*
		 * Access violation, report error.
		 * The user may not exist already, if deleted
		 * from a different connection.
		 */
		int denied_access = access & ((credentials->universal_access
					       & access) ^ access);
		struct user *user = user_find(credentials->uid);
		if (user != NULL) {
			diag_set(AccessDeniedError,
				 priv_name(denied_access),
				 schema_object_name(object_type), object_name,
				 user->def->name);
		} else {
			/*
			 * The user may have been dropped, in
			 * which case user_find() will set the
			 * error.
			 */
			assert(!diag_is_empty(&fiber()->diag));
		}
		return -1;
	}
	return 0;
}

int
access_check_universe(user_access_t access)
{
	return access_check_universe_object(access, SC_UNIVERSE, "");
}

int
generic_session_push(struct session *session, struct port *port)
{
	(void) port;
	const char *name =
		tt_sprintf("Session '%s'", session_type_strs[session->type]);
	diag_set(ClientError, ER_UNSUPPORTED, name, "push()");
	return -1;
}

int
generic_session_fd(struct session *session)
{
	(void) session;
	return -1;
}

int64_t
generic_session_sync(struct session *session)
{
	(void) session;
	return 0;
}
