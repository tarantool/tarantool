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

struct vclock;

enum {
	XROW_HEADER_IOVMAX = 1,
	XROW_BODY_IOVMAX = 2,
	XROW_IOVMAX = XROW_HEADER_IOVMAX + XROW_BODY_IOVMAX,
	XROW_HEADER_LEN_MAX = 40,
	XROW_BODY_LEN_MAX = 128,
	IPROTO_HEADER_LEN = 28,
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
xrow_header_encode(const struct xrow_header *header, uint64_t sync,
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

/**
 * DML request.
 */
struct request {
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
	/** Search key. */
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
 * Create a JSON-like string representation of a request. */
const char *
request_str(const struct request *request);

/**
 * Decode DML request from a given MessagePack map.
 * @param row request header.
 * @param[out] request DML request to decode to.
 * @param key_map a bit map of keys that are required by the caller,
 *        @sa request_key_map().
 * @retval 0 on success
 * @retval -1 on error
 */
int
xrow_decode_dml(struct xrow_header *xrow, struct request *request,
		uint64_t key_map);

/**
 * Encode the request fields to iovec using region_alloc().
 * @param request request to encode
 * @param iov[out] iovec to fill
 * @retval -1 on error, see diag
 * @retval > 0 the number of iovecs used
 */
int
xrow_encode_dml(const struct request *request, struct iovec *iov);

/**
 * CALL/EVAL request.
 */
struct call_request {
	/** Request header */
	const struct xrow_header *header;
	/** Function name for CALL request. MessagePack String. */
	const char *name;
	/** Expression for EVAL request. MessagePack String. */
	const char *expr;
	/** CALL/EVAL parameters. MessagePack Array. */
	const char *args;
	const char *args_end;
};

/**
 * Decode CALL/EVAL request from a given MessagePack map.
 * @param[out] call_request Request to decode to.
 * @param type Request type - either CALL or CALL_16 or EVAL.
 * @param sync Request sync.
 * @param data Request MessagePack encoded body.
 * @param len @data length.
 */
int
xrow_decode_call(const struct xrow_header *row, struct call_request *request);

/**
 * AUTH request
 */
struct auth_request {
	/** MessagePack encoded name of the user to authenticate. */
	const char *user_name;
	/** Auth scramble. @sa scramble.h */
	const char *scramble;
};

/**
 * Decode AUTH request from MessagePack.
 * @param row request header.
 * @param[out] request Request to decode.
 * @retval  0 on success
 * @retval -1 on error
 */
int
xrow_decode_auth(const struct xrow_header *row, struct auth_request *request);

/**
 * Encode AUTH command.
 * @param[out] Row.
 * @param salt Salt from IPROTO greeting.
 * @param salt_len Length of @salt.
 * @param login User login.
 * @param login_len Length of @login.
 * @param password User password.
 * @param password_len Length of @password.
 *
 * @retval  0 Success.
 * @retval -1 Memory error.
*/
int
xrow_encode_auth(struct xrow_header *row, const char *salt, size_t salt_len,
		 const char *login, size_t login_len, const char *password,
		 size_t password_len);

/**
 * Encode a vote request for master election.
 * @param row[out] Row to encode into.
 */
void
xrow_encode_request_vote(struct xrow_header *row);

/**
 * Encode SUBSCRIBE command.
 * @param[out] Row.
 * @param replicaset_uuid Replica set uuid.
 * @param instance_uuid Instance uuid.
 * @param vclock Replication clock.
 *
 * @retval  0 Success.
 * @retval -1 Memory error.
 */
int
xrow_encode_subscribe(struct xrow_header *row,
		      const struct tt_uuid *replicaset_uuid,
		      const struct tt_uuid *instance_uuid,
		      const struct vclock *vclock);

/**
 * Decode SUBSCRIBE command.
 * @param row Row to decode.
 * @param[out] replicaset_uuid.
 * @param[out] instance_uuid.
 * @param[out] vclock.
 * @param[out] version_id.
 * @param[out] read_only.
 *
 * @retval  0 Success.
 * @retval -1 Memory or format error.
 */
int
xrow_decode_subscribe(struct xrow_header *row, struct tt_uuid *replicaset_uuid,
		      struct tt_uuid *instance_uuid, struct vclock *vclock,
		      uint32_t *version_id, bool *read_only);

/**
 * Encode JOIN command.
 * @param[out] row Row to encode into.
 * @param instance_uuid.
 *
 * @retval  0 Success.
 * @retval -1 Memory error.
 */
int
xrow_encode_join(struct xrow_header *row, const struct tt_uuid *instance_uuid);

/**
 * Decode JOIN command.
 * @param row Row to decode.
 * @param[out] instance_uuid.
 *
 * @retval  0 Success.
 * @retval -1 Memory or format error.
 */
static inline int
xrow_decode_join(struct xrow_header *row, struct tt_uuid *instance_uuid)
{
	return xrow_decode_subscribe(row, NULL, instance_uuid, NULL, NULL,
				     NULL);
}

/**
 * Encode end of stream command (a response to JOIN command).
 * @param row[out] Row to encode into.
 * @param vclock.
 *
 * @retval  0 Success.
 * @retval -1 Memory error.
 */
int
xrow_encode_vclock(struct xrow_header *row, const struct vclock *vclock);

/**
 * Decode end of stream command (a response to JOIN command).
 * @param row Row to decode.
 * @param[out] vclock.
 *
 * @retval  0 Success.
 * @retval -1 Memory or format error.
 */
static inline int
xrow_decode_vclock(struct xrow_header *row, struct vclock *vclock)
{
	return xrow_decode_subscribe(row, NULL, NULL, vclock, NULL, NULL);
}

/**
 * Decode peer vclock and access rights (a response to VOTE command).
 * @param row Row to decode.
 * @param[out] vclock.
 * @param[out] read_only.
 *
 * @retval  0 Success.
 * @retval -1 Memory or format error.
 */
static inline int
xrow_decode_request_vote(struct xrow_header *row, struct vclock *vclock,
			 bool *read_only)
{
	return xrow_decode_subscribe(row, NULL, NULL, vclock, NULL, read_only);
}

/**
 * Encode a heartbeat message.
 * @param row[out] Row to encode into.
 * @param replica_id Instance id.
 * @param tm Time stamp.
 */
void
xrow_encode_timestamp(struct xrow_header *row, uint32_t replica_id, double tm);

/**
 * Fast encode xrow header using the specified header fields.
 * It is faster than the xrow_header_encode, because uses
 * the predefined values for all fields of the header, defined
 * in the struct iproto_header_bin in iproto_port.cc. Because of
 * it, the implementation is placed in the same
 * file: iproto_port.cc.
 *
 * @param out Previously allocated memory of at least
 *        IPROTO_HEADER_LEN bytes.
 * @param type IPROTO_OK or iproto error code.
 * @param sync Sync of the response. Must be the same as the
 *        request sync.
 * @param schema_version Schema version.
 * @param body_length Length of the body of the iproto message.
 *        Please, pass it without IPROTO_HEADER_LEN.
 * @see xrow_header_encode()
 */
void
iproto_header_encode(char *data, uint32_t type, uint64_t sync,
		     uint32_t schema_version, uint32_t body_length);

struct obuf;
struct obuf_svp;

int
iproto_prepare_select(struct obuf *buf, struct obuf_svp *svp);

/**
 * Write select header to a preallocated buffer.
 * This function doesn't throw (and we rely on this in iproto.cc).
 */
void
iproto_reply_select(struct obuf *buf, struct obuf_svp *svp, uint64_t sync,
		    uint32_t schema_version, uint32_t count);

/**
 * Encode iproto header with IPROTO_OK response code.
 * @param out Encode to.
 * @param sync Request sync.
 * @param schema_version.
 *
 * @retval  0 Success.
 * @retval -1 Memory error.
 */
int
iproto_reply_ok(struct obuf *out, uint64_t sync, uint32_t schema_version);

/**
 * Encode iproto header with IPROTO_OK response code
 * and vclock in the body.
 * @param out Encode to.
 * @param sync Request sync.
 * @param schema_version.
 * @param vclock.
 * @param read_only.
 *
 * @retval  0 Success.
 * @retval -1 Memory error.
 */
int
iproto_reply_request_vote(struct obuf *out, uint64_t sync,
			 uint32_t schema_version, const struct vclock *vclock,
			 bool read_only);

/**
 * Write an error packet int output buffer. Doesn't throw if out
 * of memory
 */
int
iproto_reply_error(struct obuf *out, const struct error *e, uint64_t sync,
		   uint32_t schema_version);

/**
 * Write an IPROTO_CHUNK header from a specified position in a
 * buffer.
 * @param buf Buffer to write to.
 * @param svp Position to write from.
 * @param sync Request sync.
 * @param schema_version Actual schema version.
 */
void
iproto_reply_chunk(struct obuf *buf, struct obuf_svp *svp, uint64_t sync,
		   uint32_t schema_version);

/** Write error directly to a socket. */
void
iproto_write_error(int fd, const struct error *e, uint32_t schema_version,
		   uint64_t sync);

enum {
	/* Maximal length of protocol name in handshake */
	GREETING_PROTOCOL_LEN_MAX = 32,
	/* Maximal length of salt in handshake */
	GREETING_SALT_LEN_MAX = 44,
};

/**
 * The server sends a greeting into a newly established socket,
 * regardless of the socket protocol. This allows the connected
 * client identify the protocol, server version and instance uuid.
 * The greeting also contains a random salt which can be
 * used to encode a password.
 */
struct greeting {
	/** Peer version id. */
	uint32_t version_id;
	uint32_t salt_len;
	/** Peer protocol - Binary or Console */
	char protocol[GREETING_PROTOCOL_LEN_MAX + 1];
	/** Peer instance uuid */
	struct tt_uuid uuid;
	/** Random salt. */
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
greeting_encode(char *greetingbuf, uint32_t version_id,
		const struct tt_uuid *uuid, const char *salt,
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

/**
 * Encode an xrow record into the specified iovec.
 *
 * @param row Record to encode.
 * @param[out] out Encoded record.
 *
 * @retval >= 0 Used iovector components.
 * @retval   -1 Error.
 */
int
xrow_to_iovec(const struct xrow_header *row, struct iovec *out);

/**
 * Decode ERROR and set it to diagnostics area.
 * @param row Encoded error.
 */
void
xrow_decode_error(struct xrow_header *row);

#if defined(__cplusplus)
} /* extern "C" */

/** @copydoc xrow_header_decode. */
static inline void
xrow_header_decode_xc(struct xrow_header *header, const char **pos,
		      const char *end)
{
	if (xrow_header_decode(header, pos, end) < 0)
		diag_raise();
}

/** @copydoc xrow_to_iovec. */
static inline int
xrow_to_iovec_xc(const struct xrow_header *row, struct iovec *out)
{
	int rc = xrow_to_iovec(row, out);
	if (rc < 0)
		diag_raise();
	return rc;
}

/** @copydoc xrow_decode_error. */
static inline void
xrow_decode_error_xc(struct xrow_header *row)
{
	xrow_decode_error(row);
	diag_raise();
}

/** @copydoc xrow_decode_dml. */
static inline void
xrow_decode_dml_xc(struct xrow_header *row, struct request *request,
		   uint64_t key_map)
{
	if (xrow_decode_dml(row, request, key_map) != 0)
		diag_raise();
}

/** @copydoc xrow_encode_dml. */
static inline int
xrow_encode_dml_xc(const struct request *request, struct iovec *iov)
{
	int iovcnt = xrow_encode_dml(request, iov);
	if (iovcnt < 0)
		diag_raise();
	return iovcnt;
}

/** @copydoc xrow_decode_call. */
static inline void
xrow_decode_call_xc(const struct xrow_header *row,
		    struct call_request *request)
{
	if (xrow_decode_call(row, request) != 0)
		diag_raise();
}

/** @copydoc xrow_decode_auth. */
static inline void
xrow_decode_auth_xc(const struct xrow_header *row,
		    struct auth_request *request)
{
	if (xrow_decode_auth(row, request) != 0)
		diag_raise();
}

/** @copydoc xrow_encode_auth. */
static inline void
xrow_encode_auth_xc(struct xrow_header *row, const char *salt, size_t salt_len,
		    const char *login, size_t login_len, const char *password,
		    size_t password_len)
{
	if (xrow_encode_auth(row, salt, salt_len, login, login_len, password,
			     password_len) != 0)
		diag_raise();
}

/** @copydoc xrow_encode_subscribe. */
static inline void
xrow_encode_subscribe_xc(struct xrow_header *row,
			 const struct tt_uuid *replicaset_uuid,
			 const struct tt_uuid *instance_uuid,
			 const struct vclock *vclock)
{
	if (xrow_encode_subscribe(row, replicaset_uuid, instance_uuid,
				  vclock) != 0)
		diag_raise();
}

/** @copydoc xrow_decode_subscribe. */
static inline void
xrow_decode_subscribe_xc(struct xrow_header *row,
			 struct tt_uuid *replicaset_uuid,
		         struct tt_uuid *instance_uuid, struct vclock *vclock,
			 uint32_t *replica_version_id)
{
	if (xrow_decode_subscribe(row, replicaset_uuid, instance_uuid,
				  vclock, replica_version_id, NULL) != 0)
		diag_raise();
}

/** @copydoc xrow_encode_join. */
static inline void
xrow_encode_join_xc(struct xrow_header *row,
		    const struct tt_uuid *instance_uuid)
{
	if (xrow_encode_join(row, instance_uuid) != 0)
		diag_raise();
}

/** @copydoc xrow_decode_join. */
static inline void
xrow_decode_join_xc(struct xrow_header *row, struct tt_uuid *instance_uuid)
{
	if (xrow_decode_join(row, instance_uuid) != 0)
		diag_raise();
}

/** @copydoc xrow_encode_vclock. */
static inline void
xrow_encode_vclock_xc(struct xrow_header *row, const struct vclock *vclock)
{
	if (xrow_encode_vclock(row, vclock) != 0)
		diag_raise();
}

/** @copydoc xrow_decode_vclock. */
static inline void
xrow_decode_vclock_xc(struct xrow_header *row, struct vclock *vclock)
{
	if (xrow_decode_vclock(row, vclock) != 0)
		diag_raise();
}

/** @copydoc xrow_decode_request_vote. */
static inline void
xrow_decode_request_vote_xc(struct xrow_header *row, struct vclock *vclock,
			    bool *read_only)
{
	if (xrow_decode_request_vote(row, vclock, read_only) != 0)
		diag_raise();
}

/** @copydoc iproto_reply_ok. */
static inline void
iproto_reply_ok_xc(struct obuf *out, uint64_t sync, uint32_t schema_version)
{
	if (iproto_reply_ok(out, sync, schema_version) != 0)
		diag_raise();
}

/** @copydoc iproto_reply_request_vote_xc. */
static inline void
iproto_reply_request_vote_xc(struct obuf *out, uint64_t sync,
			     uint32_t schema_version,
			     const struct vclock *vclock, bool read_only)
{
	if (iproto_reply_request_vote(out, sync, schema_version,
				      vclock, read_only) != 0)
		diag_raise();
}

#endif

#endif /* TARANTOOL_XROW_H_INCLUDED */
