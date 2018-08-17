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
#include "xrow.h"

#include <msgpuck.h>
#include <small/region.h>
#include <small/obuf.h>
#include "third_party/base64.h"

#include "fiber.h"
#include "version.h"

#include "error.h"
#include "vclock.h"
#include "scramble.h"
#include "iproto_constants.h"

static inline uint32_t
mp_sizeof_vclock(const struct vclock *vclock)
{
	uint32_t size = vclock_size(vclock);
	return mp_sizeof_map(size) + size * (mp_sizeof_uint(UINT32_MAX) +
					     mp_sizeof_uint(UINT64_MAX));
}

static inline char *
mp_encode_vclock(char *data, const struct vclock *vclock)
{
	data = mp_encode_map(data, vclock_size(vclock));
	struct vclock_iterator it;
	vclock_iterator_init(&it, vclock);
	vclock_foreach(&it, replica) {
		data = mp_encode_uint(data, replica.id);
		data = mp_encode_uint(data, replica.lsn);
	}
	return data;
}

static int
mp_decode_vclock(const char **data, struct vclock *vclock)
{
	vclock_create(vclock);
	if (mp_typeof(**data) != MP_MAP)
		return -1;
	uint32_t size = mp_decode_map(data);
	for (uint32_t i = 0; i < size; i++) {
		if (mp_typeof(**data) != MP_UINT)
			return -1;
		uint32_t id = mp_decode_uint(data);
		if (mp_typeof(**data) != MP_UINT)
			return -1;
		int64_t lsn = mp_decode_uint(data);
		if (lsn > 0)
			vclock_follow(vclock, id, lsn);
	}
	return 0;
}

int
xrow_header_decode(struct xrow_header *header, const char **pos,
		   const char *end)
{
	memset(header, 0, sizeof(struct xrow_header));
	const char *tmp = *pos;
	if (mp_check(&tmp, end) != 0) {
error:
		diag_set(ClientError, ER_INVALID_MSGPACK, "packet header");
		return -1;
	}

	if (mp_typeof(**pos) != MP_MAP)
		goto error;

	uint32_t size = mp_decode_map(pos);
	for (uint32_t i = 0; i < size; i++) {
		if (mp_typeof(**pos) != MP_UINT)
			goto error;
		uint64_t key = mp_decode_uint(pos);
		if (key >= IPROTO_KEY_MAX ||
		    iproto_key_type[key] != mp_typeof(**pos))
			goto error;
		switch (key) {
		case IPROTO_REQUEST_TYPE:
			header->type = mp_decode_uint(pos);
			break;
		case IPROTO_SYNC:
			header->sync = mp_decode_uint(pos);
			break;
		case IPROTO_REPLICA_ID:
			header->replica_id = mp_decode_uint(pos);
			break;
		case IPROTO_GROUP_ID:
			header->group_id = mp_decode_uint(pos);
			break;
		case IPROTO_LSN:
			header->lsn = mp_decode_uint(pos);
			break;
		case IPROTO_TIMESTAMP:
			header->tm = mp_decode_double(pos);
			break;
		case IPROTO_SCHEMA_VERSION:
			header->schema_version = mp_decode_uint(pos);
			break;
		default:
			/* unknown header */
			mp_next(pos);
		}
	}
	assert(*pos <= end);
	if (*pos < end) {
		const char *body = *pos;
		if (mp_check(pos, end)) {
			diag_set(ClientError, ER_INVALID_MSGPACK, "packet body");
			return -1;
		}
		header->bodycnt = 1;
		header->body[0].iov_base = (void *) body;
		header->body[0].iov_len = *pos - body;
	}
	return 0;
}

/**
 * @pre pos points at a valid msgpack
 */
static inline int
xrow_decode_uuid(const char **pos, struct tt_uuid *out)
{
	if (mp_typeof(**pos) != MP_STR) {
error:
		diag_set(ClientError, ER_INVALID_MSGPACK, "UUID");
		return -1;
	}
	uint32_t len = mp_decode_strl(pos);
	if (tt_uuid_from_strl(*pos, len, out) != 0)
		goto error;
	*pos += len;
	return 0;
}

int
xrow_header_encode(const struct xrow_header *header, uint64_t sync,
		   struct iovec *out, size_t fixheader_len)
{
	/* allocate memory for sign + header */
	out->iov_base = region_alloc(&fiber()->gc, XROW_HEADER_LEN_MAX +
				     fixheader_len);
	if (out->iov_base == NULL) {
		diag_set(OutOfMemory, XROW_HEADER_LEN_MAX + fixheader_len,
			 "gc arena", "xrow header encode");
		return -1;
	}
	char *data = (char *) out->iov_base + fixheader_len;

	/* Header */
	char *d = data + 1; /* Skip 1 byte for MP_MAP */
	int map_size = 0;
	if (true) {
		d = mp_encode_uint(d, IPROTO_REQUEST_TYPE);
		d = mp_encode_uint(d, header->type);
		map_size++;
	}

