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
#include "authentication.h"
#include "user.h"
#include "session.h"
#include "msgpuck.h"
#include "error.h"
#include <base64.h>

void
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
		diag_raise();
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
		tnt_raise(ClientError, ER_INVALID_MSGPACK,
			   "authentication request body");
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
		tnt_raise(ClientError, ER_INVALID_MSGPACK,
			   "authentication scramble");
	}
	if (scramble_len != SCRAMBLE_SIZE) {
		/* Authentication mechanism, data. */
		tnt_raise(ClientError, ER_INVALID_MSGPACK,
			   "invalid scramble size");
	}
	if (user == NULL ||
	    scramble_check(scramble, salt, user->def->hash2) != 0) {
		auth_res.is_authenticated = false;
		if (session_run_on_auth_triggers(&auth_res) != 0)
			diag_raise();
		tnt_raise(ClientError, ER_CREDS_MISMATCH);
	}
	access_check_session_xc(user);
ok:
	/* check and run auth triggers on success */
	if (! rlist_empty(&session_on_auth) &&
	    session_run_on_auth_triggers(&auth_res) != 0)
		diag_raise();
	credentials_reset(&current_session()->credentials, user);
}
