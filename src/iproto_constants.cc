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
#include "iproto_constants.h"
#include "msgpuck/msgpuck.h"
#include "exception.h"
#include "fiber.h"
#include "crc32.h"

const unsigned char iproto_key_type[IPROTO_KEY_MAX] =
{
	/* {{{ header */
		/* 0x00 */	MP_UINT,   /* IPROTO_CODE */
		/* 0x01 */	MP_UINT,   /* IPROTO_SYNC */
		/* 0x02 */	MP_UINT,   /* IPROTO_SERVER_ID */
		/* 0x03 */	MP_UINT,   /* IPROTO_LSN */
		/* 0x04 */	MP_DOUBLE, /* IPROTO_TIMESTAMP */
	/* }}} */

	/* {{{ unused */
		/* 0x05 */	MP_UINT,
		/* 0x06 */	MP_UINT,
		/* 0x07 */	MP_UINT,
		/* 0x08 */	MP_UINT,
		/* 0x09 */	MP_UINT,
		/* 0x0a */	MP_UINT,
		/* 0x0b */	MP_UINT,
		/* 0x0c */	MP_UINT,
		/* 0x0d */	MP_UINT,
		/* 0x0e */	MP_UINT,
		/* 0x0f */	MP_UINT,
	/* }}} */

	/* {{{ body -- integer keys */
		/* 0x10 */	MP_UINT, /* IPROTO_SPACE_ID */
		/* 0x11 */	MP_UINT, /* IPROTO_INDEX_ID */
		/* 0x12 */	MP_UINT, /* IPROTO_LIMIT */
		/* 0x13 */	MP_UINT, /* IPROTO_OFFSET */
		/* 0x14 */	MP_UINT, /* IPROTO_ITERATOR */
	/* }}} */

	/* {{{ unused */
		/* 0x15 */	MP_UINT,
		/* 0x16 */	MP_UINT,
		/* 0x17 */	MP_UINT,
		/* 0x18 */	MP_UINT,
		/* 0x19 */	MP_UINT,
		/* 0x1a */	MP_UINT,
		/* 0x1b */	MP_UINT,
		/* 0x1c */	MP_UINT,
		/* 0x1d */	MP_UINT,
		/* 0x1e */	MP_UINT,
		/* 0x1f */	MP_UINT,
	/* }}} */

	/* {{{ body -- all keys */
	/* 0x20 */	MP_ARRAY, /* IPROTO_KEY */
	/* 0x21 */	MP_ARRAY, /* IPROTO_TUPLE */
	/* 0x22 */	MP_STR, /* IPROTO_FUNCTION_NAME */
	/* 0x23 */	MP_STR, /* IPROTO_USER_NAME */
	/* }}} */
};

const char *iproto_request_type_strs[] =
{
	NULL,
	"SELECT",
	"INSERT",
	"REPLACE",
	"UPDATE",
	"DELETE",
	"CALL"
};

void
iproto_packet_decode(struct iproto_packet *packet, const char **pos,
		     const char *end)
{
	memset(packet, 0, sizeof(*packet));
	const char *pos2 = *pos;
	if (mp_check(&pos2, end) != 0) {
error:
		tnt_raise(ClientError, ER_INVALID_MSGPACK, "packet header");
	}

	if (mp_typeof(**pos) != MP_MAP)
		goto error;

	uint32_t size = mp_decode_map(pos);
	for (uint32_t i = 0; i < size; i++) {
		if (mp_typeof(**pos) != MP_UINT)
			goto error;
		unsigned char key = mp_decode_uint(pos);
		if (iproto_key_type[key] != mp_typeof(**pos))
			goto error;
		switch (key) {
		case IPROTO_CODE:
			packet->code = mp_decode_uint(pos);
			break;
		case IPROTO_SYNC:
			packet->sync = mp_decode_uint(pos);
			break;
		case IPROTO_LSN:
			packet->lsn = mp_decode_uint(pos);
			break;
		case IPROTO_TIMESTAMP:
			packet->tm = mp_decode_double(pos);
			break;
		default:
			/* unknown header */
			mp_next(pos);
		}
	}

	assert(*pos <= end);
	if (*pos < end) {
		packet->bodycnt = 1;
		packet->body[0].iov_base = (void *) *pos;
		packet->body[0].iov_len = (end - *pos);
		*pos = end;
	}
}

int
iproto_packet_encode(const struct iproto_packet *packet, struct iovec *iov)
{
	enum { HEADER_LEN_MAX = 40 };

	/* allocate memory for sign + header */
	char *data = (char *) region_alloc(&fiber()->gc, HEADER_LEN_MAX);

	/* Header */
	char *d = data + 1; /* Skip 1 byte for MP_MAP */
	int map_size = 0;
	if (true) {
		d = mp_encode_uint(d, IPROTO_CODE);
		d = mp_encode_uint(d, packet->code);
		map_size++;
	}

	if (packet->sync) {
		d = mp_encode_uint(d, IPROTO_SYNC);
		d = mp_encode_uint(d, packet->sync);
		map_size++;
	}

	if (packet->lsn) {
		d = mp_encode_uint(d, IPROTO_LSN);
		d = mp_encode_uint(d, packet->lsn);
		map_size++;
	}

	if (packet->tm) {
		d = mp_encode_uint(d, IPROTO_TIMESTAMP);
		d = mp_encode_double(d, packet->tm);
		map_size++;
	}

	assert(d <= data + HEADER_LEN_MAX);
	mp_encode_map(data, map_size);
	iov->iov_base = data;
	iov->iov_len = (d - data);
	iov++;

	memcpy(iov, packet->body, sizeof(*iov) * packet->bodycnt);
	assert(1 + packet->bodycnt <= IPROTO_PACKET_IOVMAX);
	return 1 + packet->bodycnt; /* new iovcnt */
}
