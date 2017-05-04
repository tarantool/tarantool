#ifndef TARANTOOL_XROW_H_INCLUDED
#define TARANTOOL_XROW_H_INCLUDED
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
#include <stddef.h>
#include <sys/uio.h> /* struct iovec */

#include "tt_uuid.h"
#include "diag.h"

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
	uint32_t replica_id;
	uint64_t sync;
	int64_t lsn; /* LSN must be signed for correct comparison */
	double tm;

	int bodycnt;
	uint32_t schema_version;
	struct iovec body[XROW_BODY_IOVMAX];

};

/**
 * Encode xrow into a binary packet
 *
 * @param header xrow
 * @param[out] out iovec to store encoded packet
 * @param fixheader_len the number of bytes to reserve for fixheader
 *
 * @retval > 0 the number of iovector components used (<= XROW_IOVMAX)
 * @retval -1 on error (check diag)
 *
 * @pre out iovec must have space at least for XROW_IOVMAX members
 * @post retval <= XROW_IOVMAX
 */
int
xrow_header_encode(const struct xrow_header *header,
		   struct iovec *out, size_t fixheader_len);

/**
 * Decode xrow from a binary packet
 *
 * @param header[out] xrow to fill
 * @param pos[inout] the start of a packet
 * @param end the end of a packet
 *
 * @retval 0 on success
 * @retval -1 on error (check diag)
 * @post *pos == end on success
 */
int
xrow_header_decode(struct xrow_header *header,
		   const char **pos, const char *end);

struct request
{
	/*
	 * Either log row, or network header, or NULL, depending
	 * on where this packet originated from: the write ahead
	 * log/snapshot, client request, or a Lua request.
	 */
	struct xrow_header *header;
	/**
	 * Request type - IPROTO type code
	 */
	uint32_t type;
	uint32_t space_id;
	uint32_t index_id;
	uint32_t offset;
	uint32_t limit;
	uint32_t iterator;
	/** Search key or proc name. */
	const char *key;
	const char *key_end;
	/** Insert/replace/upsert tuple or proc argument or update operations. */
	const char *tuple;
	const char *tuple_end;
	/** Upsert operations. */
	const char *ops;
	const char *ops_end;
	/** Base field offset for UPDATE/UPSERT, e.g. 0 for C and 1 for Lua. */
	int index_base;
};

/**
 * Initialize a request for @a code
 * @param request request
 * @param code see `enum iproto_type`
 */
void
request_create(struct request *request, uint32_t code);

/**
 * Decode @a data buffer
 * @param request request to fill up
 * @param data a buffer
 * @param len a buffer size
 * @param key_map a bit map of keys that are required by the caller,
 *        @sa request_key_map().
 * @retval 0 on success
 * @retval -1 on error, see diag
 */
int
request_decode(struct request *request, const char *data, uint32_t len,
	       uint64_t key_map);

/**
 * Encode the request fields to iovec using region_alloc().
 * @param request request to encode
 * @param iov[out] iovec to fill
 * @retval -1 on error, see diag
 * @retval > 0 the number of iovecs used
 */
int
request_encode(struct request *request, struct iovec *iov);

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
 * \brief Format a text greeting sent by the instance during handshake.
 * This function encodes greeting for binary protocol (adds "(Binary)"
 * after version signature).
 *
 * \param[out] greetingbuf buffer to store result. Exactly
 * IPROTO_GREETING_SIZE bytes will be written.
 * \param version_id instance version_id created by version_id()
 * \param uuid instance UUID
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
 * \brief Parse a text greeting send by the instance during handshake.
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

/**
 * @copydoc xrow_header_decode()
 */
static inline void
xrow_header_decode_xc(struct xrow_header *header, const char **pos,
		      const char *end)
{
	if (xrow_header_decode(header, pos, end) < 0)
		diag_raise();
}

void
xrow_decode_uuid(const char **pos, struct tt_uuid *out);

char *
xrow_encode_uuid(char *pos, const struct tt_uuid *in);

int
xrow_to_iovec(const struct xrow_header *row, struct iovec *out);

/**
 * \brief Decode ERROR and re-throw it as ClientError exception
 * \param row
*/
void
xrow_decode_error(struct xrow_header *row);

/**
 * @copydoc xrow_header_encode()
 */
static inline int
xrow_header_encode_xc(const struct xrow_header *header,
		      struct iovec *out, size_t fixheader_len)
{
	int iovcnt = xrow_header_encode(header, out, fixheader_len);
	if (iovcnt < 0)
		diag_raise();
	return iovcnt;
}

static inline void
request_decode_xc(struct request *request, const char *data, uint32_t len,
		  uint64_t key_map)
{
	if (request_decode(request, data, len, key_map) < 0)
		diag_raise();
}

static inline int
request_encode_xc(struct request *request, struct iovec *iov)
{
	int iovcnt = request_encode(request, iov);
	if (iovcnt < 0)
		diag_raise();
	return iovcnt;
}

struct request *
xrow_decode_request(struct xrow_header *row);

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
 * \param replicaset_uuid replica set uuid
 * \param instance_uuid instance uuid
 * \param vclock replication clock
*/
void
xrow_encode_subscribe(struct xrow_header *row,
		      const struct tt_uuid *replicaset_uuid,
		      const struct tt_uuid *instance_uuid,
		      const struct vclock *vclock);

/**
 * \brief Decode SUBSCRIBE command
 * \param row
 * \param[out] replicaset_uuid
 * \param[out] instance_uuid
 * \param[out] vclock
*/
void
xrow_decode_subscribe(struct xrow_header *row, struct tt_uuid *replicaset_uuid,
		      struct tt_uuid *instance_uuid, struct vclock *vclock);

/**
 * \brief Encode JOIN command
 * \param[out] row
 * \param instance_uuid
*/
void
xrow_encode_join(struct xrow_header *row, const struct tt_uuid *instance_uuid);

/**
 * \brief Decode JOIN command
 * \param row
 * \param[out] instance_uuid
*/
static inline void
xrow_decode_join(struct xrow_header *row, struct tt_uuid *instance_uuid)
{
	return xrow_decode_subscribe(row, NULL, instance_uuid, NULL);
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
