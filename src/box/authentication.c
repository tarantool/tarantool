/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2022, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "authentication.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "assoc.h"
#include "auth_chap_sha1.h"
#include "base64.h"
#include "diag.h"
#include "errcode.h"
#include "error.h"
#include "fiber.h"
#include "iostream.h"
#include "msgpuck.h"
#include "security.h"
#include "session.h"
#include "small/region.h"
#include "tt_static.h"
#include "user.h"
#include "user_def.h"

const struct auth_method *AUTH_METHOD_DEFAULT;

/** Map of all registered authentication methods: name -> auth_method. */
static struct mh_strnptr_t *auth_methods = NULL;

bool
authenticate_password(const struct authenticator *auth,
		      const char *password, int password_len)
{
	/*
	 * We don't really need to zero the salt here, because any salt would
	 * do as long as we use the same salt in auth_request_prepare and
	 * authenticate_request. We zero it solely to avoid address sanitizer
	 * complaints about usage of uninitialized memory.
	 */
	const char salt[AUTH_SALT_SIZE] = {0};
	struct region *region = &fiber()->gc;
	size_t region_svp = region_used(region);
	const char *auth_request, *auth_request_end;
	auth_request_prepare(auth->method, password, password_len, salt,
			     &auth_request, &auth_request_end);
	bool ret = authenticate_request(auth, salt, auth_request,
					auth_request_end);
	region_truncate(region, region_svp);
	return ret;
}

int
authenticate(const char *user_name, uint32_t user_name_len,
	     const char *salt, const char *tuple)
{
	struct on_auth_trigger_ctx auth_res = {
		.user_name = user_name,
		.user_name_len = user_name_len,
		.is_authenticated = true,
	};
	struct user *user = user_find_by_name(user_name, user_name_len);
	if (user == NULL && diag_get()->last->code != ER_NO_SUCH_USER)
		return -1;
	/*
	 * Check the request body as usual even if the user doesn't exist
	 * to prevent user enumeration by analyzing error codes.
	 */
	diag_clear(diag_get());
	/*
	 * Allow authenticating back to the guest user without a password,
	 * because the guest user isn't allowed to have a password, anyway.
	 * This is useful for connection pooling.
	 */
	uint32_t part_count = mp_decode_array(&tuple);
	if (part_count == 0 && user != NULL && user->def->uid == GUEST)
		goto ok;
	/* Expected: authentication method name and data. */
	if (part_count < 2) {
		diag_set(ClientError, ER_INVALID_MSGPACK,
			 "authentication request body");
		return -1;
	}
	if (mp_typeof(*tuple) != MP_STR) {
		diag_set(ClientError, ER_INVALID_MSGPACK,
			 "authentication request body");
		return -1;
	}
	uint32_t method_name_len;
	const char *method_name = mp_decode_str(&tuple, &method_name_len);
	const struct auth_method *method = auth_method_by_name(
					method_name, method_name_len);
	if (method == NULL) {
		diag_set(ClientError, ER_UNKNOWN_AUTH_METHOD,
			 tt_cstr(method_name, method_name_len));
		return -1;
	}
	const char *auth_request = tuple;
	const char *auth_request_end = tuple;
	mp_next(&auth_request_end);
	if (auth_request_check(method, auth_request, auth_request_end) != 0)
		return -1;
	if (security_check_auth_pre(user_name, user_name_len) != 0)
		return -1;
	if (user == NULL || user->def->auth == NULL ||
	    user->def->auth->method != method ||
	    !authenticate_request(user->def->auth, salt,
				  auth_request, auth_request_end)) {
		auth_res.is_authenticated = false;
		if (session_run_on_auth_triggers(&auth_res) != 0)
			return -1;
		diag_set(ClientError, ER_CREDS_MISMATCH);
		return -1;
	}
	if (security_check_auth_post(user) != 0)
		return -1;
	if (access_check_session(user) != 0)
		return -1;
ok:
	if (session_run_on_auth_triggers(&auth_res) != 0)
		return -1;
	credentials_reset(&current_session()->credentials, user);
	return 0;
}

int
auth_method_check_io(const struct auth_method *method,
		     const struct iostream *io)
{
	if ((method->flags & AUTH_METHOD_REQUIRES_ENCRYPTION) != 0 &&
	    (io->flags & IOSTREAM_IS_ENCRYPTED) == 0) {
		diag_set(ClientError, ER_UNSUPPORTED,
			 tt_sprintf("Authentication method '%s'", method->name),
			 "unencrypted connection");
		return -1;
	}
	return 0;
}

const struct auth_method *
auth_method_by_name(const char *name, uint32_t name_len)
{
	struct mh_strnptr_t *h = auth_methods;
	mh_int_t i = mh_strnptr_find_str(h, name, name_len);
	return i != mh_end(h) ? mh_strnptr_node(h, i)->val : NULL;
}

void
auth_method_register(struct auth_method *method)
{
	struct mh_strnptr_t *h = auth_methods;
	const char *name = method->name;
	uint32_t name_len = strlen(name);
	uint32_t name_hash = mh_strn_hash(name, name_len);
	struct mh_strnptr_node_t n = {name, name_len, name_hash, method};
	struct mh_strnptr_node_t prev;
	struct mh_strnptr_node_t *prev_ptr = &prev;
	mh_strnptr_put(h, &n, &prev_ptr, NULL);
	assert(prev_ptr == NULL);
}

void
auth_init(void)
{
	auth_methods = mh_strnptr_new();
	struct auth_method *method = auth_chap_sha1_new();
	AUTH_METHOD_DEFAULT = method;
	auth_method_register(method);
}

void
auth_free(void)
{
	struct mh_strnptr_t *h = auth_methods;
	auth_methods = NULL;
	AUTH_METHOD_DEFAULT = NULL;
	mh_int_t i;
	mh_foreach(h, i) {
		struct auth_method *method = mh_strnptr_node(h, i)->val;
		method->auth_method_delete(method);
	}
	mh_strnptr_delete(h);
}
