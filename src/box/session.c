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
#include "fiber_cond.h"
#include "sio.h"
#include "memory.h"
#include "assoc.h"
#include "trigger.h"
#include "user.h"
#include "error.h"
#include "tt_static.h"
#include "sql_stmt_cache.h"
#include "watcher.h"
#include "on_shutdown.h"
#include "sql.h"
#include "tweaks.h"

const char *session_type_strs[] = {
	"background",
	"binary",
	"console",
	"repl",
	"applier",
	"unknown",
};

static const struct session_vtab generic_session_vtab = {
	/* .push = */ generic_session_push,
	/* .fd = */ generic_session_fd,
	/* .sync = */ generic_session_sync,
};

struct session_vtab session_vtab_registry[session_type_MAX];

static struct mh_i64ptr_t *session_registry;

struct mempool session_pool;

/**
 * List of sessions blocking shutdown, linked by session::in_shutdown_list.
 * The shutdown trigger callback won't return until it's empty.
 */
static RLIST_HEAD(shutdown_list);

/** Signalled when shutdown_list becomes empty. */
static struct fiber_cond shutdown_list_empty_cond;

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
session_on_stop(struct trigger *trigger, void *event)
{
	(void)event;
	/*
	 * Remove on_stop trigger from the fiber, otherwise the
	 * fiber will attempt to destroy the trigger eventually,
	 * after the trigger and its memory is long gone.
	 */
	trigger_clear(trigger);
	/* Destroy the session */
	session_delete(fiber_get_session(fiber()));
	return 0;
}

/**
 * Watcher registered for a session. Unregistered when the session is closed.
 */
struct session_watcher {
	/** Base class. */
	struct watcher base;
	/** Session that registered this watcher. */
	struct session *session;
	/** Request sync number used on watch. */
	uint64_t sync;
	/** Watcher callback. */
	session_notify_f cb;
};

static void
session_watcher_run_f(struct watcher *base)
{
	struct session_watcher *watcher = (struct session_watcher *)base;
	size_t key_len;
	const char *key = watcher_key(base, &key_len);
	const char *data_end;
	const char *data = watcher_data(base, &data_end);
	watcher->cb(watcher->session, watcher->sync, key, key_len,
		    data, data_end);
}

void
session_watch(struct session *session, uint64_t sync,
	      const char *key, size_t key_len, session_notify_f cb)
{
	/* Look up a watcher for the specified key in this session. */
	struct mh_strnptr_t *h = session->watchers;
	if (h == NULL)
		h = session->watchers = mh_strnptr_new();
	uint32_t key_hash = mh_strn_hash(key, key_len);
	struct mh_strnptr_key_t k = {key, (uint32_t)key_len, key_hash};
	mh_int_t i = mh_strnptr_find(h, &k, NULL);
	/* If a watcher is already registered, acknowledge a notification. */
	if (i != mh_end(h)) {
		struct session_watcher *watcher =
			(struct session_watcher *)mh_strnptr_node(h, i)->val;
		watcher->sync = sync;
		watcher_ack(&watcher->base);
		return;
	}
	/* Otherwise register a new watcher. */
	struct session_watcher *watcher =
		(struct session_watcher *)xmalloc(sizeof(*watcher));
	watcher->session = session;
	watcher->sync = sync;
	watcher->cb = cb;
	box_register_watcher(key, key_len, session_watcher_run_f,
			     (watcher_destroy_f)free, WATCHER_EXPLICIT_ACK,
			     &watcher->base);
	key = watcher_key(&watcher->base, &key_len);
	struct mh_strnptr_node_t n = {
		key, (uint32_t)key_len, key_hash, watcher
	};
	mh_strnptr_put(h, &n, NULL, NULL);
}

void
session_unwatch(struct session *session, const char *key,
		size_t key_len)
{
	struct mh_strnptr_t *h = session->watchers;
	if (h == NULL)
		return;
	mh_int_t i = mh_strnptr_find_str(h, key, key_len);
	if (i == mh_end(h))
		return;
	struct session_watcher *watcher =
		(struct session_watcher *)mh_strnptr_node(h, i)->val;
	mh_strnptr_del(h, i, NULL);
	watcher_unregister(&watcher->base);
}

/**
 * Returns true if the session is watching the given key.
 * They key string is zero-terminated.
 */