	if (sync) {
		d = mp_encode_uint(d, IPROTO_SYNC);
		d = mp_encode_uint(d, sync);
		map_size++;
	}

	if (header->replica_id) {
		d = mp_encode_uint(d, IPROTO_REPLICA_ID);
		d = mp_encode_uint(d, header->replica_id);
		map_size++;
	}

	if (header->group_id) {
		d = mp_encode_uint(d, IPROTO_GROUP_ID);
		d = mp_encode_uint(d, header->group_id);
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
	assert(d <= data + XROW_HEADER_LEN_MAX);
	mp_encode_map(data, map_size);
	out->iov_len = d - (char *) out->iov_base;
	out++;

	memcpy(out, header->body, sizeof(*out) * header->bodycnt);
	assert(1 + header->bodycnt <= XROW_IOVMAX);
	return 1 + header->bodycnt; /* new iovcnt */
}

static inline char *
xrow_encode_uuid(char *pos, const struct tt_uuid *in)
{
	return mp_encode_str(pos, tt_uuid_str(in), UUID_STR_LEN);
}

/* m_ - msgpack meta, k_ - key, v_ - value */
struct PACKED iproto_header_bin {
	uint8_t m_len;                          /* MP_UINT32 */
	uint32_t v_len;                         /* length */
	uint8_t m_header;                       /* MP_MAP */
	uint8_t k_code;                         /* IPROTO_REQUEST_TYPE */
	uint8_t m_code;                         /* MP_UINT32 */
	uint32_t v_code;                        /* response status */
	uint8_t k_sync;                         /* IPROTO_SYNC */
	uint8_t m_sync;                         /* MP_UINT64 */
	uint64_t v_sync;                        /* sync */
	uint8_t k_schema_version;               /* IPROTO_SCHEMA_VERSION */
	uint8_t m_schema_version;               /* MP_UINT32 */
	uint32_t v_schema_version;              /* schema_version */
};

static_assert(sizeof(struct iproto_header_bin) == IPROTO_HEADER_LEN,
	      "sizeof(iproto_header_bin)");

void
iproto_header_encode(char *out, uint32_t type, uint64_t sync,
		     uint32_t schema_version, uint32_t body_length)
{
	struct iproto_header_bin header;
	header.m_len = 0xce;
	/* 5 - sizeof(m_len and v_len fields). */
	header.v_len = mp_bswap_u32(sizeof(header) + body_length - 5);
	header.m_header = 0x83;
	header.k_code = IPROTO_REQUEST_TYPE;
	header.m_code = 0xce;
	header.v_code = mp_bswap_u32(type);
	header.k_sync = IPROTO_SYNC;
	header.m_sync = 0xcf;
	header.v_sync = mp_bswap_u64(sync);
	header.k_schema_version = IPROTO_SCHEMA_VERSION;
	header.m_schema_version = 0xce;
	header.v_schema_version = mp_bswap_u32(schema_version);
	memcpy(out, &header, sizeof(header));
}

struct PACKED iproto_body_bin {
	uint8_t m_body;                    /* MP_MAP */
	uint8_t k_data;                    /* IPROTO_DATA or IPROTO_ERROR */
	uint8_t m_data;                    /* MP_STR or MP_ARRAY */
	uint32_t v_data_len;               /* string length of array size */
};

static const struct iproto_body_bin iproto_body_bin = {
	0x81, IPROTO_DATA, 0xdd, 0
};

static const struct iproto_body_bin iproto_error_bin = {
	0x81, IPROTO_ERROR, 0xdb, 0
};

/** Return a 4-byte numeric error code, with status flags. */
static inline uint32_t
iproto_encode_error(uint32_t error)
{
	return error | IPROTO_TYPE_ERROR;
}

int
iproto_reply_ok(struct obuf *out, uint64_t sync, uint32_t schema_version)
{
	char *buf = (char *)obuf_alloc(out, IPROTO_HEADER_LEN + 1);
	if (buf == NULL) {
		diag_set(OutOfMemory, IPROTO_HEADER_LEN + 1, "obuf_alloc",
			 "buf");
		return -1;
	}
	iproto_header_encode(buf, IPROTO_OK, sync, schema_version, 1);
	buf[IPROTO_HEADER_LEN] = 0x80; /* empty MessagePack Map */
	return 0;
}

int
iproto_reply_vclock(struct obuf *out, const struct vclock *vclock,
		    uint64_t sync, uint32_t schema_version)
{
	size_t max_size = IPROTO_HEADER_LEN + mp_sizeof_map(1) +
		mp_sizeof_uint(UINT32_MAX) + mp_sizeof_vclock(vclock);

	char *buf = obuf_reserve(out, max_size);
	if (buf == NULL) {
		diag_set(OutOfMemory, max_size,
			 "obuf_alloc", "buf");
		return -1;
	}

	char *data = buf + IPROTO_HEADER_LEN;
	data = mp_encode_map(data, 1);
	data = mp_encode_uint(data, IPROTO_VCLOCK);
	data = mp_encode_vclock(data, vclock);
	size_t size = data - buf;
	assert(size <= max_size);

	iproto_header_encode(buf, IPROTO_OK, sync, schema_version,
			     size - IPROTO_HEADER_LEN);

	char *ptr = obuf_alloc(out, size);
	assert(ptr == buf);
	return 0;
}

int
iproto_reply_vote(struct obuf *out, const struct ballot *ballot,
		  uint64_t sync, uint32_t schema_version)
{
	size_t max_size = IPROTO_HEADER_LEN + mp_sizeof_map(1) +
		mp_sizeof_uint(UINT32_MAX) + mp_sizeof_map(3) +
		mp_sizeof_uint(UINT32_MAX) + mp_sizeof_bool(ballot->is_ro) +
		mp_sizeof_uint(UINT32_MAX) + mp_sizeof_vclock(&ballot->vclock) +
		mp_sizeof_uint(UINT32_MAX) + mp_sizeof_vclock(&ballot->gc_vclock);

	char *buf = obuf_reserve(out, max_size);
	if (buf == NULL) {
		diag_set(OutOfMemory, max_size,
			 "obuf_alloc", "buf");
		return -1;
	}

	char *data = buf + IPROTO_HEADER_LEN;
	data = mp_encode_map(data, 1);
	data = mp_encode_uint(data, IPROTO_BALLOT);
	data = mp_encode_map(data, 3);
	data = mp_encode_uint(data, IPROTO_BALLOT_IS_RO);
	data = mp_encode_bool(data, ballot->is_ro);
	data = mp_encode_uint(data, IPROTO_BALLOT_VCLOCK);
	data = mp_encode_vclock(data, &ballot->vclock);
	data = mp_encode_uint(data, IPROTO_BALLOT_GC_VCLOCK);
	data = mp_encode_vclock(data, &ballot->gc_vclock);
	size_t size = data - buf;
	assert(size <= max_size);

	iproto_header_encode(buf, IPROTO_OK, sync, schema_version,
			     size - IPROTO_HEADER_LEN);

	char *ptr = obuf_alloc(out, size);
	assert(ptr == buf);
	return 0;
}

int
iproto_reply_error(struct obuf *out, const struct error *e, uint64_t sync,
		   uint32_t schema_version)
{
	uint32_t msg_len = strlen(e->errmsg);
	uint32_t errcode = box_error_code(e);

	struct iproto_body_bin body = iproto_error_bin;
	char *header = (char *)obuf_alloc(out, IPROTO_HEADER_LEN);
	if (header == NULL)
		return -1;

	iproto_header_encode(header, iproto_encode_error(errcode), sync,
			     schema_version, sizeof(body) + msg_len);
	body.v_data_len = mp_bswap_u32(msg_len);
	/* Malformed packet appears to be a lesser evil than abort. */
	return obuf_dup(out, &body, sizeof(body)) != sizeof(body) ||
	       obuf_dup(out, e->errmsg, msg_len) != msg_len ? -1 : 0;
}

void
iproto_write_error(int fd, const struct error *e, uint32_t schema_version,
		   uint64_t sync)
{
	uint32_t msg_len = strlen(e->errmsg);
	uint32_t errcode = box_error_code(e);

	char header[IPROTO_HEADER_LEN];
	struct iproto_body_bin body = iproto_error_bin;

	iproto_header_encode(header, iproto_encode_error(errcode), sync,
			     schema_version, sizeof(body) + msg_len);

	body.v_data_len = mp_bswap_u32(msg_len);
	(void) write(fd, header, sizeof(header));
	(void) write(fd, &body, sizeof(body));
	(void) write(fd, e->errmsg, msg_len);
}

enum { SVP_SIZE = IPROTO_HEADER_LEN  + sizeof(iproto_body_bin) };

int
iproto_prepare_select(struct obuf *buf, struct obuf_svp *svp)
{
	/**
	 * Reserve memory before taking a savepoint.
	 * This ensures that we get a contiguous chunk of memory
	 * and the savepoint is pointing at the beginning of it.
	 */
	void *ptr = obuf_reserve(buf, SVP_SIZE);
	if (ptr == NULL) {
		diag_set(OutOfMemory, SVP_SIZE, "obuf", "reserve");
		return -1;
	}
	*svp = obuf_create_svp(buf);
	ptr = obuf_alloc(buf, SVP_SIZE);
	assert(ptr !=  NULL);
	return 0;
}

void
iproto_reply_select(struct obuf *buf, struct obuf_svp *svp, uint64_t sync,
		    uint32_t schema_version, uint32_t count)
{
	char *pos = (char *) obuf_svp_to_ptr(buf, svp);
	iproto_header_encode(pos, IPROTO_OK, sync, schema_version,
			        obuf_size(buf) - svp->used -
				IPROTO_HEADER_LEN);

	struct iproto_body_bin body = iproto_body_bin;
	body.v_data_len = mp_bswap_u32(count);

	memcpy(pos + IPROTO_HEADER_LEN, &body, sizeof(body));
}

void
iproto_reply_chunk(struct obuf *buf, struct obuf_svp *svp, uint64_t sync,
		   uint32_t schema_version)
{
	char *pos = (char *) obuf_svp_to_ptr(buf, svp);
	iproto_header_encode(pos, IPROTO_CHUNK, sync, schema_version,
			     obuf_size(buf) - svp->used - IPROTO_HEADER_LEN);
	struct iproto_body_bin body = iproto_body_bin;
	body.v_data_len = mp_bswap_u32(1);
	memcpy(pos + IPROTO_HEADER_LEN, &body, sizeof(body));
}

int
xrow_decode_dml(struct xrow_header *row, struct request *request,
		uint64_t key_map)
{
	memset(request, 0, sizeof(*request));
	request->header = row;
	request->type = row->type;

	if (row->bodycnt == 0)
		goto done;

	assert(row->bodycnt == 1);
	const char *data = (const char *) row->body[0].iov_base;
	const char *end = data + row->body[0].iov_len;
	assert((end - data) > 0);

	if (mp_typeof(*data) != MP_MAP || mp_check_map(data, end) > 0) {
error:
		diag_set(ClientError, ER_INVALID_MSGPACK, "packet body");
		return -1;
	}

	uint32_t size = mp_decode_map(&data);
	for (uint32_t i = 0; i < size; i++) {
		if (! iproto_dml_body_has_key(data, end)) {
			if (mp_check(&data, end) != 0 ||
			    mp_check(&data, end) != 0)
				goto error;
			continue;
		}
		uint64_t key = mp_decode_uint(&data);
		const char *value = data;
		if (mp_check(&data, end) ||
		    key >= IPROTO_KEY_MAX ||
		    iproto_key_type[key] != mp_typeof(*value))
			goto error;
		key_map &= ~iproto_key_bit(key);
		switch (key) {
		case IPROTO_SPACE_ID:
			request->space_id = mp_decode_uint(&value);
			break;
		case IPROTO_INDEX_ID:
			request->index_id = mp_decode_uint(&value);
			break;
		case IPROTO_OFFSET:
			request->offset = mp_decode_uint(&value);
			break;
		case IPROTO_INDEX_BASE:
			request->index_base = mp_decode_uint(&value);
			break;
		case IPROTO_LIMIT:
			request->limit = mp_decode_uint(&value);
			break;
		case IPROTO_ITERATOR:
			request->iterator = mp_decode_uint(&value);
			break;
		case IPROTO_TUPLE:
			request->tuple = value;
			request->tuple_end = data;
			break;
		case IPROTO_KEY:
			request->key = value;
			request->key_end = data;
			break;
		case IPROTO_OPS:
			request->ops = value;
			request->ops_end = data;
			break;
		case IPROTO_TUPLE_META:
			request->tuple_meta = value;
			request->tuple_meta_end = data;
			break;
		default:
			break;
		}
	}
	if (data != end) {
		diag_set(ClientError, ER_INVALID_MSGPACK, "packet end");
		return -1;
	}
done:
	if (key_map) {
		enum iproto_key key = (enum iproto_key) bit_ctz_u64(key_map);
		diag_set(ClientError, ER_MISSING_REQUEST_FIELD,
			 iproto_key_name(key));
		return -1;
	}
	return 0;
}

const char *
request_str(const struct request *request)
{
	char *buf = tt_static_buf();
	char *end = buf + TT_STATIC_BUF_LEN;
	char *pos = buf;
	pos += snprintf(pos, end - pos, "{type: '%s', lsn: %lld, "\
			"space_id: %u, index_id: %u",
			iproto_type_name(request->type),
			(long long) request->header->lsn,
			(unsigned) request->space_id,
			(unsigned) request->index_id);
	if (request->key != NULL) {
		pos += snprintf(pos, end - pos, ", key: ");
		pos += mp_snprint(pos, end - pos, request->key);
	}
	if (request->tuple != NULL) {
		pos += snprintf(pos, end - pos, ", tuple: ");
		pos += mp_snprint(pos, end - pos, request->tuple);
	}
	if (request->ops != NULL) {
		pos += snprintf(pos, end - pos, ", ops: ");
		pos += mp_snprint(pos, end - pos, request->ops);
	}
	pos += snprintf(pos, end - pos, "}");
	return buf;
}

int
xrow_encode_dml(const struct request *request, struct iovec *iov)
{
	int iovcnt = 1;
	const int MAP_LEN_MAX = 40;
	uint32_t key_len = request->key_end - request->key;
	uint32_t ops_len = request->ops_end - request->ops;
	uint32_t tuple_meta_len = request->tuple_meta_end - request->tuple_meta;
	uint32_t len = MAP_LEN_MAX + key_len + ops_len + tuple_meta_len;
	char *begin = (char *) region_alloc(&fiber()->gc, len);
	if (begin == NULL) {
		diag_set(OutOfMemory, len, "region_alloc", "begin");
		return -1;
	}
	char *pos = begin + 1;     /* skip 1 byte for MP_MAP */
	int map_size = 0;
	if (request->space_id) {
		pos = mp_encode_uint(pos, IPROTO_SPACE_ID);
		pos = mp_encode_uint(pos, request->space_id);
		map_size++;
	}
	if (request->index_id) {
		pos = mp_encode_uint(pos, IPROTO_INDEX_ID);
		pos = mp_encode_uint(pos, request->index_id);
		map_size++;
	}
	if (request->index_base) { /* UPDATE/UPSERT */
		pos = mp_encode_uint(pos, IPROTO_INDEX_BASE);
		pos = mp_encode_uint(pos, request->index_base);
		map_size++;
	}
	if (request->key) {
		pos = mp_encode_uint(pos, IPROTO_KEY);
		memcpy(pos, request->key, key_len);
		pos += key_len;
		map_size++;
	}
	if (request->ops) {
		pos = mp_encode_uint(pos, IPROTO_OPS);
		memcpy(pos, request->ops, ops_len);
		pos += ops_len;
		map_size++;
	}
	if (request->tuple_meta) {
		pos = mp_encode_uint(pos, IPROTO_TUPLE_META);
		memcpy(pos, request->tuple_meta, tuple_meta_len);
		pos += tuple_meta_len;
		map_size++;
	}
	if (request->tuple) {
		pos = mp_encode_uint(pos, IPROTO_TUPLE);
		iov[iovcnt].iov_base = (void *) request->tuple;
		iov[iovcnt].iov_len = (request->tuple_end - request->tuple);
		iovcnt++;
		map_size++;
	}

	if (map_size == 0)
		return 0;

	assert(pos <= begin + len);
	mp_encode_map(begin, map_size);
	iov[0].iov_base = begin;
	iov[0].iov_len = pos - begin;

	return iovcnt;
}

int
xrow_to_iovec(const struct xrow_header *row, struct iovec *out)
{
	assert(mp_sizeof_uint(UINT32_MAX) == 5);
	int iovcnt = xrow_header_encode(row, row->sync, out, 5);
	if (iovcnt < 0)
		return -1;
	ssize_t len = -5;
	for (int i = 0; i < iovcnt; i++)
		len += out[i].iov_len;

	/* Encode length */
	char *data = (char *) out[0].iov_base;
	*(data++) = 0xce; /* MP_UINT32 */
	*(uint32_t *) data = mp_bswap_u32(len);

	assert(iovcnt <= XROW_IOVMAX);
	return iovcnt;
}

int
xrow_decode_call(const struct xrow_header *row, struct call_request *request)
{
	if (row->bodycnt == 0) {
		diag_set(ClientError, ER_INVALID_MSGPACK,
			 "missing request body");
		return -1;
	}

	assert(row->bodycnt == 1);
	const char *data = (const char *) row->body[0].iov_base;
	const char *end = data + row->body[0].iov_len;
	assert((end - data) > 0);

	if (mp_typeof(*data) != MP_MAP || mp_check_map(data, end) > 0) {
error:
		diag_set(ClientError, ER_INVALID_MSGPACK, "packet body");
		return -1;
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
		case IPROTO_FUNCTION_NAME:
			if (mp_typeof(*value) != MP_STR)
				goto error;
			request->name = value;
			break;
		case IPROTO_EXPR:
			if (mp_typeof(*value) != MP_STR)
				goto error;
			request->expr = value;
			break;
		case IPROTO_TUPLE:
			if (mp_typeof(*value) != MP_ARRAY)
				goto error;
			request->args = value;
			request->args_end = data;
			break;
		default:
			continue; /* unknown key */
		}
	}
	if (data != end) {
		diag_set(ClientError, ER_INVALID_MSGPACK, "packet end");
		return -1;
	}
	if (row->type == IPROTO_EVAL) {
		if (request->expr == NULL) {
			diag_set(ClientError, ER_MISSING_REQUEST_FIELD,
				 iproto_key_name(IPROTO_EXPR));
			return -1;
		}
	} else if (request->name == NULL) {
		assert(row->type == IPROTO_CALL_16 ||
		       row->type == IPROTO_CALL);
		diag_set(ClientError, ER_MISSING_REQUEST_FIELD,
			 iproto_key_name(IPROTO_FUNCTION_NAME));
		return -1;
	}
	if (request->args == NULL) {
		static const char empty_args[] = { (char)0x90 };
		request->args = empty_args;
		request->args_end = empty_args + sizeof(empty_args);
	}
	return 0;
}

