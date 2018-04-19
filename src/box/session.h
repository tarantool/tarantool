#ifndef INCLUDES_TARANTOOL_SESSION_H
#define INCLUDES_TARANTOOL_SESSION_H
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
#include <inttypes.h>
#include <stdbool.h>
#include "trigger.h"
#include "fiber.h"
#include "user.h"
#include "authentication.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

void
session_init();

void
session_free();

enum session_type {
	SESSION_TYPE_BACKGROUND = 0,
	SESSION_TYPE_BINARY,
	SESSION_TYPE_CONSOLE,
	SESSION_TYPE_REPL,
	SESSION_TYPE_APPLIER,
	session_type_MAX,
};

extern const char *session_type_strs[];

/**
 * Abstraction of a single user session:
 * for now, only provides accounting of established
 * sessions and on-connect/on-disconnect event
 * handling, user credentials. In future: the
 * client/server protocol, etc.
 * Session identifiers grow monotonically.
 * 0 sid is reserved to mean 'no session'.
 */
struct session {
	/** Session id. */
	uint64_t id;
	/** File descriptor - socket of the connected peer.
	 * Only if the session has a peer.
	 */
	int fd;
	/**
	 * For iproto requests, we set this field
	 * to the value of packet sync. Since the
	 * session may be reused between many requests,
	 * the value is true only at the beginning
	 * of the request, and gets distorted after
	 * the first yield.
	 */
	uint64_t sync;
	enum session_type type;
	/** Session user id and global grants */
	struct credentials credentials;
	/** Trigger for fiber on_stop to cleanup created on-demand session */
	struct trigger fiber_on_stop;
};

/**
 * Find a session by id.
 */
struct session *
session_find(uint64_t sid);

/** Global on-connect triggers. */
extern struct rlist session_on_connect;

extern struct rlist session_on_auth;

/**
 * Get the current session from @a fiber
 * @param fiber fiber
 * @return session if any
 * @retval NULL if there is no active session
 */
static inline struct session *
fiber_get_session(struct fiber *fiber)
{
	return (struct session *) fiber_get_key(fiber, FIBER_KEY_SESSION);
}

/**
 * Set the current session in @a fiber
 * @param fiber fiber
 * @param session a value to set
 */
static inline void
fiber_set_user(struct fiber *fiber, struct credentials *cr)
{
	fiber_set_key(fiber, FIBER_KEY_USER, cr);
}

static inline void
fiber_set_session(struct fiber *fiber, struct session *session)
{
	fiber_set_key(fiber, FIBER_KEY_SESSION, session);
}

static inline void
credentials_init(struct credentials *cr, uint8_t auth_token, uint32_t uid)
{
	cr->auth_token = auth_token;
	cr->universal_access = universe.access[cr->auth_token].effective;
	cr->uid = uid;
}

/*
 * For use in local hot standby, which runs directly
 * from ev watchers (without current fiber), but needs
 * to execute transactions.
 */
extern struct credentials admin_credentials;

/**
 * Create a new session on demand, and set fiber on_stop
 * trigger to destroy it when this fiber ends.
 */
struct session *
session_create_on_demand(int fd);

/*
 * When creating a new fiber, the database (box)
 * may not be initialized yet. When later on
 * this fiber attempts to access the database,
 * we have no other choice but initialize fiber-specific
 * database state (something like a database connection)
 * on demand. This is why this function needs to
 * check whether or not the current session exists
 * and create it otherwise.
 */
static inline struct session *
current_session()
{
	struct session *session = fiber_get_session(fiber());
	if (session == NULL) {
		session = session_create_on_demand(-1);
		if (session == NULL)
			diag_raise();
	}
	return session;
}

/*
 * Return the current user. Create it if it doesn't
 * exist yet.
 * The same rationale for initializing the current
 * user on demand as in current_session() applies.
 */
static inline struct credentials *
effective_user()
{
	struct credentials *u =
		(struct credentials *) fiber_get_key(fiber(),
						     FIBER_KEY_USER);
	if (u == NULL) {
		session_create_on_demand(-1);
		u = (struct credentials *) fiber_get_key(fiber(),
							 FIBER_KEY_USER);
	}
	return u;
}

/** Global on-disconnect triggers. */
extern struct rlist session_on_disconnect;

void
session_storage_cleanup(int sid);

/**
 * Create a session.
 * Invokes a Lua trigger box.session.on_connect if it is
 * defined. Issues a new session identifier.
 * Must called by the networking layer
 * when a new connection is established.
 *
 * @return handle for a created session
 * @exception tnt_Exception or lua error if session
 * trigger fails or runs out of resources.
 */
struct session *
session_create(int fd, enum session_type type);

/**
 * Destroy a session.
 * Must be called by the networking layer on disconnect.
 * Invokes a Lua trigger box.session.on_disconnect if it
 * is defined.
 * @param session   session to destroy. may be NULL.
 *
 * @exception none
 */
void
session_destroy(struct session *);

/** Run on-connect triggers */
int
session_run_on_connect_triggers(struct session *session);

/** Run on-disconnect triggers */
void
session_run_on_disconnect_triggers(struct session *session);

/** Run auth triggers */
int
session_run_on_auth_triggers(const struct on_auth_trigger_ctx *result);

/**
 * Check whether or not the current user is authorized to connect
 */
int
access_check_session(struct user *user);

/**
 * Check whether or not the current user can be granted
 * the requested access to the universe.
 */
int
access_check_universe(user_access_t access);

#if defined(__cplusplus)
} /* extern "C" */

#include "diag.h"

static inline void
access_check_session_xc(struct user *user)
{
	if (access_check_session(user) != 0)
		diag_raise();
}

static inline void
access_check_universe_xc(user_access_t access)
{
	if (access_check_universe(access) != 0)
		diag_raise();
}

#endif /* defined(__cplusplus) */

#endif /* INCLUDES_TARANTOOL_SESSION_H */
