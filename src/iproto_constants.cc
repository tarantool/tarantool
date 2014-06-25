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
#include "tt_uuid.h"
#include "box/vclock.h"

const unsigned char iproto_key_type[IPROTO_KEY_MAX] =
{
	/* {{{ header */
		/* 0x00 */	MP_UINT,   /* IPROTO_REQUEST_TYPE */
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
	/* 0x24 */	MP_STR, /* IPROTO_SERVER_UUID */
	/* 0x25 */	MP_STR, /* IPROTO_CLUSTER_UUID */
	/* 0x26 */	MP_MAP, /* IPROTO_VCLOCK */
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
	"CALL",
	"AUTH"
};

#define bit(c) (1ULL<<IPROTO_##c)
const uint64_t iproto_body_key_map[IPROTO_DML_REQUEST_MAX + 1] = {
	0,                                                     /* unused */
	bit(SPACE_ID) | bit(LIMIT) | bit(KEY),                 /* SELECT */
	bit(SPACE_ID) | bit(TUPLE),                            /* INSERT */
	bit(SPACE_ID) | bit(TUPLE),                            /* REPLACE */
	bit(SPACE_ID) | bit(KEY) | bit(TUPLE),                 /* UPDATE */
	bit(SPACE_ID) | bit(KEY),                              /* DELETE */
	bit(FUNCTION_NAME) | bit(TUPLE),                       /* CALL */
	bit(USER_NAME) | bit(TUPLE)                            /* AUTH */
};
#undef bit

const char *iproto_key_strs[IPROTO_KEY_MAX] = {
	"type",             /* 0x00 */
	"sync",             /* 0x01 */
	"server_id",          /* 0x02 */
	"lsn",              /* 0x03 */
	"timestamp",        /* 0x04 */
	"",                 /* 0x05 */
	"",                 /* 0x06 */
	"",                 /* 0x07 */
	"",                 /* 0x08 */
	"",                 /* 0x09 */
	"",                 /* 0x0a */
	"",                 /* 0x0b */
	"",                 /* 0x0c */
	"",                 /* 0x0d */
	"",                 /* 0x0e */
	"",                 /* 0x0f */
	"space_id",         /* 0x10 */
	"index_id",         /* 0x11 */
	"limit",            /* 0x12 */
	"offset",           /* 0x13 */
	"iterator",         /* 0x14 */
	"",                 /* 0x15 */
	"",                 /* 0x16 */
	"",                 /* 0x17 */
	"",                 /* 0x18 */
	"",                 /* 0x19 */
	"",                 /* 0x1a */
	"",                 /* 0x1b */
	"",                 /* 0x1c */
	"",                 /* 0x1d */
	"",                 /* 0x1e */
	"",                 /* 0x1f */
	"key",              /* 0x20 */
	"tuple",            /* 0x21 */
	"function name",    /* 0x22 */
	"user name",        /* 0x23 */
	"server UUID"       /* 0x24 */
	"cluster UUID"      /* 0x25 */
	"vector clock"      /* 0x26 */
};

void
iproto_header_decode(struct iproto_header *header, const char **pos,
		     const char *end)
{
	memset(header, 0, sizeof(struct iproto_header));
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
		case IPROTO_REQUEST_TYPE:
			header->type = mp_decode_uint(pos);
			break;
		case IPROTO_SYNC:
			header->sync = mp_decode_uint(pos);
			break;
		case IPROTO_SERVER_ID:
			header->server_id = mp_decode_uint(pos);
			break;
		case IPROTO_LSN:
			header->lsn = mp_decode_uint(pos);
			break;
		case IPROTO_TIMESTAMP:
			header->tm = mp_decode_double(pos);
			break;
		default:
			/* unknown header */
			mp_next(pos);
		}
	}
	assert(*pos <= end);
	if (*pos < end) {
		header->bodycnt = 1;
		header->body[0].iov_base = (void *) *pos;
		header->body[0].iov_len = (end - *pos);
		*pos = end;
	}
}

/**
 * @pre pos points at a valid msgpack
 */
void
iproto_decode_uuid(const char **pos, struct tt_uuid *out)
{
	if (mp_typeof(**pos) != MP_STR)
error:
		tnt_raise(ClientError, ER_INVALID_MSGPACK,
			  "UUID");
	uint32_t len = mp_decode_strl(pos);
	if (tt_uuid_from_strl(*pos, len, out) != 0)
		goto error;
	*pos += len;
}

int
iproto_header_encode(const struct iproto_header *header, struct iovec *out)
{
	enum { HEADER_LEN_MAX = 40 };

	/* allocate memory for sign + header */
	char *data = (char *) region_alloc(&fiber()->gc, HEADER_LEN_MAX);

	/* Header */
	char *d = data + 1; /* Skip 1 byte for MP_MAP */
	int map_size = 0;
	if (true) {
		d = mp_encode_uint(d, IPROTO_REQUEST_TYPE);
		d = mp_encode_uint(d, header->type);
		map_size++;
	}

	if (header->sync) {
		d = mp_encode_uint(d, IPROTO_SYNC);
		d = mp_encode_uint(d, header->sync);
		map_size++;
	}

	if (header->server_id) {
		d = mp_encode_uint(d, IPROTO_SERVER_ID);
		d = mp_encode_uint(d, header->server_id);
		map_size++;
	}

	if (header->lsn) {
		d = mp_encode_uint(d, IPROTO_LSN);
		d = mp_encode_uint(d, header->lsn);
		map_size++;
	}

	if (header->tm) {
		d = mp_encode_uint(d, IPROTO_TIMESTAMP);
		d = mp_encode_double(d, header->tm);
		map_size++;
	}

	assert(d <= data + HEADER_LEN_MAX);
	mp_encode_map(data, map_size);
	out->iov_base = data;
	out->iov_len = (d - data);
	out++;

	memcpy(out, header->body, sizeof(*out) * header->bodycnt);
	assert(1 + header->bodycnt <= IPROTO_PACKET_IOVMAX);
	return 1 + header->bodycnt; /* new iovcnt */
}

