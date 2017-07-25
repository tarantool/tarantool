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

#include <stdint.h>

/** Auth request details. */
struct auth_request {
	/**
	 * MessagePack encoded name of the user to
	 * authenticate.
	 */
	const char *user_name;
	/** Auth scramble. @sa scramble.h */
	const char *scramble;
	/** Request sync. */
	uint64_t sync;
};

/**
 * Decode auth request from MessagePack.
 * @param[out] request Request to decode to.
 * @param sync Request sync.
 * @param data Request MessagePack body.
 * @param len @data length.
 */
void
auth_request_decode_xc(struct auth_request *request, uint64_t sync,
		       const char *data, uint32_t len);

void
authenticate(const char *user_name, uint32_t len, const char *tuple);

#endif /* INCLUDES_TARANTOOL_BOX_AUTHENTICATION_H */
