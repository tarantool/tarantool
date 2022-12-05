/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2022, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "authentication.h"

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "base64.h"
#include "diag.h"
#include "errcode.h"
#include "error.h"
#include "msgpuck.h"
#include "scramble.h"
#include "session.h"
#include "user.h"
#include "user_def.h"

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
	uint32_t part_count;
	uint32_t scramble_len;
	const char *scramble;
	/*
	 * Allow authenticating back to the guest user without a password,
	 * because the guest user isn't allowed to have a password, anyway.
	 * This is useful for connection pooling.
	 */
	part_count = mp_decode_array(&tuple);
	if (part_count == 0 && user != NULL && user->def->uid == GUEST)
		goto ok;

	if (part_count < 2) {
		/* Expected at least: authentication mechanism and data. */
		diag_set(ClientError, ER_INVALID_MSGPACK,
			 "authentication request body");
		return -1;
	}
	mp_next(&tuple); /* Skip authentication mechanism. */
	if (mp_typeof(*tuple) == MP_STR) {
		scramble = mp_decode_str(&tuple, &scramble_len);
	} else if (mp_typeof(*tuple) == MP_BIN) {
		/*
		 * scramble is not a character stream, so some
		 * codecs automatically pack it as MP_BIN
		 */
		scramble = mp_decode_bin(&tuple, &scramble_len);
	} else {
		diag_set(ClientError, ER_INVALID_MSGPACK,
			 "authentication scramble");
		return -1;
	}
	if (scramble_len != SCRAMBLE_SIZE) {
		/* Authentication mechanism, data. */
		diag_set(ClientError, ER_INVALID_MSGPACK,
			 "invalid scramble size");
		return -1;
	}
	if (user == NULL ||
	    scramble_check(scramble, salt, user->def->hash2) != 0) {
		auth_res.is_authenticated = false;
		if (session_run_on_auth_triggers(&auth_res) != 0)
			return -1;
		diag_set(ClientError, ER_CREDS_MISMATCH);
		return -1;
	}
	if (access_check_session(user) != 0)
		return -1;
ok:
	/* check and run auth triggers on success */
	if (! rlist_empty(&session_on_auth) &&
	    session_run_on_auth_triggers(&auth_res) != 0)
		return -1;
	credentials_reset(&current_session()->credentials, user);
	return 0;
}
