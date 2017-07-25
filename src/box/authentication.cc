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
#include "iproto_constants.h"

static char zero_hash[SCRAMBLE_SIZE];

void
auth_request_decode_xc(struct auth_request *request, uint64_t sync,
		       const char *data, uint32_t len)
{
	const char *end = data + len;
	uint32_t map_size;
	if (mp_typeof(*data) != MP_MAP || mp_check_map(data, end) > 0)
		tnt_raise(ClientError, ER_INVALID_MSGPACK, "packet body");

	map_size = mp_decode_map(&data);
	request->user_name = NULL;
	request->scramble = NULL;
	request->sync = sync;
	for (uint32_t i = 0; i < map_size; ++i) {
		uint8_t key = *data;
		if (key != IPROTO_USER_NAME && key != IPROTO_TUPLE) {
			/* Skip key + value. */
			mp_check(&data, end);
			mp_check(&data, end);
			continue;
		}
		const char *value = ++data;
		if (mp_check(&data, end) != 0) {
			tnt_raise(ClientError, ER_INVALID_MSGPACK,
				  "packet body");
		}
		if (key == IPROTO_USER_NAME)
			request->user_name = value;
		else
			request->scramble = value;
	}
	if (request->user_name == NULL) {
		tnt_raise(ClientError, ER_MISSING_REQUEST_FIELD,
			  iproto_key_name(IPROTO_USER_NAME));
	}
	if (request->scramble == NULL) {
		tnt_raise(ClientError, ER_MISSING_REQUEST_FIELD,
			  iproto_key_name(IPROTO_TUPLE));
	}
	if (data != end)
		tnt_raise(ClientError, ER_INVALID_MSGPACK, "packet body");
}

void
authenticate(const char *user_name, uint32_t len,
	     const char *tuple)
{
	struct user *user = user_find_by_name_xc(user_name, len);
	struct session *session = current_session();
	uint32_t part_count;
	uint32_t scramble_len;
	const char *scramble;
	/*
	 * Allow authenticating back to GUEST user without
	 * checking a password. This is useful for connection
	 * pooling.
	 */
	part_count = mp_decode_array(&tuple);
	if (part_count == 0 && user->def->uid == GUEST &&
	    memcmp(user->def->hash2, zero_hash, SCRAMBLE_SIZE) == 0) {
		/* No password is set for GUEST, OK. */
		goto ok;
	}

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

	if (scramble_check(scramble, session->salt, user->def->hash2))
		tnt_raise(ClientError, ER_PASSWORD_MISMATCH, user->def->name);

	/* check and run auth triggers on success */
	if (! rlist_empty(&session_on_auth) &&
	    session_run_on_auth_triggers(user->def->name) != 0)
		diag_raise();
ok:
	credentials_init(&session->credentials, user->auth_token,
			 user->def->uid);
}