static bool
session_is_watching(struct session *session, const char *key)
{
	struct mh_strnptr_t *h = session->watchers;
	if (h == NULL)
		return false;
	uint32_t key_len = strlen(key);
	uint32_t key_hash = mh_strn_hash(key, key_len);
	struct mh_strnptr_key_t k = {key, key_len, key_hash};
	return mh_strnptr_find(h, &k, NULL) != mh_end(h);
}

/**
 * Unregisters all watchers registered in this session.
 * Called when the session is closed.
 */
static void
session_unregister_all_watchers(struct session *session)
{
	struct mh_strnptr_t *h = session->watchers;
	if (h == NULL)
		return;
	mh_int_t i;
	mh_foreach(h, i) {
		struct session_watcher *watcher =
			(struct session_watcher *)mh_strnptr_node(h, i)->val;
		watcher_unregister(&watcher->base);
	}
	mh_strnptr_delete(h);
	session->watchers = NULL;
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
	session_unregister_all_watchers(session);
	rlist_del_entry(session, in_shutdown_list);
	if (rlist_empty(&session->in_shutdown_list))
		fiber_cond_broadcast(&shutdown_list_empty_cond);
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
session_new(enum session_type type)
{
	struct session *session = xmempool_alloc(&session_pool);
	session->id = sid_max();
	memset(&session->meta, 0, sizeof(session->meta));
	session_set_type(session, type);
	session->sql_flags = sql_default_session_flags();
	session->sql_default_engine = SQL_STORAGE_ENGINE_MEMTX;
	session->sql_stmts = NULL;
	session->watchers = NULL;
	rlist_create(&session->in_shutdown_list);

	/* For on_connect triggers. */
	credentials_create(&session->credentials, guest_user);
	struct mh_i64ptr_node_t node;
	node.key = session->id;
	node.val = session;
	mh_i64ptr_put(session_registry, &node, NULL, NULL);
	return session;
}

struct session *
session_new_on_demand(void)
{
	assert(fiber_get_session(fiber()) == NULL);

	/* Create session on demand */
	struct session *s = session_new(SESSION_TYPE_BACKGROUND);
	/* Add a trigger to destroy session on fiber stop */
	trigger_create(&s->fiber_on_stop, session_on_stop, NULL, NULL);
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

void
session_add_stmt_id(struct session *session, uint32_t id)
{
	if (session->sql_stmts == NULL) {
		session->sql_stmts = mh_i32ptr_new();
	}
	sql_session_stmt_hash_add_id(session->sql_stmts, id);
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
session_delete(struct session *session)
{
	/* Watchers are unregistered in session_close(). */
	assert(session->watchers == NULL);
	assert(rlist_empty(&session->in_shutdown_list));
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

void
session_settings_init(void);

/**
 * Waits for all sessions that subscribed to 'box.shutdown' event to close.
 */
static int
session_on_shutdown_f(void *arg)
{
	(void)arg;
	fiber_set_name(fiber_self(), "session.shutdown");
	struct mh_i64ptr_t *h = session_registry;
	mh_int_t i;
	mh_foreach(h, i) {
		struct session *session =
			(struct session *)mh_i64ptr_node(h, i)->val;
		if (session_is_watching(session, "box.shutdown")) {
			rlist_add_entry(&shutdown_list, session,
					in_shutdown_list);
		}
	}
	while (!rlist_empty(&shutdown_list))
		fiber_cond_wait(&shutdown_list_empty_cond);
	return 0;
}

void
session_init(void)
{
	for (int type = 0; type < session_type_MAX; type++)
		session_vtab_registry[type] = generic_session_vtab;
	session_registry = mh_i64ptr_new();
	mempool_create(&session_pool, &cord()->slabc, sizeof(struct session));
	credentials_create(&admin_credentials, admin_user);
	session_settings_init();
	fiber_cond_create(&shutdown_list_empty_cond);
	if (box_on_shutdown(NULL, session_on_shutdown_f, NULL) != 0)
		panic("failed to set session shutdown trigger");
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
access_check_universe(user_access_t access)
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
				 schema_object_name(SC_UNIVERSE), "",
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

/**
 * If set, raise an error on any attempt to use box.session.push.
 */
static bool box_session_push_is_disabled = true;
TWEAK_BOOL(box_session_push_is_disabled);

int
session_push_check_deprecation(void)
{
	say_warn_once("box.session.push is deprecated. "
		      "Consider using box.broadcast instead.");
	if (box_session_push_is_disabled) {
		diag_set(ClientError, ER_DEPRECATED, "box.session.push");
		return -1;
	}
	return 0;
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
