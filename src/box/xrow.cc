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
#include "xrow.h"
#include "msgpuck/msgpuck.h"
#include "exception.h"
#include "fiber.h"
#include "tt_uuid.h"
#include "vclock.h"
#include "scramble.h"
#include "third_party/base64.h"
#include "iproto_constants.h"

void
xrow_header_decode(struct xrow_header *header, const char **pos,
		   const char *end)
{
	memset(header, 0, sizeof(struct xrow_header));
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
xrow_decode_uuid(const char **pos, struct tt_uuid *out)
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
xrow_header_encode(const struct xrow_header *header, struct iovec *out)
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
	assert(1 + header->bodycnt <= XROW_IOVMAX);
	return 1 + header->bodycnt; /* new iovcnt */
}

char *
xrow_encode_uuid(char *pos, const struct tt_uuid *in)
{
	return mp_encode_str(pos, tt_uuid_str(in), UUID_STR_LEN);
}

int
xrow_to_iovec(const struct xrow_header *row,
	      struct iovec *out)
{
	static const int iov0_len = mp_sizeof_uint(UINT32_MAX);
	int iovcnt = xrow_header_encode(row, out + 1) + 1;
	char *fixheader = (char *) region_alloc(&fiber()->gc, iov0_len);
	uint32_t len = 0;
	for (int i = 1; i < iovcnt; i++)
		len += out[i].iov_len;

	/* Encode length */
	char *data = fixheader;
	*(data++) = 0xce; /* MP_UINT32 */
	*(uint32_t *) data = mp_bswap_u32(len);
	out[0].iov_base = fixheader;
	out[0].iov_len = iov0_len;

	assert(iovcnt <= XROW_IOVMAX);
	return iovcnt;
}

void
xrow_encode_auth(struct xrow_header *packet, const char *greeting,
		 const char *login, size_t login_len,
		 const char *password, size_t password_len)
{
	assert(login != NULL);
	memset(packet, 0, sizeof(*packet));

	enum { PACKET_LEN_MAX = 128 };
	size_t buf_size = PACKET_LEN_MAX + login_len + SCRAMBLE_SIZE;
	char *buf = (char *) region_alloc(&fiber()->gc, buf_size);

	char *d = buf;
	d = mp_encode_map(d, password != NULL ? 2 : 1);
	d = mp_encode_uint(d, IPROTO_USER_NAME);
	d = mp_encode_str(d, login, login_len);
	if (password != NULL) { /* password can be omitted */
		char salt[SCRAMBLE_SIZE];
		char scramble[SCRAMBLE_SIZE];
		if (base64_decode(greeting + 64, SCRAMBLE_BASE64_SIZE, salt,
				  SCRAMBLE_SIZE) != SCRAMBLE_SIZE)
			panic("invalid salt: %64s", greeting + 64);
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
xrow_encode_subscribe(struct xrow_header *row,
		      const struct tt_uuid *cluster_uuid,
		      const struct tt_uuid *server_uuid,
		      const struct vclock *vclock)
{
	memset(row, 0, sizeof(*row));
	uint32_t cluster_size = vclock_size(vclock);
	size_t size = 128 + cluster_size *
		(mp_sizeof_uint(UINT32_MAX) + mp_sizeof_uint(UINT64_MAX));
	char *buf = (char *) region_alloc(&fiber()->gc, size);
	char *data = buf;
	data = mp_encode_map(data, 3);
	data = mp_encode_uint(data, IPROTO_CLUSTER_UUID);
	data = xrow_encode_uuid(data, cluster_uuid);
	data = mp_encode_uint(data, IPROTO_SERVER_UUID);
	data = xrow_encode_uuid(data, server_uuid);
	data = mp_encode_uint(data, IPROTO_VCLOCK);
	data = mp_encode_map(data, cluster_size);
	vclock_foreach(vclock, server) {
		data = mp_encode_uint(data, server.id);
		data = mp_encode_uint(data, server.lsn);
	}
	assert(data <= buf + size);
	row->body[0].iov_base = buf;
	row->body[0].iov_len = (data - buf);
	row->bodycnt = 1;
	row->type = IPROTO_SUBSCRIBE;
}

void
xrow_decode_subscribe(struct xrow_header *row, struct tt_uuid *cluster_uuid,
			struct tt_uuid *server_uuid, struct vclock *vclock)
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
			if (cluster_uuid == NULL)
				goto skip;
			xrow_decode_uuid(&d, cluster_uuid);
			break;
		case IPROTO_SERVER_UUID:
			if (server_uuid == NULL)
				goto skip;
			xrow_decode_uuid(&d, server_uuid);
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

void
xrow_encode_join(struct xrow_header *row, const struct tt_uuid *server_uuid)
{
	memset(row, 0, sizeof(*row));

	size_t size = 64;
	char *buf = (char *) region_alloc(&fiber()->gc, size);
	char *data = buf;
	data = mp_encode_map(data, 1);
	data = mp_encode_uint(data, IPROTO_SERVER_UUID);
	/* Greet the remote server with our server UUID */
	data = xrow_encode_uuid(data, server_uuid);
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
	uint32_t cluster_size = vclock_size(vclock);
	size_t size = 8 + cluster_size *
		(mp_sizeof_uint(UINT32_MAX) + mp_sizeof_uint(UINT64_MAX));
	char *buf = (char *) region_alloc(&fiber()->gc, size);
	char *data = buf;
	data = mp_encode_map(data, 1);
	data = mp_encode_uint(data, IPROTO_VCLOCK);
	data = mp_encode_map(data, cluster_size);
	vclock_foreach(vclock, server) {
		data = mp_encode_uint(data, server.id);
		data = mp_encode_uint(data, server.lsn);
	}
	assert(data <= buf + size);
	row->body[0].iov_base = buf;
	row->body[0].iov_len = (data - buf);
	row->bodycnt = 1;
	row->type = IPROTO_OK;
}