int
xrow_decode_auth(const struct xrow_header *row, struct auth_request *request)
{
	if (row->bodycnt == 0) {
		diag_set(ClientError, ER_INVALID_MSGPACK,
			 "missing request body");
		return -1;
	}

	assert(row->bodycnt == 1);
	const char *data = (const char *) row->body[0].iov_base;
	const char *end = data + row->body[0].iov_len;
	assert((end - data) > 0);

	if (mp_typeof(*data) != MP_MAP || mp_check_map(data, end) > 0) {
error:
		diag_set(ClientError, ER_INVALID_MSGPACK, "packet body");
		return -1;
	}

	memset(request, 0, sizeof(*request));

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
		return -1;
	}
	if (request->user_name == NULL) {
		diag_set(ClientError, ER_MISSING_REQUEST_FIELD,
			  iproto_key_name(IPROTO_USER_NAME));
		return -1;
	}
	if (request->scramble == NULL) {
		diag_set(ClientError, ER_MISSING_REQUEST_FIELD,
			 iproto_key_name(IPROTO_TUPLE));
		return -1;
	}
	return 0;
}

int
xrow_encode_auth(struct xrow_header *packet, const char *salt, size_t salt_len,
		 const char *login, size_t login_len,
		 const char *password, size_t password_len)
{
	assert(login != NULL);
	memset(packet, 0, sizeof(*packet));

	size_t buf_size = XROW_BODY_LEN_MAX + login_len + SCRAMBLE_SIZE;
	char *buf = (char *) region_alloc(&fiber()->gc, buf_size);
	if (buf == NULL) {
		diag_set(OutOfMemory, buf_size, "region_alloc", "buf");
		return -1;
	}

	char *d = buf;
	d = mp_encode_map(d, password != NULL ? 2 : 1);
	d = mp_encode_uint(d, IPROTO_USER_NAME);
	d = mp_encode_str(d, login, login_len);
	if (password != NULL) { /* password can be omitted */
		assert(salt_len >= SCRAMBLE_SIZE); /* greetingbuf_decode */
		(void) salt_len;
		char scramble[SCRAMBLE_SIZE];
		scramble_prepare(scramble, salt, password, password_len);
		d = mp_encode_uint(d, IPROTO_TUPLE);
		d = mp_encode_array(d, 2);
		d = mp_encode_str(d, "chap-sha1", strlen("chap-sha1"));
		d = mp_encode_str(d, scramble, SCRAMBLE_SIZE);
	}

	assert(d <= buf + buf_size);
	packet->body[0].iov_base = buf;
	packet->body[0].iov_len = (d - buf);
	packet->bodycnt = 1;
	packet->type = IPROTO_AUTH;
	return 0;
}

