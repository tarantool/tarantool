/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2026, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "auth_pap_sha256.h"

#include <assert.h>
#include <openssl/evp.h>
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
#include "random.h"
#include "say.h"
#include "small/region.h"
#include "trivia/util.h"

/**
 * PAP-SHA256:
 *
 * 1. Authentication data stored in the '_user' system space:
 *
 *      MP_ARRAY([MP_STR(salt), MP_STR(hash)])
 *
 *    'salt' is a random salt generated on each password update
 *    'hash' is sha256(salt + password)
 *
 * 2. Authentication request data sent in the IPROTO_TUPLE field
 *    of an IPROTO_AUTH request:
 *
 *      MP_ARRAY([MP_STR('pap-sha256'), MP_STR(password)])
 *
 *    'password' is the challenged password
 *
 * Notes:
 *
 *  - The password is sent over network without any hashing so this
 *    authentication method is safe to use only if the channel is
 *    encrypted.
 *
 *  - Applying a random salt to the password before hashing and
 *    storing it in the database renders rainbow tables inefficient.
 */

#define AUTH_PAP_SHA256_NAME "pap-sha256"

enum {
	/**
	 * Size of random salt stored in the '_user' space.
	 *
	 * Note, it has nothing to do with AUTH_SALT_SIZE, which is
	 * the size of salt sent in a greeting message. The latter
	 * isn't used by this authentication method.
	 */
	PASSWORD_SALT_SIZE = 20,
	/** PASSWORD_SALT_SIZE when encoded in base64. */
	PASSWORD_SALT_BASE64_SIZE = 28,
	/** Size of the password hash stored in the '_user' space. */
	PASSWORD_HASH_SIZE = 32,
	/** PASSWORD_HASH_SIZE when encoded in base64. */
	PASSWORD_HASH_BASE64_SIZE = 44
};

/** pap-sha256 authenticator implementation. */
struct auth_pap_sha256_authenticator {
	/** Base class. */
	struct authenticator base;
	/** Random salt. */
	char password_salt[PASSWORD_SALT_SIZE];
	/** sha256(salt + password). */
	char password_hash[PASSWORD_HASH_SIZE];
};

/** Computes sha256(salt + password). */
static void
password_prepare(const char *password, int password_len,
		 const char *password_salt, char *out)
{
	const EVP_MD *md = EVP_get_digestbyname("SHA256");
	if (md == NULL)
		goto fail;
	EVP_MD_CTX *ctx = EVP_MD_CTX_new();
	if (ctx == NULL)
		goto fail;
	unsigned int size;
	if (EVP_DigestInit_ex(ctx, md, NULL) != 1 ||
	    EVP_DigestUpdate(ctx, password_salt, PASSWORD_SALT_SIZE) != 1 ||
	    EVP_DigestUpdate(ctx, password, password_len) != 1 ||
	    EVP_DigestFinal_ex(ctx, (unsigned char *)out, &size) != 1)
		goto fail;
	assert(size == PASSWORD_HASH_SIZE);
	(void)size;
	EVP_MD_CTX_free(ctx);
	return;
fail:
	panic("Unexpected EVP error");
}

/** auth_method::auth_method_delete */
static void
auth_pap_sha256_delete(struct auth_method *method)
{
	TRASH(method);
	free(method);
}

/** auth_method::auth_data_prepare */
static void
auth_pap_sha256_data_prepare(const struct auth_method *method,
			     const char *password, int password_len,
			     const char **auth_data,
			     const char **auth_data_end)
{
	(void)method;
	struct region *region = &fiber()->gc;
	size_t size = mp_sizeof_array(2) +
		      mp_sizeof_str(PASSWORD_SALT_BASE64_SIZE) +
		      mp_sizeof_str(PASSWORD_HASH_BASE64_SIZE);
	char *p = xregion_alloc(region, size);
	*auth_data = p;
	*auth_data_end = p + size;
	p = mp_encode_array(p, 2);
	p = mp_encode_strl(p, PASSWORD_SALT_BASE64_SIZE);
	char password_salt[PASSWORD_SALT_SIZE];
	random_bytes(password_salt, PASSWORD_SALT_SIZE);
	int len = base64_encode(password_salt, PASSWORD_SALT_SIZE,
				p, PASSWORD_SALT_BASE64_SIZE, /*options=*/0);
	assert(len == PASSWORD_SALT_BASE64_SIZE);
	p += len;
	p = mp_encode_strl(p, PASSWORD_HASH_BASE64_SIZE);
	char password_hash[PASSWORD_HASH_SIZE];
	password_prepare(password, password_len, password_salt, password_hash);
	len = base64_encode(password_hash, PASSWORD_HASH_SIZE,
			    p, PASSWORD_HASH_BASE64_SIZE, /*options=*/0);
	assert(len == PASSWORD_HASH_BASE64_SIZE);
	p += len;
	assert(p == *auth_data_end);
	(void)p;
}

