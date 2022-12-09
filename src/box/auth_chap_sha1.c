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
#include <string.h>

#include "authentication.h"
#include "base64.h"
#include "diag.h"
#include "errcode.h"
#include "error.h"
#include "fiber.h"
#include "msgpuck.h"
#include "sha1.h"
#include "small/region.h"
#include "trivia/util.h"

/**
 * These are the core bits of the built-in Tarantool
 * authentication. They implement the same algorithm as
 * in MySQL 4.1 authentication:
 *
 * SERVER:  seed = create_random_string()
 *          send(seed)
 *
 * CLIENT:  recv(seed)
 *          hash1 = sha1("password")
 *          hash2 = sha1(hash1)
 *          reply = xor(hash1, sha1(seed, hash2))
 *
 *          ^^ these steps are done in scramble_prepare()
 *
 *          send(reply)
 *
 *
 * SERVER:  recv(reply)
 *
 *          hash1 = xor(reply, sha1(seed, hash2))
 *          candidate_hash2 = sha1(hash1)
 *          check(candidate_hash2 == hash2)
 *
 *          ^^ these steps are done in scramble_check()
 */

enum { SCRAMBLE_SIZE = 20, SCRAMBLE_BASE64_SIZE = 28 };

static_assert((int)SCRAMBLE_SIZE <= (int)AUTH_SALT_SIZE,
	      "SCRAMBLE_SIZE must be less than or equal to AUTH_SALT_SIZE");

#define AUTH_CHAP_SHA1_NAME "chap-sha1"

/** chap-sha1 authenticator implementation. */
struct auth_chap_sha1_authenticator {
	/** Base class. */
	struct authenticator base;
	/** sha1(sha1(password)). */
	char hash2[SCRAMBLE_SIZE];
};

static void
xor(unsigned char *to, unsigned const char *left,
    unsigned const char *right, uint32_t len)
{
	const uint8_t *end = to + len;
	while (to < end)
		*to++ = *left++ ^ *right++;
}

/**
 * Prepare a scramble (cipher) to send over the wire
 * to the server for authentication.
 */
static void
scramble_prepare(void *out, const void *salt, const void *password,
		 int password_len)
{
	unsigned char hash1[SCRAMBLE_SIZE];
	unsigned char hash2[SCRAMBLE_SIZE];
	SHA1_CTX ctx;

	SHA1Init(&ctx);
	SHA1Update(&ctx, password, password_len);
	SHA1Final(hash1, &ctx);

	SHA1Init(&ctx);
	SHA1Update(&ctx, hash1, SCRAMBLE_SIZE);
	SHA1Final(hash2, &ctx);

	SHA1Init(&ctx);
	SHA1Update(&ctx, salt, SCRAMBLE_SIZE);
	SHA1Update(&ctx, hash2, SCRAMBLE_SIZE);
	SHA1Final(out, &ctx);

	xor(out, hash1, out, SCRAMBLE_SIZE);
}

/**
 * Verify a password.
 *
 * @retval 0  passwords do match
 * @retval !0 passwords do not match
 */
static int
scramble_check(const void *scramble, const void *salt, const void *hash2)
{
	SHA1_CTX ctx;
	unsigned char candidate_hash2[SCRAMBLE_SIZE];

	SHA1Init(&ctx);
	SHA1Update(&ctx, salt, SCRAMBLE_SIZE);
	SHA1Update(&ctx, hash2, SCRAMBLE_SIZE);
	SHA1Final(candidate_hash2, &ctx);

	xor(candidate_hash2, candidate_hash2, scramble, SCRAMBLE_SIZE);
	/*
	 * candidate_hash2 now supposedly contains hash1, turn it
	 * into hash2
	 */
	SHA1Init(&ctx);
	SHA1Update(&ctx, candidate_hash2, SCRAMBLE_SIZE);
	SHA1Final(candidate_hash2, &ctx);

	return memcmp(hash2, candidate_hash2, SCRAMBLE_SIZE);
}

/**
 * Prepare a password hash as is stored in the _user space.
 * @pre out must be at least SCRAMBLE_BASE64_SIZE
 * @post out contains base64_encode(sha1(sha1(password)), 0)
 */
static void
password_prepare(const char *password, int len, char *out, int out_len)
{
	unsigned char hash2[SCRAMBLE_SIZE];
	SHA1_CTX ctx;

	SHA1Init(&ctx);
	SHA1Update(&ctx, (const unsigned char *)password, len);
	SHA1Final(hash2, &ctx);

	SHA1Init(&ctx);
	SHA1Update(&ctx, hash2, SCRAMBLE_SIZE);
	SHA1Final(hash2, &ctx);

	base64_encode((char *)hash2, SCRAMBLE_SIZE, out, out_len, 0);
}

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
