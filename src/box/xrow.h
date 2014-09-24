#ifndef TARANTOOL_XROW_H_INCLUDED
#define TARANTOOL_XROW_H_INCLUDED
/*
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

#if defined(__cplusplus)
extern "C" {
#endif

enum {
	IPROTO_PACKET_HEAD_IOVMAX = 1,
	IPROTO_PACKET_BODY_IOVMAX = 2,
	IPROTO_PACKET_IOVMAX = IPROTO_PACKET_HEAD_IOVMAX +
		IPROTO_PACKET_BODY_IOVMAX
};

enum { IPROTO_ROW_IOVMAX = IPROTO_PACKET_IOVMAX + 1 };


struct iproto_header {
	uint32_t type;
	uint32_t server_id;
	uint64_t sync;
	uint64_t lsn;
	double tm;

	int bodycnt;
	struct iovec body[IPROTO_PACKET_BODY_IOVMAX];
};

void
iproto_header_decode(struct iproto_header *header,
		     const char **pos, const char *end);
struct tt_uuid;

void
iproto_decode_uuid(const char **pos, struct tt_uuid *out);

char *
iproto_encode_uuid(char *pos, const struct tt_uuid *in);

int
iproto_header_encode(const struct iproto_header *header,
		     struct iovec *out);

int
iproto_row_encode(const struct iproto_header *row, struct iovec *out);

/**
 * \brief Decode ERROR and re-throw it as ClientError exception
 * \param row
 */
void
iproto_decode_error(struct iproto_header *row);

/**
 * \brief Encode AUTH command
 * \param[out] row
 * \param greeting - IPROTO greeting
 * \param login - user login
 * \param password - user password
 */
void
iproto_encode_auth(struct iproto_header *row, const char *greeting,
		   const char *login, const char *password);

/**
 * \brief Encode SUBSCRIBE command
 * \param row[out]
 * \param cluster_uuid cluster uuid
 * \param server_uuid server uuid
 * \param vclock server vclock
 */
void
iproto_encode_subscribe(struct iproto_header *row,
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
iproto_decode_subscribe(struct iproto_header *row, struct tt_uuid *cluster_uuid,
			struct tt_uuid *server_uuid, struct vclock *vclock);

/**
 * \brief Encode JOIN command
 * \param[out] row
 * \param server_uuid
 */
void
iproto_encode_join(struct iproto_header *row, const struct tt_uuid *server_uuid);

/**
 * \brief Decode JOIN command
 * \param row
 * \param[out] server_uuid
 */
static inline void
iproto_decode_join(struct iproto_header *row, struct tt_uuid *server_uuid)
{
	return iproto_decode_subscribe(row, NULL, server_uuid, NULL);
}

/**
 * \brief Encode end of stream command (a response to JOIN command)
 * \param row[out]
 * \param vclock
 */
void
iproto_encode_eos(struct iproto_header *row, const struct vclock *vclock);

/**
 * \brief Decode end of stream command (a response to JOIN command)
 * \param row
 * \param[out] vclock
 */
static inline void
iproto_decode_eos(struct iproto_header *row, struct vclock *vclock)
{
	return iproto_decode_subscribe(row, NULL, NULL, vclock);
}

#if defined(__cplusplus)
} /* extern "C" */
#endif

#endif /* TARANTOOL_XROW_H_INCLUDED */
