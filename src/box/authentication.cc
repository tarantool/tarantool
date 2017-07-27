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
#include "xrow.h"

static char zero_hash[SCRAMBLE_SIZE];

int
xrow_decode_auth(const struct xrow_header *row, struct auth_request *request)
{
	if (row->bodycnt == 0) {
		diag_set(ClientError, ER_INVALID_MSGPACK,
			 "missing request body");
		return 1;
	}

	assert(row->bodycnt == 1);
	const char *data = (const char *) row->body[0].iov_base;
	const char *end = data + row->body[0].iov_len;
	assert((end - data) > 0);

	if (mp_typeof(*data) != MP_MAP || mp_check_map(data, end) > 0) {
error:
		diag_set(ClientError, ER_INVALID_MSGPACK, "packet body");
		return 1;
	}

	memset(request, 0, sizeof(*request));
	request->header = row;

	uint32_t map_size = mp_decode_map(&data);
	for (uint32_t i = 0; i < map_size; ++i) {
		if ((end - data) < 1 || mp_typeof(*data) != MP_UINT)
			goto error;

		uint64_t key = mp_decode_uint(&data);
		const char *value = data;
		if (mp_check(&data, end) != 0)
			goto error;

		switch (key) {
		case IPROTO_USER_NAME:
			if (mp_typeof(*value) != MP_STR)
				goto error;
			request->user_name = value;
			break;
		case IPROTO_TUPLE:
			if (mp_typeof(*value) != MP_ARRAY)
				goto error;
			request->scramble = value;
			break;
		default:
			continue; /* unknown key */
		}
	}
	if (data != end) {
		diag_set(ClientError, ER_INVALID_MSGPACK, "packet end");
		return 1;
	}
	if (request->user_name == NULL) {
		diag_set(ClientError, ER_MISSING_REQUEST_FIELD,
			  iproto_key_name(IPROTO_USER_NAME));
		return 1;
	}
	if (request->scramble == NULL) {
		diag_set(ClientError, ER_MISSING_REQUEST_FIELD,
			 iproto_key_name(IPROTO_TUPLE));
		return 1;
	}
	return 0;
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
