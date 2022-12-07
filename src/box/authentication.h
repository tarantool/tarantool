#ifndef INCLUDES_TARANTOOL_BOX_AUTHENTICATION_H
#define INCLUDES_TARANTOOL_BOX_AUTHENTICATION_H
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

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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
 * Authenticates a user.
 *
 * Takes the following arguments:
 * user_name: user name string, not necessarily null-terminated.
 * user_name_len: length of the user name string.
 * salt: random salt sent in the greeting message.
 * tuple: value of the IPROTO_TUPLE key sent in the IPROTO_AUTH request body.
 *
 * Raises an exception on error.
 */
void
authenticate(const char *user_name, uint32_t user_name_len,
	     const char *salt, const char *tuple);

#endif /* INCLUDES_TARANTOOL_BOX_AUTHENTICATION_H */