void
xrow_decode_error(struct xrow_header *row)
{
	uint32_t code = row->type & (IPROTO_TYPE_ERROR - 1);

	char error[DIAG_ERRMSG_MAX] = { 0 };
	const char *pos;
	uint32_t map_size;

	if (row->bodycnt == 0)
		goto error;
	pos = (char *) row->body[0].iov_base;
	if (mp_check(&pos, pos + row->body[0].iov_len))
		goto error;

	pos = (char *) row->body[0].iov_base;
	if (mp_typeof(*pos) != MP_MAP)
		goto error;
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

error:
	box_error_set(__FILE__, __LINE__, code, error);
}

void
xrow_encode_vote(struct xrow_header *row)
{
	memset(row, 0, sizeof(*row));
	row->type = IPROTO_VOTE;
}

int
xrow_decode_ballot(struct xrow_header *row, struct ballot *ballot)
{
	ballot->is_ro = false;
	vclock_create(&ballot->vclock);

	if (row->bodycnt == 0)
		goto err;
	assert(row->bodycnt == 1);

	const char *data = (const char *) row->body[0].iov_base;
	const char *end = data + row->body[0].iov_len;
	const char *tmp = data;
	if (mp_check(&tmp, end) != 0 || mp_typeof(*data) != MP_MAP)
		goto err;

	/* Find BALLOT key. */
	uint32_t map_size = mp_decode_map(&data);
	for (uint32_t i = 0; i < map_size; i++) {
		if (mp_typeof(*data) != MP_UINT) {
			mp_next(&data); /* key */
			mp_next(&data); /* value */
			continue;
		}
		if (mp_decode_uint(&data) == IPROTO_BALLOT)
			break;
	}
	if (data == end)
		return 0;

	/* Decode BALLOT map. */
	map_size = mp_decode_map(&data);
	for (uint32_t i = 0; i < map_size; i++) {
		if (mp_typeof(*data) != MP_UINT) {
			mp_next(&data); /* key */
			mp_next(&data); /* value */
			continue;
		}
		uint32_t key = mp_decode_uint(&data);
		switch (key) {
		case IPROTO_BALLOT_IS_RO:
			if (mp_typeof(*data) != MP_BOOL)
				goto err;
			ballot->is_ro = mp_decode_bool(&data);
			break;
		case IPROTO_BALLOT_VCLOCK:
			if (mp_decode_vclock(&data, &ballot->vclock) != 0)
				goto err;
			break;
		case IPROTO_BALLOT_GC_VCLOCK:
			if (mp_decode_vclock(&data, &ballot->gc_vclock) != 0)
				goto err;
			break;
		default:
			mp_next(&data);
		}
	}
	return 0;
err:
	diag_set(ClientError, ER_INVALID_MSGPACK, "packet body");
	return -1;
}

