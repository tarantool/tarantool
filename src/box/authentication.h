/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2022, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct auth_method;

/** Default authentication method. */
extern const struct auth_method *AUTH_METHOD_DEFAULT;

/**
 * State passed to authentication trigger.
 */
struct on_auth_trigger_ctx {
	/** Authenticated user name. Not null-terminated! */
	const char *user_name;
	/** Length of the user_name string. */
	size_t user_name_len;
	/* true if authentication was successful */
	bool is_authenticated;
};

/**
 * Abstract authenticator class.
 *
 * A concrete instance of this class is created for each user that
 * enabled authentication.
 */
struct authenticator {
	/** Authentication method used by this authenticator. */
	const struct auth_method *method;
};

/**
 * Abstract authentication method class.
 *
 * A concrete instance of this class is created for each supported
 * authentication method.
 */
struct auth_method {
	/** Unique authentication method name. */
	const char *name;
	/** Destroys an authentication method object. */
	void
	(*auth_method_delete)(struct auth_method *method);
	/**
	 * Given a password, this function prepares MsgPack data that can be
	 * used to check an authentication request. The data is allocated on
	 * the fiber region.
	 *
	 * We store the data as a value in the 'auth' field of the '_user'
	 * system table, using the authentication method name as a key.
	 */
	void
	(*auth_data_prepare)(const struct auth_method *method,
			     const char *password, int password_len,
			     const char **auth_data,
			     const char **auth_data_end);
	/**
	 * Given a password and a connection salt (sent in the greeting),
	 * this function prepares MsgPack data that can be used to perform
	 * an authentication request. The data is allocated on the fiber
	 * region.
	 *
	 * We store the data in the second field of IPROTO_TUPLE sent in
	 * IPROTO_AUTH request body. The first IPROTO_TUPLE field is set to
	 * the authentication method name.
	 */
	void
	(*auth_request_prepare)(const struct auth_method *method,
				const char *password, int password_len,
				const char *salt,
				const char **auth_request,
				const char **auth_request_end);
	/**
	 * Checks the format of an authentication request.
	 *
	 * Returns 0 on success. If the request is malformed, sets diag to
	 * ER_INVALID_AUTH_REQUEST and returns -1.
	 *
	 * See also auth_request_prepare.
	 */
	int
	(*auth_request_check)(const struct auth_method *method,
			      const char *auth_request,
			      const char *auth_request_end);
	/**
	 * Constructs a new authenticator object from an authentication data.
	 *
	 * Returns a pointer on success. If the data is malformed, sets diag to
	 * ER_INVALID_AUTH_DATA and returns NULL.
	 *
	 * See also auth_data_prepare.
	 */
	struct authenticator *
	(*authenticator_new)(const struct auth_method *method,
			     const char *auth_data, const char *auth_data_end);
	/** Destroys an authenticator object. */
	void
	(*authenticator_delete)(struct authenticator *auth);
	/**
	 * Authenticates a request.
	 *
	 * Returns true if authentication was successful.
	 *
	 * The request must be well-formed.
	 * The salt must match the salt used to prepare the request.
	 *
	 * See also auth_request_prepare, auth_request_check.
	 */
	bool
	(*authenticate_request)(const struct authenticator *auth,
				const char *salt,
				const char *auth_request,
				const char *auth_request_end);
};

static inline void
auth_data_prepare(const struct auth_method *method,
		  const char *password, int password_len,
		  const char **auth_data, const char **auth_data_end)
{
	method->auth_data_prepare(method, password, password_len,
				 auth_data, auth_data_end);
}

static inline void
auth_request_prepare(const struct auth_method *method,
		     const char *password, int password_len, const char *salt,
		     const char **auth_request, const char **auth_request_end)
{
	method->auth_request_prepare(method, password, password_len, salt,
				     auth_request, auth_request_end);
}

static inline int
auth_request_check(const struct auth_method *method,
		   const char *auth_request, const char *auth_request_end)
{
	return method->auth_request_check(method, auth_request,
					  auth_request_end);
}

static inline struct authenticator *
authenticator_new(const struct auth_method *method,
		  const char *auth_data, const char *auth_data_end)
{
	return method->authenticator_new(method, auth_data, auth_data_end);
}

static inline void
authenticator_delete(struct authenticator *auth)
{
	auth->method->authenticator_delete(auth);
}

/**
 * Authenticates a request.
 *
 * Returns true if authentication was successful.
 *
 * NOTE: the request must be well-formed (checked by auth_request_check).
 */
static inline bool
authenticate_request(const struct authenticator *auth, const char *salt,
		     const char *auth_request, const char *auth_request_end)
{
	assert(auth->method->auth_request_check(auth->method, auth_request,
						auth_request_end) == 0);
	return auth->method->authenticate_request(
			auth, salt, auth_request, auth_request_end);
}

/**
 * Authenticates a password.
 *
 * Returns true if authentication was successful.
 *
 * This is a helper function that prepares an authentication request with
 * auth_request_prepare and then checks it with authenticate_request using
 * zero salt.
 */
bool
authenticate_password(const struct authenticator *auth,
		      const char *password, int password_len);

/**
 * Authenticates a user.
 *
 * Takes the following arguments:
 * user_name: user name string, not necessarily null-terminated.
 * user_name_len: length of the user name string.
 * salt: random salt sent in the greeting message.
 * tuple: value of the IPROTO_TUPLE key sent in the IPROTO_AUTH request body.
 *
 * Returns 0 on success. On error, sets diag and returns -1.
 *
 * Errors:
 *   ER_INVALID_MSGPACK: missing authentication method name or data
 *   ER_UNKNOWN_AUTH_METHOD: unknown authentication method name
 *   ER_INVALID_AUTH_REQUEST: malformed authentication request
 *   ER_CREDS_MISMATCH: authentication denied
 */
int
authenticate(const char *user_name, uint32_t user_name_len,
	     const char *salt, const char *tuple);

/**
 * Looks up an authentication method by name.
 * If not found, returns NULL (diag NOT set).
 */
const struct auth_method *
auth_method_by_name(const char *name, uint32_t name_len);

/**
 * Registers an authentication method.
 * There must not be another method with the same name.
 */
void
auth_method_register(struct auth_method *method);

void
auth_init(void);

void
auth_free(void);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */
