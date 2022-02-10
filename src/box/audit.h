/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2021, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include "trivia/config.h"

struct space;
struct port;

/** Audit log event codes. */
enum audit_event_code {
	CALL,
	EVAL,
	EVAL_CONSOLE,
	AUDIT_ENABLED,
	SPACE_SELECT,
	SPACE_INSERT,
	SPACE_REPLACE,
	SPACE_UPDATE,
	SPACE_UPSERT,
	SPACE_DELETE,
	SPACE_GET,
	AUTH_USER,
	NO_AUTH_USER,
	OPEN_CONNECT,
	CLOSE_CONNECT,
	USER_CREATED,
	USER_DELETED,
	ROLE_CREATED,
	ROLE_DELETED,
	USER_ENABLED,
	USER_DISABLED,
	USER_GRANT_RIGHTS,
	ROLE_GRANT_RIGHTS,
	USER_REVOKE_RIGHTS,
	ROLE_REVOKE_RIGHTS,
	PASSWORD_CHANGED,
	ACCESS_DENIED,
	CUSTOM,
	MESSAGE,
	AUDIT_EVENT_TYPE_MAX,
};

/** Structure which describes different audit lgo events. */
struct audit_event_ctx {
	/**
	 * Optional field that is used for custom audit
	 * log messages.
	 */
	const char *tag;
	/** Audit log event code. */
	enum audit_event_code code;
	/** User whose actions led to the audit event. */
	const char *user;
	/** Module that initiated audit event. */
	const char *module;
	/** Roles of the user. */
	const char *roles;
	/** Union which describe all different events. */
	union {
		struct {
			/**
			 * Function which was called over iproto
			 * connection.
			 */
			const char *func;
			/**
			 * Exprassion for evaluation passed over console
			 * connection.
			 */
			const char *expr;
			/** Function or expression arguments. */
			const char *args;
		} call_eval_event;
		struct {
			/**
			 * Audit log init string. Determines audit log
			 * file name.
			 */
			const char *audit_log;
			/**
			 * String describes audit log mode, can be true
			 * or false.
			 */
			const char *audit_nonblock;
			/**
			 * String representing all specified audit log
			 * filters.
			 */
			const char *audit_filter;
			/** Audit log format. Can be json, cvs or plain. */
			const char *audit_format;
		} audit_event;
		struct {
			/** Name of the space for current event. */
			const char *space;
		} space_event;
		struct {
			/** User name to which this event refers. */
			const char *user;
			/** Roles names to which this event refers. */
			const char *roles;
			/** Old user or role privileges. */
			const char *old_privs;
			/** New user or role privileges. */
			const char *new_privs;
			/**  Type of object to which this event refers. */
			const char *object_type;
			/** Name of object to which this event refers. */
			const char *object_name;
		} user_roles_event;
		struct {
			/** User name to which this event refers. */
			const char *user;
			/** Roles names to which this event refers. */
			const char *roles;
			/** Type of access that was denied. */
			const char *access_type;
			/**  Type of object to which this event refers. */
			const char *object_type;
			/** Name of object to which this event refers. */
			const char *object_name;
		} access_event;
		struct {
			/** Custom event type, sets by user. */
			const char *type;
			/** Custom event description, sets by user. */
			const char *description;
		} custom_event;
	};
};

/** Initialize audit log event context. */
static inline void
audit_event_ctx_init(struct audit_event_ctx *ctx, enum audit_event_code code)
{
	memset(ctx, 0, sizeof(struct audit_event_ctx));
	ctx->code = code;
}

#if defined(ENABLE_AUDIT_LOG)
# include "audit_impl.h"
#else /* !defined(ENABLE_AUDIT_LOG) */

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

static inline int
audit_log_init(const char *audit_log, int audit_nonblock,
	       const char *audit_format)
{
	(void)audit_log;
	(void)audit_nonblock;
	(void)audit_format;
	return 0;
}

static inline void
audit_log_free(void) {}

static inline void
audit_log_set_space_triggers(struct space *space)
{
	(void)space;
}

static inline void
audit_log(struct audit_event_ctx *ctx)
{
	(void)ctx;
}

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* !defined(ENABLE_AUDIT_LOG) */