int
xrow_encode_subscribe(struct xrow_header *row,
		      const struct tt_uuid *replicaset_uuid,
		      const struct tt_uuid *instance_uuid,
		      const struct vclock *vclock)
{
	memset(row, 0, sizeof(*row));
	size_t size = XROW_BODY_LEN_MAX + mp_sizeof_vclock(vclock);
	char *buf = (char *) region_alloc(&fiber()->gc, size);
	if (buf == NULL) {
		diag_set(OutOfMemory, size, "region_alloc", "buf");
		return -1;
	}
	char *data = buf;
	data = mp_encode_map(data, 4);
	data = mp_encode_uint(data, IPROTO_CLUSTER_UUID);
	data = xrow_encode_uuid(data, replicaset_uuid);
	data = mp_encode_uint(data, IPROTO_INSTANCE_UUID);
	data = xrow_encode_uuid(data, instance_uuid);
	data = mp_encode_uint(data, IPROTO_VCLOCK);
	data = mp_encode_vclock(data, vclock);
	data = mp_encode_uint(data, IPROTO_SERVER_VERSION);
	data = mp_encode_uint(data, tarantool_version_id());
	assert(data <= buf + size);
	row->body[0].iov_base = buf;
	row->body[0].iov_len = (data - buf);
	row->bodycnt = 1;
	row->type = IPROTO_SUBSCRIBE;
	return 0;
}

