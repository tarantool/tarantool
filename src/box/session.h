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

extern void sql_session_settings_init();

struct port;
struct session_vtab;

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
 * default_flags accumulates flags value from SQL submodules.
 * It is assigned during sql_init(). Lately it is used in each session
 * initialization.
 */
extern uint32_t default_flags;

/**
 * Session meta is used in different ways by sessions of different
 * types, and allows to do not store attributes in struct session,
 * that are used only by a session of particular type.
 */
union session_meta {
	/** IProto connection. */
	void *connection;
	/** Console file/socket descriptor. */
	int fd;
};

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
	/** SQL Tarantool Default storage engine. */
	uint8_t sql_default_engine;
	/** SQL Connection flag for current user session */
	uint32_t sql_flags;
	enum session_type type;
	/** Session virtual methods. */
	const struct session_vtab *vtab;
	/** Session metadata. */
	union session_meta meta;
	/**
	 * ID of statements prepared in current session.
	 * This map is allocated on demand.
	 */
	struct mh_i32ptr_t *sql_stmts;
	/** Session user id and global grants */
	struct credentials credentials;
	/** Trigger for fiber on_stop to cleanup created on-demand session */
	struct trigger fiber_on_stop;
};

struct session_vtab {
	/**
	 * Push a port data into a session data channel - socket,
	 * console or something.
	 * @param session Session to push into.
	 * @param sync Request sync in scope of which to send the
	 *        push.
	 * @param port Port with data to push.
	 *
	 * @retval  0 Success.
	 * @retval -1 Error.
	 */
	int
	(*push)(struct session *session, uint64_t sync, struct port *port);
	/**
	 * Get session file descriptor if exists.
	 * @param session Session to get descriptor from.
	 * @retval  -1 No fd.
	 * @retval >=0 Found fd.
	 */
	int
	(*fd)(struct session *session);
	/**
	 * For iproto requests, we set sync to the value of packet
	 * sync. Since the session may be reused between many
	 * requests, the value is true only at the beginning
	 * of the request, and gets distorted after the first
	 * yield. For other sessions it is 0.
	 */
	int64_t
	(*sync)(struct session *session);
};

extern struct session_vtab session_vtab_registry[];

/** Change session type and vtab. */
void
session_set_type(struct session *session, enum session_type type);

/**
 * Close a session. It will return errors from all virtual methods
 * and its type is fixed.
 */
void
session_close(struct session *session);

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
	return fiber->storage.session;
}

/**
 * Set the current session in @a fiber
 * @param fiber fiber
 * @param session a value to set
 */
static inline void
fiber_set_user(struct fiber *fiber, struct credentials *cr)
{
	fiber->storage.credentials = cr;
}

static inline void
fiber_set_session(struct fiber *fiber, struct session *session)
{
	fiber->storage.session = session;
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
session_create_on_demand();

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
		session = session_create_on_demand();
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
	struct fiber *f = fiber();
	struct credentials *u = f->storage.credentials;
	if (u == NULL) {
		session_create_on_demand();
		u = f->storage.credentials;
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
session_create(enum session_type type);

/** Return true if given statement id belongs to the session. */
bool
session_check_stmt_id(struct session *session, uint32_t stmt_id);

/** Add prepared statement ID to the session hash. */
int
session_add_stmt_id(struct session *session, uint32_t stmt_id);

/** Remove prepared statement ID from the session hash. */
void
session_remove_stmt_id(struct session *session, uint32_t stmt_id);

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

/**
 * Same as access_check_universe(), but in case the current user
 * doesn't have universal access, set AccessDeniedError for the
 * given object type and name.
 */
int
access_check_universe_object(user_access_t access,
			     enum schema_object_type object_type,
			     const char *object_name);

static inline int
session_push(struct session *session, uint64_t sync, struct port *port)
{
	return session->vtab->push(session, sync, port);
}

static inline int
session_fd(struct session *session)
{
	return session->vtab->fd(session);
}

static inline int
session_sync(struct session *session)
{
	return session->vtab->sync(session);
}

/**
 * In a common case, a session does not support push. This
 * function always returns -1 and sets ER_UNSUPPORTED error.
 */
int
generic_session_push(struct session *session, uint64_t sync, struct port *port);

/** Return -1 from any session. */
int
generic_session_fd(struct session *session);

/** Return 0 from any session. */
int64_t
generic_session_sync(struct session *session);

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
