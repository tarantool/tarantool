#ifndef TARANTOOL_XROW_H_INCLUDED
#define TARANTOOL_XROW_H_INCLUDED
/*
 * Copyright 2010-2015, Tarantool AUTHORS, please see AUTHORS file.
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
#include <stddef.h>
#include <sys/uio.h> /* struct iovec */

#include "tt_uuid.h"

#if defined(__cplusplus)
extern "C" {
#endif

enum {
	XROW_HEADER_IOVMAX = 1,
	XROW_BODY_IOVMAX = 2,
	XROW_IOVMAX = XROW_HEADER_IOVMAX + XROW_BODY_IOVMAX,
};

struct xrow_header {
	/* (!) Please update txn_add_redo() after changing members */

	uint32_t type;
	uint32_t server_id;
	uint64_t sync;
	uint64_t lsn;
	double tm;

	int bodycnt;
	uint32_t schema_id;
	struct iovec body[XROW_BODY_IOVMAX];

};

enum {
	/* Maximal length of protocol name in handshake */
	GREETING_PROTOCOL_LEN_MAX = 32,
	/* Maximal length of salt in handshake */
	GREETING_SALT_LEN_MAX = 44,
};

struct greeting {
	uint32_t version_id;
	uint32_t salt_len;
	char protocol[GREETING_PROTOCOL_LEN_MAX + 1];
	struct tt_uuid uuid;
	char salt[GREETING_SALT_LEN_MAX];
};

/**
 * \brief Format a text greeting sent by the server during handshake.
 * This function encodes greeting for binary protocol (adds "(Binary)"
 * after version signature).
 *
 * \param[out] greetingbuf buffer to store result. Exactly
 * IPROTO_GREETING_SIZE bytes will be written.
 * \param version_id server version_id created by version_id()
 * \param uuid server UUID
 * \param salt random bytes that client should use to sign passwords.
 * \param salt_len size of \a salt. Up to GREETING_SALT_LEN_MAX bytes.
 *
 * \sa greeting_decode()
 */

void
greeting_encode(char *greetingbuf, uint32_t version_id, const
		struct tt_uuid *uuid, const char *salt,
		uint32_t salt_len);

/**
 * \brief Parse a text greeting send by the server during handshake.
 * This function supports both binary and console protocol.
 *
 * \param greetingbuf a text greeting
 * \param[out] greeting parsed struct greeting.
 * \retval 0 on success
 * \retval -1 on failure due to mailformed greeting
 *
 * \sa greeting_encode()
 */
int
greeting_decode(const char *greetingbuf, struct greeting *greeting);


#if defined(__cplusplus)
} /* extern "C" */

void
xrow_header_decode(struct xrow_header *header,
		   const char **pos, const char *end);

void
xrow_decode_uuid(const char **pos, struct tt_uuid *out);

char *
xrow_encode_uuid(char *pos, const struct tt_uuid *in);

int
xrow_header_encode(const struct xrow_header *header,
		   struct iovec *out, size_t fixheader_len);

int
xrow_to_iovec(const struct xrow_header *row, struct iovec *out);

/**
 * \brief Decode ERROR and re-throw it as ClientError exception
 * \param row
*/
void
xrow_decode_error(struct xrow_header *row);

/**
 * \brief Encode AUTH command
 * \param[out] row
 * \param salt - salt from IPROTO greeting
 * \param salt_len length of \a salt
 * \param login - user login
 * \param login_len - length of \a login
 * \param password - user password
 * \param password_len - length of \a password
*/
void
xrow_encode_auth(struct xrow_header *row, const char *salt, size_t salt_len,
		 const char *login, size_t login_len,
		 const char *password, size_t password_len);

/**
 * \brief Encode SUBSCRIBE command
 * \param row[out]
 * \param cluster_uuid cluster uuid
 * \param server_uuid server uuid
 * \param vclock server vclock
*/
void
xrow_encode_subscribe(struct xrow_header *row,
		      const struct tt_uuid *cluster_uuid,
		      const struct tt_uuid *server_uuid,
		      const struct vclock *vclock);

/**
 * \brief Decode SUBSCRIBE command
 * \param row
 * \param[out] cluster_uuid
 * \param[out] server_uuid
 * \param[out] vclock
*/
void
xrow_decode_subscribe(struct xrow_header *row, struct tt_uuid *cluster_uuid,
		      struct tt_uuid *server_uuid, struct vclock *vclock);

/**
 * \brief Encode JOIN command
 * \param[out] row
 * \param server_uuid
*/
void
xrow_encode_join(struct xrow_header *row, const struct tt_uuid *server_uuid);

/**
 * \brief Decode JOIN command
 * \param row
 * \param[out] server_uuid
*/
static inline void
xrow_decode_join(struct xrow_header *row, struct tt_uuid *server_uuid)
{
	return xrow_decode_subscribe(row, NULL, server_uuid, NULL);
}

/**
 * \brief Encode end of stream command (a response to JOIN command)
 * \param row[out]
 * \param vclock
*/
void
xrow_encode_vclock(struct xrow_header *row, const struct vclock *vclock);

/**
 * \brief Decode end of stream command (a response to JOIN command)
 * \param row
 * \param[out] vclock
*/
static inline void
xrow_decode_vclock(struct xrow_header *row, struct vclock *vclock)
{
	return xrow_decode_subscribe(row, NULL, NULL, vclock);
}

#endif

#endif /* TARANTOOL_XROW_H_INCLUDED */