int
xrow_decode_subscribe(struct xrow_header *row, struct tt_uuid *replicaset_uuid,
		      struct tt_uuid *instance_uuid, struct vclock *vclock,
		      uint32_t *version_id)
{
	if (row->bodycnt == 0) {
		diag_set(ClientError, ER_INVALID_MSGPACK, "request body");
		return -1;
	}
	assert(row->bodycnt == 1);
	const char *data = (const char *) row->body[0].iov_base;
	const char *end = data + row->body[0].iov_len;
	const char *d = data;
	if (mp_check(&d, end) != 0 || mp_typeof(*data) != MP_MAP) {
		diag_set(ClientError, ER_INVALID_MSGPACK, "request body");
		return -1;
	}

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
			if (replicaset_uuid == NULL)
				goto skip;
			if (xrow_decode_uuid(&d, replicaset_uuid) != 0)
				return -1;
			break;
		case IPROTO_INSTANCE_UUID:
			if (instance_uuid == NULL)
				goto skip;
			if (xrow_decode_uuid(&d, instance_uuid) != 0)
				return -1;
			break;
		case IPROTO_VCLOCK:
			if (vclock == NULL)
				goto skip;
			if (mp_decode_vclock(&d, vclock) != 0) {
				diag_set(ClientError, ER_INVALID_MSGPACK,
					 "invalid VCLOCK");
				return -1;
			}
			break;
		case IPROTO_SERVER_VERSION:
			if (version_id == NULL)
				goto skip;
			if (mp_typeof(*d) != MP_UINT) {
				diag_set(ClientError, ER_INVALID_MSGPACK,
					 "invalid VERSION");
				return -1;
			}
			*version_id = mp_decode_uint(&d);
			break;
		default: skip:
			mp_next(&d); /* value */
		}
	}
	return 0;
}

