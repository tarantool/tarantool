#ifndef INCLUDES_TARANTOOL_SESSION_H
#define INCLUDES_TARANTOOL_SESSION_H
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
#include <inttypes.h>
#include <stdbool.h>
#include "trigger.h"
#include "fiber.h"

enum {	SESSION_SEED_SIZE = 32, SESSION_DELIM_SIZE = 16 };
/** Predefined user ids. */
enum { GUEST = 0, ADMIN =  1, PUBLIC = 2 /* role */ };

/**
 * Abstraction of a single user session:
 * for now, only provides accounting of established
 * sessions and on-connect/on-disconnect event
 * handling, in future: user credentials, protocol, etc.
 * Session identifiers grow monotonically.
 * 0 sid is reserved to mean 'no session'.
 */

struct session {
	/** Session id. */
	uint32_t id;
	/** File descriptor - socket of the connected peer. */
	int fd;
	/** Peer cookie - description of the peer. */
	uint64_t cookie;
	/** Authentication salt. */
	char salt[SESSION_SEED_SIZE];
	/** A look up key to quickly find session user. */
	uint8_t auth_token;
	/** User id of the authenticated user. */
	uint32_t uid;
	/** Current transaction, if started. */
	struct txn *txn;
	/** Trigger for fiber on_stop to cleanup created on-demand session */
	struct trigger fiber_on_stop;
};

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
session_create(int fd, uint64_t cookie);

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

/**
 * Find a session by id.
 */
struct session *
session_find(uint32_t sid);

/** Set session auth token and user id. */
static inline void
session_set_user(struct session *session, uint8_t auth_token, uint32_t uid)
{
	session->auth_token = auth_token;
	session->uid = uid;
}

/** Global on-connect triggers. */
extern struct rlist session_on_connect;

/** Run on-connect triggers */
void
session_run_on_connect_triggers(struct session *session);

/** Global on-disconnect triggers. */
extern struct rlist session_on_disconnect;

/** Run on-disconnect triggers */
void
session_run_on_disconnect_triggers(struct session *session);

void
session_init();

void
session_free();

void
session_storage_cleanup(int sid);

static inline struct session *
fiber_get_session(struct fiber *fiber)
{
	return (struct session *) fiber_get_key(fiber, FIBER_KEY_SESSION);
}

static inline void
fiber_set_session(struct fiber *fiber, struct session *session)
{
	fiber_set_key(fiber, FIBER_KEY_SESSION, session);
}

#define session() ({\
	struct session *s = fiber_get_session(fiber());				\
	/* Create session on demand */						\
	if (s == NULL) {							\
		s = session_create(-1, 0);					\
		fiber_set_session(fiber(), s);					\
		/* Add a trigger to destroy session on fiber stop */		\
		trigger_add(&fiber()->on_stop, &s->fiber_on_stop);		\
	}									\
	s; })

/** A helper class to create and set session in single-session fibers. */
struct SessionGuard
{
	struct session *session;
	SessionGuard(int fd, uint64_t cookie);
	~SessionGuard();
};

struct SessionGuardWithTriggers: public SessionGuard
{
	SessionGuardWithTriggers(int fd, uint64_t cookie);
	~SessionGuardWithTriggers();
};

#endif /* INCLUDES_TARANTOOL_SESSION_H */