/** auth_method::auth_request_prepare */
static void
auth_pap_sha256_request_prepare(const struct auth_method *method,
				const char *password, int password_len,
				const char *salt,
				const char **auth_request,
				const char **auth_request_end)
{
	(void)method;
	(void)salt;
	struct region *region = &fiber()->gc;
	size_t size = mp_sizeof_str(password_len);
	char *p = xregion_alloc(region, size);
	*auth_request = p;
	*auth_request_end = p + size;
	p = mp_encode_str(p, password, password_len);
	assert(p == *auth_request_end);
	(void)p;
}

/** auth_method::auth_request_check */
static int
auth_pap_sha256_request_check(const struct auth_method *method,
			      const char *auth_request,
			      const char *auth_request_end)
{
	(void)method;
	if (mp_typeof(*auth_request) != MP_STR) {
		diag_set(ClientError, ER_INVALID_AUTH_REQUEST,
			 AUTH_PAP_SHA256_NAME, "expected password string");
		return -1;
	}
#ifndef NDEBUG
	mp_next(&auth_request);
	assert(auth_request == auth_request_end);
#endif
	(void)auth_request_end;
	return 0;
}

/** auth_method::authenticator_new */
static struct authenticator *
auth_pap_sha256_authenticator_new(const struct auth_method *method,
				  const char *auth_data,
				  const char *auth_data_end)
{
	struct auth_pap_sha256_authenticator *auth = xcalloc(1, sizeof(*auth));
	auth->base.method = method;
	if (mp_typeof(*auth_data) != MP_ARRAY ||
	    mp_decode_array(&auth_data) != 2) {
		diag_set(ClientError, ER_INVALID_AUTH_DATA,
			 AUTH_PAP_SHA256_NAME,
			 "expected array with password salt and hash");
		goto fail;
	}
	if (mp_typeof(*auth_data) != MP_STR) {
		diag_set(ClientError, ER_INVALID_AUTH_DATA,
			 AUTH_PAP_SHA256_NAME, "password salt must be string");
		goto fail;
	}
	uint32_t len;
	const char *s = mp_decode_str(&auth_data, &len);
	if (base64_decode(s, len, auth->password_salt,
			  PASSWORD_SALT_SIZE) != PASSWORD_SALT_SIZE) {
		diag_set(ClientError, ER_INVALID_AUTH_DATA,
			 AUTH_PAP_SHA256_NAME, "invalid password salt size");
		goto fail;
	}
	if (mp_typeof(*auth_data) != MP_STR) {
		diag_set(ClientError, ER_INVALID_AUTH_DATA,
			 AUTH_PAP_SHA256_NAME, "password hash must be string");
		goto fail;
	}
	s = mp_decode_str(&auth_data, &len);
	if (base64_decode(s, len, auth->password_hash,
			  PASSWORD_HASH_SIZE) != PASSWORD_HASH_SIZE) {
		diag_set(ClientError, ER_INVALID_AUTH_DATA,
			 AUTH_PAP_SHA256_NAME, "invalid password hash size");
		goto fail;
	}
	assert(auth_data == auth_data_end);
	(void)auth_data_end;
	return (struct authenticator *)auth;
fail:
	free(auth);
	return NULL;
}

/** auth_method::authenticator_delete */
static void
auth_pap_sha256_authenticator_delete(struct authenticator *auth_)
{
	struct auth_pap_sha256_authenticator *auth =
		(struct auth_pap_sha256_authenticator *)auth_;
	TRASH(auth);
	free(auth);
}

/** auth_method::authenticator_check_request */
static bool
auth_pap_sha256_authenticate_request(const struct authenticator *auth_,
				     const char *salt,
				     const char *auth_request,
				     const char *auth_request_end)
{
	(void)salt;
	const struct auth_pap_sha256_authenticator *auth =
		(const struct auth_pap_sha256_authenticator *)auth_;
	assert(mp_typeof(*auth_request) == MP_STR);
	uint32_t password_len;
	const char *password = mp_decode_str(&auth_request, &password_len);
	assert(auth_request == auth_request_end);
	(void)auth_request_end;
	char candidate_hash[PASSWORD_HASH_SIZE];
	password_prepare(password, password_len, auth->password_salt,
			 candidate_hash);
	return memcmp(candidate_hash, auth->password_hash,
		      PASSWORD_HASH_SIZE) == 0;
}

struct auth_method *
auth_pap_sha256_new(void)
{
	struct auth_method *method = xcalloc(1, sizeof(*method));
	method->name = AUTH_PAP_SHA256_NAME;
	method->flags = AUTH_METHOD_REQUIRES_ENCRYPTION;
	method->auth_method_delete = auth_pap_sha256_delete;
	method->auth_data_prepare = auth_pap_sha256_data_prepare;
	method->auth_request_prepare = auth_pap_sha256_request_prepare;
	method->auth_request_check = auth_pap_sha256_request_check;
	method->authenticator_new = auth_pap_sha256_authenticator_new;
	method->authenticator_delete = auth_pap_sha256_authenticator_delete;
	method->authenticate_request = auth_pap_sha256_authenticate_request;
	return method;
}