int
xrow_encode_join(struct xrow_header *row, const struct tt_uuid *instance_uuid)
{
	memset(row, 0, sizeof(*row));

	size_t size = 64;
	char *buf = (char *) region_alloc(&fiber()->gc, size);
	if (buf == NULL) {
		diag_set(OutOfMemory, size, "region_alloc", "buf");
		return -1;
	}
	char *data = buf;
	data = mp_encode_map(data, 1);
	data = mp_encode_uint(data, IPROTO_INSTANCE_UUID);
	/* Greet the remote replica with our replica UUID */
	data = xrow_encode_uuid(data, instance_uuid);
	assert(data <= buf + size);

	row->body[0].iov_base = buf;
	row->body[0].iov_len = (data - buf);
	row->bodycnt = 1;
	row->type = IPROTO_JOIN;
	return 0;
}

int
xrow_encode_vclock(struct xrow_header *row, const struct vclock *vclock)
{
	memset(row, 0, sizeof(*row));

	/* Add vclock to response body */
	size_t size = 8 + mp_sizeof_vclock(vclock);
	char *buf = (char *) region_alloc(&fiber()->gc, size);
	if (buf == NULL) {
		diag_set(OutOfMemory, size, "region_alloc", "buf");
		return -1;
	}
	char *data = buf;
	data = mp_encode_map(data, 1);
	data = mp_encode_uint(data, IPROTO_VCLOCK);
	data = mp_encode_vclock(data, vclock);
	assert(data <= buf + size);
	row->body[0].iov_base = buf;
	row->body[0].iov_len = (data - buf);
	row->bodycnt = 1;
	row->type = IPROTO_OK;
	return 0;
}

