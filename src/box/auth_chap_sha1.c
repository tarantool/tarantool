/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2022, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "auth_chap_sha1.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "authentication.h"
#include "base64.h"
#include "diag.h"
#include "errcode.h"
#include "error.h"
#include "fiber.h"
#include "msgpuck.h"
#include "scramble.h"
#include "small/region.h"
#include "trivia/util.h"

#define AUTH_CHAP_SHA1_NAME "chap-sha1"

/** chap-sha1 authenticator implementation. */
struct auth_chap_sha1_authenticator {
	/** Base class. */
	struct authenticator base;
	/** sha1(sha1(password)). */
	char hash2[SCRAMBLE_SIZE];
};

/** auth_method::auth_method_delete */
static void
auth_chap_sha1_delete(struct auth_method *method)
{
	TRASH(method);
	free(method);
}

/** auth_method::auth_data_prepare */
static void
auth_chap_sha1_data_prepare(const struct auth_method *method,
			    const char *password, int password_len,
			    const char **auth_data,
			    const char **auth_data_end)
{
	(void)method;
	struct region *region = &fiber()->gc;
	size_t size = mp_sizeof_str(SCRAMBLE_BASE64_SIZE);
	char *p = xregion_alloc(region, size);
	*auth_data = p;
	*auth_data_end = p + size;
	p = mp_encode_strl(p, SCRAMBLE_BASE64_SIZE);
	password_prepare(password, password_len, p, SCRAMBLE_BASE64_SIZE);
}

/** auth_method::auth_request_prepare */
static void
auth_chap_sha1_request_prepare(const struct auth_method *method,
			       const char *password, int password_len,
			       const char *salt,
			       const char **auth_request,
			       const char **auth_request_end)
{
	(void)method;
	struct region *region = &fiber()->gc;
	size_t size = mp_sizeof_str(SCRAMBLE_SIZE);
	char *p = xregion_alloc(region, size);
	*auth_request = p;
	*auth_request_end = p + size;
	p = mp_encode_strl(p, SCRAMBLE_SIZE);
	scramble_prepare(p, salt, password, password_len);
}

/** auth_method::auth_request_check */
static int
auth_chap_sha1_request_check(const struct auth_method *method,
			     const char *auth_request,
			     const char *auth_request_end)
{
	(void)method;
	uint32_t scramble_len;
	if (mp_typeof(*auth_request) == MP_STR) {
		scramble_len = mp_decode_strl(&auth_request);
	} else if (mp_typeof(*auth_request) == MP_BIN) {
		/*
		 * Scramble is not a character stream, so some codecs
		 * automatically pack it as MP_BIN.
		 */
		scramble_len = mp_decode_binl(&auth_request);
	} else {
		diag_set(ClientError, ER_INVALID_AUTH_REQUEST,
			 AUTH_CHAP_SHA1_NAME, "scramble must be string");
		return -1;
	}
	assert(auth_request + scramble_len == auth_request_end);
	(void)auth_request_end;
	if (scramble_len != SCRAMBLE_SIZE) {
		diag_set(ClientError, ER_INVALID_AUTH_REQUEST,
			 AUTH_CHAP_SHA1_NAME, "invalid scramble size");
		return -1;
	}
	return 0;
}

/** auth_method::authenticator_new */
static struct authenticator *
auth_chap_sha1_authenticator_new(const struct auth_method *method,
				 const char *auth_data,
				 const char *auth_data_end)
{
	if (mp_typeof(*auth_data) != MP_STR) {
		diag_set(ClientError, ER_INVALID_AUTH_DATA,
			 AUTH_CHAP_SHA1_NAME, "scramble must be string");
		return NULL;
	}
	uint32_t hash2_base64_len;
	const char *hash2_base64 = mp_decode_str(&auth_data,
						 &hash2_base64_len);
	assert(auth_data == auth_data_end);
	(void)auth_data_end;
	if (hash2_base64_len != SCRAMBLE_BASE64_SIZE) {
		diag_set(ClientError, ER_INVALID_AUTH_DATA,
			 AUTH_CHAP_SHA1_NAME, "invalid scramble size");
		return NULL;
	}
	struct auth_chap_sha1_authenticator *auth = xmalloc(sizeof(*auth));
	auth->base.method = method;
	int hash2_len = base64_decode(hash2_base64, hash2_base64_len,
				      auth->hash2, sizeof(auth->hash2));
	assert(hash2_len == sizeof(auth->hash2));
	(void)hash2_len;
	return (struct authenticator *)auth;
}

/** auth_method::authenticator_delete */
static void
auth_chap_sha1_authenticator_delete(struct authenticator *auth_)
{
	struct auth_chap_sha1_authenticator *auth =
		(struct auth_chap_sha1_authenticator *)auth_;
	TRASH(auth);
	free(auth);
}

/** auth_method::authenticator_check_request */
static bool
auth_chap_sha1_authenticate_request(const struct authenticator *auth_,
				    const char *salt,
				    const char *auth_request,
				    const char *auth_request_end)
{
	const struct auth_chap_sha1_authenticator *auth =
		(const struct auth_chap_sha1_authenticator *)auth_;
	uint32_t scramble_len;
	const char *scramble;
	if (mp_typeof(*auth_request) == MP_STR) {
		scramble = mp_decode_str(&auth_request, &scramble_len);
	} else if (mp_typeof(*auth_request) == MP_BIN) {
		scramble = mp_decode_bin(&auth_request, &scramble_len);
	} else {
		unreachable();
	}
	assert(auth_request == auth_request_end);
	(void)auth_request_end;
	assert(scramble_len == SCRAMBLE_SIZE);
	(void)scramble_len;
	return scramble_check(scramble, salt, auth->hash2) == 0;
}

struct auth_method *
auth_chap_sha1_new(void)
{
	struct auth_method *method = xmalloc(sizeof(*method));
	method->name = AUTH_CHAP_SHA1_NAME;
	method->auth_method_delete = auth_chap_sha1_delete;
	method->auth_data_prepare = auth_chap_sha1_data_prepare;
	method->auth_request_prepare = auth_chap_sha1_request_prepare;
	method->auth_request_check = auth_chap_sha1_request_check;
	method->authenticator_new = auth_chap_sha1_authenticator_new;
	method->authenticator_delete = auth_chap_sha1_authenticator_delete;
	method->authenticate_request = auth_chap_sha1_authenticate_request;
	return method;
}
