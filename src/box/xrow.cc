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
#include "third_party/base64.h"

#include "fiber.h"
#include "version.h"

#include "error.h"
#include "vclock.h"
#include "scramble.h"
#include "iproto_constants.h"

enum { HEADER_LEN_MAX = 40, BODY_LEN_MAX = 128 };

/**
 * Globally unique identifier of this instance.
 */
struct tt_uuid INSTANCE_UUID;

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
void
xrow_decode_uuid(const char **pos, struct tt_uuid *out)
{
	if (mp_typeof(**pos) != MP_STR) {
error:
		tnt_raise(ClientError, ER_INVALID_MSGPACK, "UUID");
	}
	uint32_t len = mp_decode_strl(pos);
	if (tt_uuid_from_strl(*pos, len, out) != 0)
		goto error;
	*pos += len;
}

int
xrow_header_encode(const struct xrow_header *header, struct iovec *out,
		   size_t fixheader_len)
{
	/* allocate memory for sign + header */
	out->iov_base = region_alloc(&fiber()->gc, HEADER_LEN_MAX +
				     fixheader_len);
	if (out->iov_base == NULL) {
		diag_set(OutOfMemory, HEADER_LEN_MAX + fixheader_len,
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
#if 0
	if (header->sync) {
		d = mp_encode_uint(d, IPROTO_SYNC);
		d = mp_encode_uint(d, header->sync);
		map_size++;
	}
#endif

	if (header->replica_id) {
		d = mp_encode_uint(d, IPROTO_REPLICA_ID);
		d = mp_encode_uint(d, header->replica_id);
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
	out->iov_len = d - (char *) out->iov_base;
	out++;

	memcpy(out, header->body, sizeof(*out) * header->bodycnt);
	assert(1 + header->bodycnt <= XROW_IOVMAX);
	return 1 + header->bodycnt; /* new iovcnt */
}

char *
xrow_encode_uuid(char *pos, const struct tt_uuid *in)
{
	return mp_encode_str(pos, tt_uuid_str(in), UUID_STR_LEN);
}

void
request_create(struct request *request, uint32_t type)
{
	memset(request, 0, sizeof(*request));
	request->type = type;
}

int
request_decode(struct request *request, const char *data, uint32_t len,
	       uint64_t key_map)
{
	const char *end = data + len;

	if (mp_typeof(*data) != MP_MAP || mp_check_map(data, end) > 0) {
error:
		tnt_error(ClientError, ER_INVALID_MSGPACK, "packet body");
		return -1;
	}
	uint32_t size = mp_decode_map(&data);
	for (uint32_t i = 0; i < size; i++) {
		if (! iproto_body_has_key(data, end)) {
			mp_check(&data, end);
			mp_check(&data, end);
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
		case IPROTO_FUNCTION_NAME:
		case IPROTO_USER_NAME:
		case IPROTO_EXPR:
			request->key = value;
			request->key_end = data;
			break;
		case IPROTO_OPS:
			request->ops = value;
			request->ops_end = data;
			break;
		default:
			break;
		}
	}
#ifndef NDEBUG
	if (data != end) {
		tnt_error(ClientError, ER_INVALID_MSGPACK, "packet end");
		return -1;
	}
#endif
	if (key_map) {
		enum iproto_key key = (enum iproto_key) bit_ctz_u64(key_map);
		tnt_error(ClientError, ER_MISSING_REQUEST_FIELD,
			  iproto_key_name(key));
		return -1;
	}
	return 0;
}

int
request_encode(struct request *request, struct iovec *iov)
{
	int iovcnt = 1;
	const int MAP_LEN_MAX = 40;
	uint32_t key_len = request->key_end - request->key;
	uint32_t ops_len = request->ops_end - request->ops;
	uint32_t len = MAP_LEN_MAX + key_len + ops_len;
	char *begin = (char *) region_alloc_xc(&fiber()->gc, len);
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
	if (request->tuple) {
		pos = mp_encode_uint(pos, IPROTO_TUPLE);
		iov[iovcnt].iov_base = (void *) request->tuple;
		iov[iovcnt].iov_len = (request->tuple_end - request->tuple);
		iovcnt++;
		map_size++;
	}

	assert(pos <= begin + len);
	mp_encode_map(begin, map_size);
	iov[0].iov_base = begin;
	iov[0].iov_len = pos - begin;

	return iovcnt;
}

struct request *
xrow_decode_request(struct xrow_header *row)
{
	struct request *request;
	request = region_alloc_object_xc(&fiber()->gc, struct request);
	request_create(request, row->type);
	request_decode_xc(request, (const char *) row->body[0].iov_base,
			  row->body[0].iov_len,
			  request_key_map(row->type));
	request->header = row;
	return request;
}

int
xrow_to_iovec(const struct xrow_header *row, struct iovec *out)
{
	static const int iov0_len = mp_sizeof_uint(UINT32_MAX);
	int iovcnt = xrow_header_encode_xc(row, out, iov0_len);
	ssize_t len = -iov0_len;
	for (int i = 0; i < iovcnt; i++)
		len += out[i].iov_len;

	/* Encode length */
	char *data = (char *) out[0].iov_base;
	*(data++) = 0xce; /* MP_UINT32 */
	*(uint32_t *) data = mp_bswap_u32(len);

	assert(iovcnt <= XROW_IOVMAX);
	return iovcnt;
}

void
xrow_encode_auth(struct xrow_header *packet, const char *salt, size_t salt_len,
		 const char *login, size_t login_len,
		 const char *password, size_t password_len)
{
	assert(login != NULL);
	memset(packet, 0, sizeof(*packet));

	size_t buf_size = BODY_LEN_MAX + login_len + SCRAMBLE_SIZE;
	char *buf = (char *) region_alloc_xc(&fiber()->gc, buf_size);

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
}

void
xrow_decode_error(struct xrow_header *row)
{
	uint32_t code = row->type & (IPROTO_TYPE_ERROR - 1);

	char error[DIAG_ERRMSG_MAX] = { 0 };
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
	box_error_set(__FILE__, __LINE__, code, error);
	diag_raise();
}

void
xrow_encode_subscribe(struct xrow_header *row,
		      const struct tt_uuid *replicaset_uuid,
		      const struct tt_uuid *instance_uuid,
		      const struct vclock *vclock)
{
	memset(row, 0, sizeof(*row));
	uint32_t replicaset_size = vclock_size(vclock);
	size_t size = BODY_LEN_MAX + replicaset_size *
		(mp_sizeof_uint(UINT32_MAX) + mp_sizeof_uint(UINT64_MAX));
	char *buf = (char *) region_alloc_xc(&fiber()->gc, size);
	char *data = buf;
	data = mp_encode_map(data, 3);
	data = mp_encode_uint(data, IPROTO_CLUSTER_UUID);
	data = xrow_encode_uuid(data, replicaset_uuid);
	data = mp_encode_uint(data, IPROTO_INSTANCE_UUID);
	data = xrow_encode_uuid(data, instance_uuid);
	data = mp_encode_uint(data, IPROTO_VCLOCK);
	data = mp_encode_map(data, replicaset_size);
	struct vclock_iterator it;
	vclock_iterator_init(&it, vclock);
	vclock_foreach(&it, replica) {
		data = mp_encode_uint(data, replica.id);
		data = mp_encode_uint(data, replica.lsn);
	}
	assert(data <= buf + size);
	row->body[0].iov_base = buf;
	row->body[0].iov_len = (data - buf);
	row->bodycnt = 1;
	row->type = IPROTO_SUBSCRIBE;
}

void
xrow_decode_subscribe(struct xrow_header *row, struct tt_uuid *replicaset_uuid,
		      struct tt_uuid *instance_uuid, struct vclock *vclock)
{
	if (row->bodycnt == 0)
		tnt_raise(ClientError, ER_INVALID_MSGPACK, "request body");
	assert(row->bodycnt == 1);
	const char *data = (const char *) row->body[0].iov_base;
	const char *end = data + row->body[0].iov_len;
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
			if (replicaset_uuid == NULL)
				goto skip;
			xrow_decode_uuid(&d, replicaset_uuid);
			break;
		case IPROTO_INSTANCE_UUID:
			if (instance_uuid == NULL)
				goto skip;
			xrow_decode_uuid(&d, instance_uuid);
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
		if (lsn > 0)
			vclock_follow(vclock, id, lsn);
	}
}

void
xrow_encode_join(struct xrow_header *row, const struct tt_uuid *instance_uuid)
{
	memset(row, 0, sizeof(*row));

	size_t size = 64;
	char *buf = (char *) region_alloc_xc(&fiber()->gc, size);
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
}

void
xrow_encode_vclock(struct xrow_header *row, const struct vclock *vclock)
{
	memset(row, 0, sizeof(*row));

	/* Add vclock to response body */
	uint32_t replicaset_size = vclock_size(vclock);
	size_t size = 8 + replicaset_size *
		(mp_sizeof_uint(UINT32_MAX) + mp_sizeof_uint(UINT64_MAX));
	char *buf = (char *) region_alloc_xc(&fiber()->gc, size);
	char *data = buf;
	data = mp_encode_map(data, 1);
	data = mp_encode_uint(data, IPROTO_VCLOCK);
	data = mp_encode_map(data, replicaset_size);
	struct vclock_iterator it;
	vclock_iterator_init(&it, vclock);
	vclock_foreach(&it, replica) {
		data = mp_encode_uint(data, replica.id);
		data = mp_encode_uint(data, replica.lsn);
	}
	assert(data <= buf + size);
	row->body[0].iov_base = buf;
	row->body[0].iov_len = (data - buf);
	row->bodycnt = 1;
	row->type = IPROTO_OK;
}

void
greeting_encode(char *greetingbuf, uint32_t version_id, const tt_uuid *uuid,
		const char *salt, uint32_t salt_len)
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

	assert(base64_bufsize(salt_len) + 1 < h);
	r = base64_encode(salt, salt_len, greetingbuf + h, h - 1);
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