void
xrow_encode_timestamp(struct xrow_header *row, uint32_t replica_id, double tm)
{
	memset(row, 0, sizeof(*row));
	row->type = IPROTO_OK;
	row->replica_id = replica_id;
	row->tm = tm;
}

void
greeting_encode(char *greetingbuf, uint32_t version_id,
		const struct tt_uuid *uuid, const char *salt, uint32_t salt_len)
{
	int h = IPROTO_GREETING_SIZE / 2;
	int r = snprintf(greetingbuf, h + 1, "Tarantool %u.%u.%u (Binary) ",
		version_id_major(version_id), version_id_minor(version_id),
		version_id_patch(version_id));

	assert(r + UUID_STR_LEN < h);
	tt_uuid_to_string(uuid, greetingbuf + r);
	r += UUID_STR_LEN;
	memset(greetingbuf + r, ' ', h - r - 1);
	greetingbuf[h - 1] = '\n';

	assert(base64_bufsize(salt_len, 0) + 1 < h);
	r = base64_encode(salt, salt_len, greetingbuf + h, h - 1, 0);
	assert(r < h);
	memset(greetingbuf + h + r, ' ', h - r - 1);
	greetingbuf[IPROTO_GREETING_SIZE - 1] = '\n';
}

int
greeting_decode(const char *greetingbuf, struct greeting *greeting)
{
	/* Check basic structure - magic string and \n delimiters */
	if (memcmp(greetingbuf, "Tarantool ", strlen("Tarantool ")) != 0 ||
	    greetingbuf[IPROTO_GREETING_SIZE / 2 - 1] != '\n' ||
	    greetingbuf[IPROTO_GREETING_SIZE - 1] != '\n')
		return -1;
	memset(greeting, 0, sizeof(*greeting));
	int h = IPROTO_GREETING_SIZE / 2;
	const char *pos = greetingbuf + strlen("Tarantool ");
	const char *end = greetingbuf + h;
	for (; pos < end && *pos == ' '; ++pos); /* skip spaces */

	/* Extract a version string - a string until ' ' */
	char version[20];
	const char *vend = (const char *) memchr(pos, ' ', end - pos);
	if (vend == NULL || (size_t)(vend - pos) >= sizeof(version))
		return -1;
	memcpy(version, pos, vend - pos);
	version[vend - pos] = '\0';
	pos = vend + 1;
	for (; pos < end && *pos == ' '; ++pos); /* skip spaces */

	/* Parse a version string - 1.6.6-83-gc6b2129 or 1.6.7 */
	unsigned major, minor, patch;
	if (sscanf(version, "%u.%u.%u", &major, &minor, &patch) != 3)
		return -1;
	greeting->version_id = version_id(major, minor, patch);

	if (*pos == '(') {
		/* Extract protocol name - a string between (parentheses) */
		vend = (const char *) memchr(pos + 1, ')', end - pos);
		if (!vend || (vend - pos - 1) > GREETING_PROTOCOL_LEN_MAX)
			return -1;
		memcpy(greeting->protocol, pos + 1, vend - pos - 1);
		greeting->protocol[vend - pos - 1] = '\0';
		pos = vend + 1;
		/* Parse protocol name - Binary or  Lua console. */
		if (strcmp(greeting->protocol, "Binary") != 0)
			return 0;

		if (greeting->version_id >= version_id(1, 6, 7)) {
			if (*(pos++) != ' ')
				return -1;
			for (; pos < end && *pos == ' '; ++pos); /* spaces */
			if (end - pos < UUID_STR_LEN)
				return -1;
			if (tt_uuid_from_strl(pos, UUID_STR_LEN, &greeting->uuid))
				return -1;
		}
	} else if (greeting->version_id < version_id(1, 6, 7)) {
		/* Tarantool < 1.6.7 doesn't add "(Binary)" to greeting */
		strcpy(greeting->protocol, "Binary");
	} else {
		return -1; /* Sorry, don't want to parse this greeting */
	}

	/* Decode salt for binary protocol */
	greeting->salt_len = base64_decode(greetingbuf + h, h - 1,
					   greeting->salt,
					   sizeof(greeting->salt));
	if (greeting->salt_len < SCRAMBLE_SIZE || greeting->salt_len >= (uint32_t)h)
		return -1;

	return 0;
}