char *
iproto_encode_uuid(char *pos, const struct tt_uuid *in)
{
	return mp_encode_str(pos, tt_uuid_str(in), UUID_STR_LEN);
}

int
iproto_row_encode(const struct iproto_header *row,
		  struct iovec *out)
{
	int iovcnt = iproto_header_encode(row, out + 1) + 1;
	char *fixheader = (char *)
		region_alloc(&fiber()->gc, IPROTO_FIXHEADER_SIZE);
	uint32_t len = 0;
	for (int i = 1; i < iovcnt; i++)
		len += out[i].iov_len;

	/* Encode length */
	char *data = fixheader;
	data = mp_encode_uint(data, len);
	/* Encode padding */
	ssize_t padding = IPROTO_FIXHEADER_SIZE - (data - fixheader);
	if (padding > 0) {
		data = mp_encode_strl(data, padding - 1);
#if defined(NDEBUG)
		data += padding - 1;
#else
		while (--padding > 0)
			*(data++) = 0; /* valgrind */
#endif
	}
	assert(data == fixheader + IPROTO_FIXHEADER_SIZE);
	out[0].iov_base = fixheader;
	out[0].iov_len = IPROTO_FIXHEADER_SIZE;

	assert(iovcnt <= IPROTO_ROW_IOVMAX);
	return iovcnt;
}

void
iproto_decode_error(struct iproto_header *row)
{
	uint32_t code = row->type >> 8;
	if (likely(code == 0))
		return;
	char error[TNT_ERRMSG_MAX] = { 0 };
	const char *pos;
	uint32_t map_size;

	if (row->bodycnt == 0)
		goto raise;
	pos = (char *) row->body[0].iov_base;
	if (mp_check(&pos, pos + row->body[0].iov_len))
		goto raise;

	pos = (char *) row->body[0].iov_base;
	if (mp_typeof(*pos) != MP_MAP)
		goto raise;
	map_size = mp_decode_map(&pos);
	for (uint32_t i = 0; i < map_size; i++) {
		if (mp_typeof(*pos) != MP_UINT) {
			mp_next(&pos); /* key */
			mp_next(&pos); /* value */
			continue;
		}
		uint8_t key = mp_decode_uint(&pos);
		if (key != IPROTO_ERROR || mp_typeof(*pos) != MP_STR) {
			mp_next(&pos); /* value */
			continue;
		}

		uint32_t len;
		const char *str = mp_decode_str(&pos, &len);
		snprintf(error, sizeof(error), "%.*s", len, str);
	}

raise:
	tnt_raise(ClientError, error, code);
}

void
iproto_decode_subscribe(struct iproto_header *packet,
			struct tt_uuid *cluster_uuid,
			struct tt_uuid *server_uuid, struct vclock *vclock)
{
	if (packet->bodycnt == 0)
		tnt_raise(ClientError, ER_INVALID_MSGPACK, "request body");
	assert(packet->bodycnt == 1);
	const char *data = (const char *) packet->body[0].iov_base;
	const char *end = data + packet->body[0].iov_len;
	const char *d = data;
	if (mp_check(&d, end) != 0 || mp_typeof(*data) != MP_MAP)
		tnt_raise(ClientError, ER_INVALID_MSGPACK, "request body");

	const char *lsnmap = NULL;
	d = data;
	uint32_t map_size = mp_decode_map(&d);
	for (uint32_t i = 0; i < map_size; i++) {
		if (mp_typeof(*d) != MP_UINT) {
			mp_next(&d); /* key */
			mp_next(&d); /* value */
			continue;
		}
		uint8_t key = mp_decode_uint(&d);
		switch (key) {
		case IPROTO_CLUSTER_UUID:
			if (cluster_uuid == NULL)
				goto skip;
			iproto_decode_uuid(&d, cluster_uuid);
			break;
		case IPROTO_SERVER_UUID:
			if (server_uuid == NULL)
				goto skip;
			iproto_decode_uuid(&d, server_uuid);
			break;
		case IPROTO_VCLOCK:
			if (vclock == NULL)
				goto skip;
			if (mp_typeof(*d) != MP_MAP) {
				tnt_raise(ClientError, ER_INVALID_MSGPACK,
					  "invalid VCLOCK");
			}
			lsnmap = d;
			mp_next(&d);
			break;
		default: skip:
			mp_next(&d); /* value */
		}
	}

	if (lsnmap == NULL)
		return;

	/* Check & save LSNMAP */
	d = lsnmap;
	uint32_t lsnmap_size = mp_decode_map(&d);
	for (uint32_t i = 0; i < lsnmap_size; i++) {
		if (mp_typeof(*d) != MP_UINT) {
		map_error:
			tnt_raise(ClientError, ER_INVALID_MSGPACK, "VCLOCK");
		}
		uint32_t id = mp_decode_uint(&d);
		if (mp_typeof(*d) != MP_UINT)
			goto map_error;
		int64_t lsn = (int64_t) mp_decode_uint(&d);
		vclock_follow(vclock, id, lsn);
	}
}
