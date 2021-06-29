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
#include <base64.h>

#include "fiber.h"
#include "version.h"
#include "tt_static.h"
#include "error.h"
#include "mp_error.h"
#include "scramble.h"
#include "iproto_constants.h"
#include "mpstream/mpstream.h"
#include "errinj.h"

static_assert(IPROTO_DATA < 0x7f && IPROTO_METADATA < 0x7f &&
	      IPROTO_SQL_INFO < 0x7f, "encoded IPROTO_BODY keys must fit into "\
	      "one byte");

static inline uint32_t
mp_sizeof_vclock_ignore0(const struct vclock *vclock)
{
	uint32_t size = vclock_size_ignore0(vclock);
	return mp_sizeof_map(size) + size * (mp_sizeof_uint(UINT32_MAX) +
					     mp_sizeof_uint(UINT64_MAX));
}

static inline char *
mp_encode_vclock_ignore0(char *data, const struct vclock *vclock)
{
	data = mp_encode_map(data, vclock_size_ignore0(vclock));
	struct vclock_iterator it;
	vclock_iterator_init(&it, vclock);
	struct vclock_c replica;
	replica = vclock_iterator_next(&it);
	if (replica.id == 0)
		replica = vclock_iterator_next(&it);
	for ( ; replica.id < VCLOCK_MAX; replica = vclock_iterator_next(&it)) {
		data = mp_encode_uint(data, replica.id);
		data = mp_encode_uint(data, replica.lsn);
	}
	return data;
}

static int
mp_decode_vclock_ignore0(const char **data, struct vclock *vclock)
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
		/*
		 * Skip vclock[0] coming from the remote
		 * instances.
		 */
		if (lsn > 0 && id != 0)
			vclock_follow(vclock, id, lsn);
	}
	return 0;
}

/**
 * If log_level is 'verbose' or greater,
 * dump the corrupted row contents in hex to the log.
 *
 * The format is similar to the xxd utility.
 */
void dump_row_hex(const char *start, const char *end) {
	if (!say_log_level_is_enabled(S_VERBOSE))
		return;

	char *buf = tt_static_buf();
	const char *buf_end = buf + TT_STATIC_BUF_LEN;

	say_verbose("Got a corrupted row:");
	for (const char *cur = start; cur < end;) {
		char *pos = buf;
		pos += snprintf(pos, buf_end - pos, "%08lX: ", cur - start);
		for (size_t i = 0; i < 16; ++i) {
			pos += snprintf(pos, buf_end - pos, "%02X ", (unsigned char)*cur++);
			if (cur >= end || pos == buf_end)
				break;
		}
		*pos = '\0';
		say_verbose("%s", buf);
	}
}

#define xrow_on_decode_err(start, end, what, desc_str) do {\
	diag_set(ClientError, what, desc_str);\
	dump_row_hex(start, end);\
} while (0);

int
xrow_header_decode(struct xrow_header *header, const char **pos,
		   const char *end, bool end_is_exact)
{
	memset(header, 0, sizeof(struct xrow_header));
	const char *tmp = *pos;
	const char * const start = *pos;
	if (mp_check(&tmp, end) != 0) {
error:
		xrow_on_decode_err(start, end, ER_INVALID_MSGPACK, "packet header");
		return -1;
	}

	if (mp_typeof(**pos) != MP_MAP)
		goto error;
	bool has_tsn = false;
	uint32_t flags = 0;

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
		case IPROTO_TSN:
			has_tsn = true;
			header->tsn = mp_decode_uint(pos);
			break;
		case IPROTO_FLAGS:
			flags = mp_decode_uint(pos);
			header->flags = flags;
			break;
		default:
			/* unknown header */
			mp_next(pos);
		}
	}
	assert(*pos <= end);
	if (!has_tsn) {
		/*
		 * Transaction id is not set so it is a single statement
		 * transaction.
		 */
		header->is_commit = true;
	}
	/* Restore transaction id from lsn and transaction serial number. */
	header->tsn = header->lsn - header->tsn;

	/* Nop requests aren't supposed to have a body. */
	if (*pos < end && header->type != IPROTO_NOP) {
		const char *body = *pos;
		if (mp_check(pos, end)) {
			xrow_on_decode_err(start, end, ER_INVALID_MSGPACK, "packet body");
			return -1;
		}
		header->bodycnt = 1;
		header->body[0].iov_base = (void *) body;
		header->body[0].iov_len = *pos - body;
	}
	if (end_is_exact && *pos < end) {
		xrow_on_decode_err(start,end, ER_INVALID_MSGPACK, "packet body");
		return -1;
	}
	return 0;
}

/**
 * @pre pos points at a valid msgpack
 */
static inline int
xrow_decode_uuid(const char **pos, struct tt_uuid *out)
{
	if (mp_typeof(**pos) != MP_STR)
		return -1;
	uint32_t len = mp_decode_strl(pos);
	if (tt_uuid_from_strl(*pos, len, out) != 0)
		return -1;
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
	/*
	 * We do not encode tsn and is_commit flags for
	 * single-statement transactions to save space in the
	 * binary log. We also encode tsn as a diff from lsn
	 * to save space in every multi-statement transaction row.
	 * The rules when encoding are simple:
	 * - if tsn is *not* encoded, it's a single-statement
	 *   transaction, tsn = lsn, is_commit = true
	 * - if tsn is present, it's a multi-statement
	 *   transaction, tsn = tsn + lsn, check is_commit
	 *   flag to find transaction boundary (last row in the
	 *   transaction stream).
	 */
	uint8_t flags_to_encode = header->flags & ~IPROTO_FLAG_COMMIT;
	if (header->tsn != 0) {
		if (header->tsn != header->lsn || !header->is_commit) {
			/*
			 * Encode a transaction identifier for multi row
			 * transaction members.
			 */
			d = mp_encode_uint(d, IPROTO_TSN);
			/*
			 * Differential encoding: write a transaction serial
			 * number (it is equal to lsn - transaction id) instead.
			 */
			d = mp_encode_uint(d, header->lsn - header->tsn);
			map_size++;
		}
		if (header->is_commit && header->tsn != header->lsn) {
			flags_to_encode |= IPROTO_FLAG_COMMIT;
		}
	}
	if (flags_to_encode != 0) {
		d = mp_encode_uint(d, IPROTO_FLAGS);
		d = mp_encode_uint(d, flags_to_encode);
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
iproto_header_encode(char *out, uint16_t type, uint64_t sync,
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
	uint8_t k_data;                    /* IPROTO_DATA or errors */
	uint8_t m_data;                    /* MP_STR or MP_ARRAY */
	uint32_t v_data_len;               /* string length of array size */
};

static_assert(sizeof(struct iproto_body_bin) + IPROTO_HEADER_LEN ==
	      IPROTO_SELECT_HEADER_LEN, "size of the prepared select");

static const struct iproto_body_bin iproto_body_bin = {
	0x81, IPROTO_DATA, 0xdd, 0
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
		mp_sizeof_uint(UINT32_MAX) + mp_sizeof_vclock_ignore0(vclock);

	char *buf = obuf_reserve(out, max_size);
	if (buf == NULL) {
		diag_set(OutOfMemory, max_size,
			 "obuf_alloc", "buf");
		return -1;
	}

	char *data = buf + IPROTO_HEADER_LEN;
	data = mp_encode_map(data, 1);
	data = mp_encode_uint(data, IPROTO_VCLOCK);
	data = mp_encode_vclock_ignore0(data, vclock);
	size_t size = data - buf;
	assert(size <= max_size);

	iproto_header_encode(buf, IPROTO_OK, sync, schema_version,
			     size - IPROTO_HEADER_LEN);

	char *ptr = obuf_alloc(out, size);
	(void) ptr;
	assert(ptr == buf);
	return 0;
}

int
iproto_reply_vote(struct obuf *out, const struct ballot *ballot,
		  uint64_t sync, uint32_t schema_version)
{
	size_t max_size = IPROTO_HEADER_LEN + mp_sizeof_map(1) +
		mp_sizeof_uint(UINT32_MAX) + mp_sizeof_map(6) +
		mp_sizeof_uint(UINT32_MAX) + mp_sizeof_bool(ballot->is_ro_cfg) +
		mp_sizeof_uint(UINT32_MAX) + mp_sizeof_bool(ballot->is_ro) +
		mp_sizeof_uint(IPROTO_BALLOT_IS_ANON) +
		mp_sizeof_bool(ballot->is_anon) +
		mp_sizeof_uint(IPROTO_BALLOT_IS_BOOTED) +
		mp_sizeof_bool(ballot->is_booted) +
		mp_sizeof_uint(UINT32_MAX) +
		mp_sizeof_vclock_ignore0(&ballot->vclock) +
		mp_sizeof_uint(UINT32_MAX) +
		mp_sizeof_vclock_ignore0(&ballot->gc_vclock);

	char *buf = obuf_reserve(out, max_size);
	if (buf == NULL) {
		diag_set(OutOfMemory, max_size,
			 "obuf_alloc", "buf");
		return -1;
	}

	char *data = buf + IPROTO_HEADER_LEN;
	data = mp_encode_map(data, 1);
	data = mp_encode_uint(data, IPROTO_BALLOT);
	data = mp_encode_map(data, 6);
	data = mp_encode_uint(data, IPROTO_BALLOT_IS_RO_CFG);
	data = mp_encode_bool(data, ballot->is_ro_cfg);
	data = mp_encode_uint(data, IPROTO_BALLOT_IS_RO);
	data = mp_encode_bool(data, ballot->is_ro);
	data = mp_encode_uint(data, IPROTO_BALLOT_IS_ANON);
	data = mp_encode_bool(data, ballot->is_anon);
	data = mp_encode_uint(data, IPROTO_BALLOT_IS_BOOTED);
	data = mp_encode_bool(data, ballot->is_booted);
	data = mp_encode_uint(data, IPROTO_BALLOT_VCLOCK);
	data = mp_encode_vclock_ignore0(data, &ballot->vclock);
	data = mp_encode_uint(data, IPROTO_BALLOT_GC_VCLOCK);
	data = mp_encode_vclock_ignore0(data, &ballot->gc_vclock);
	size_t size = data - buf;
	assert(size <= max_size);

	iproto_header_encode(buf, IPROTO_OK, sync, schema_version,
			     size - IPROTO_HEADER_LEN);

	char *ptr = obuf_alloc(out, size);
	(void) ptr;
	assert(ptr == buf);
	return 0;
}

static void
mpstream_error_handler(void *error_ctx)
{
	*(bool *)error_ctx = true;
}

static void
mpstream_iproto_encode_error(struct mpstream *stream, const struct error *error)
{
	mpstream_encode_map(stream, 2);
	mpstream_encode_uint(stream, IPROTO_ERROR_24);
	mpstream_encode_str(stream, error->errmsg);
	mpstream_encode_uint(stream, IPROTO_ERROR);
	error_to_mpstream_noext(error, stream);
}

int
iproto_reply_error(struct obuf *out, const struct error *e, uint64_t sync,
		   uint32_t schema_version)
{
	char *header = (char *)obuf_alloc(out, IPROTO_HEADER_LEN);
	if (header == NULL)
		return -1;

	bool is_error = false;
	struct mpstream stream;
	mpstream_init(&stream, out, obuf_reserve_cb, obuf_alloc_cb,
		      mpstream_error_handler, &is_error);

	uint32_t used = obuf_size(out);
	mpstream_iproto_encode_error(&stream, e);
	mpstream_flush(&stream);

	uint32_t errcode = box_error_code(e);
	iproto_header_encode(header, iproto_encode_error(errcode), sync,
			     schema_version, obuf_size(out) - used);

	/* Malformed packet appears to be a lesser evil than abort. */
	return is_error ? -1 : 0;
}

void
iproto_write_error(int fd, const struct error *e, uint32_t schema_version,
		   uint64_t sync)
{
	bool is_error = false;
	struct mpstream stream;
	struct region *region = &fiber()->gc;
	mpstream_init(&stream, region, region_reserve_cb, region_alloc_cb,
		      mpstream_error_handler, &is_error);

	size_t region_svp = region_used(region);
	mpstream_iproto_encode_error(&stream, e);
	mpstream_flush(&stream);
	if (is_error)
		goto cleanup;

	size_t payload_size = region_used(region) - region_svp;
	char *payload = region_join(region, payload_size);
	if (payload == NULL)
		goto cleanup;

	uint32_t errcode = box_error_code(e);
	char header[IPROTO_HEADER_LEN];
	iproto_header_encode(header, iproto_encode_error(errcode), sync,
			     schema_version, payload_size);

	ssize_t unused;

	ERROR_INJECT_YIELD(ERRINJ_IPROTO_WRITE_ERROR_DELAY);
	unused = write(fd, header, sizeof(header));
	unused = write(fd, payload, payload_size);
	(void) unused;
cleanup:
	region_truncate(region, region_svp);
}

int
iproto_prepare_header(struct obuf *buf, struct obuf_svp *svp, size_t size)
{
	/**
	 * Reserve memory before taking a savepoint.
	 * This ensures that we get a contiguous chunk of memory
	 * and the savepoint is pointing at the beginning of it.
	 */
	void *ptr = obuf_reserve(buf, size);
	if (ptr == NULL) {
		diag_set(OutOfMemory, size, "obuf_reserve", "ptr");
		return -1;
	}
	*svp = obuf_create_svp(buf);
	ptr = obuf_alloc(buf, size);
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

int
xrow_decode_sql(const struct xrow_header *row, struct sql_request *request)
{
	if (row->bodycnt == 0) {
		diag_set(ClientError, ER_INVALID_MSGPACK, "missing request body");
		return 1;
	}
	assert(row->bodycnt == 1);
	const char *data = (const char *) row->body[0].iov_base;
	const char *end = data + row->body[0].iov_len;
	assert((end - data) > 0);

	if (mp_typeof(*data) != MP_MAP || mp_check_map(data, end) > 0) {
error:
		xrow_on_decode_err(row->body[0].iov_base, end, ER_INVALID_MSGPACK,
				   "packet body");
		return -1;
	}

	uint32_t map_size = mp_decode_map(&data);
	request->sql_text = NULL;
	request->bind = NULL;
	request->stmt_id = NULL;
	for (uint32_t i = 0; i < map_size; ++i) {
		uint8_t key = *data;
		if (key != IPROTO_SQL_BIND && key != IPROTO_SQL_TEXT &&
		    key != IPROTO_STMT_ID) {
			mp_check(&data, end);   /* skip the key */
			mp_check(&data, end);   /* skip the value */
			continue;
		}
		const char *value = ++data;     /* skip the key */
		if (mp_check(&data, end) != 0)  /* check the value */
			goto error;
		if (key == IPROTO_SQL_BIND)
			request->bind = value;
		else if (key == IPROTO_SQL_TEXT)
			request->sql_text = value;
		else
			request->stmt_id = value;
	}
	if (request->sql_text != NULL && request->stmt_id != NULL) {
		xrow_on_decode_err(row->body[0].iov_base, end, ER_INVALID_MSGPACK,
				   "SQL text and statement id are incompatible "\
				   "options in one request: choose one");
		return -1;
	}
	if (request->sql_text == NULL && request->stmt_id == NULL) {
		xrow_on_decode_err(row->body[0].iov_base, end,
				   ER_MISSING_REQUEST_FIELD,
				   tt_sprintf("%s or %s",
					      iproto_key_name(IPROTO_SQL_TEXT),
					      iproto_key_name(IPROTO_STMT_ID)));
		return -1;
	}
	if (data != end)
		goto error;
	return 0;
}

void
iproto_reply_sql(struct obuf *buf, struct obuf_svp *svp, uint64_t sync,
		 uint32_t schema_version)
{
	char *pos = (char *) obuf_svp_to_ptr(buf, svp);
	iproto_header_encode(pos, IPROTO_OK, sync, schema_version,
			     obuf_size(buf) - svp->used - IPROTO_HEADER_LEN);
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

	const char *start = NULL;
	const char *end = NULL;

	if (row->bodycnt == 0)
		goto done;

	assert(row->bodycnt == 1);
	const char *data = start = (const char *) row->body[0].iov_base;
	end = data + row->body[0].iov_len;
	assert((end - data) > 0);

	if (mp_typeof(*data) != MP_MAP || mp_check_map(data, end) > 0) {
error:
		xrow_on_decode_err(row->body[0].iov_base, end, ER_INVALID_MSGPACK,
				   "packet body");
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
		xrow_on_decode_err(row->body[0].iov_base, end, ER_INVALID_MSGPACK,
				   "packet end");
		return -1;
	}
done:
	if (key_map) {
		enum iproto_key key = (enum iproto_key) bit_ctz_u64(key_map);
		xrow_on_decode_err(start, end, ER_MISSING_REQUEST_FIELD,
				   iproto_key_name(key));
		return -1;
	}
	return 0;
}

static int
request_snprint(char *buf, int size, const struct request *request)
{
	int total = 0;
	SNPRINT(total, snprintf, buf, size, "{type: '%s', "
			"replica_id: %u, lsn: %lld, "
			"space_id: %u, index_id: %u",
			iproto_type_name(request->type),
			(unsigned) request->header->replica_id,
			(long long) request->header->lsn,
			(unsigned) request->space_id,
			(unsigned) request->index_id);
	if (request->key != NULL) {
		SNPRINT(total, snprintf, buf, size, ", key: ");
		SNPRINT(total, mp_snprint, buf, size, request->key);
	}
	if (request->tuple != NULL) {
		SNPRINT(total, snprintf, buf, size, ", tuple: ");
		SNPRINT(total, mp_snprint, buf, size, request->tuple);
	}
	if (request->ops != NULL) {
		SNPRINT(total, snprintf, buf, size, ", ops: ");
		SNPRINT(total, mp_snprint, buf, size, request->ops);
	}
	SNPRINT(total, snprintf, buf, size, "}");
	return total;
}

const char *
request_str(const struct request *request)
{
	char *buf = tt_static_buf();
	if (request_snprint(buf, TT_STATIC_BUF_LEN, request) < 0)
		return "<failed to format request>";
	return buf;
}

int
xrow_encode_dml(const struct request *request, struct region *region,
		struct iovec *iov)
{
	int iovcnt = 1;
	const int MAP_LEN_MAX = 40;
	uint32_t key_len = request->key_end - request->key;
	uint32_t ops_len = request->ops_end - request->ops;
	uint32_t tuple_meta_len = request->tuple_meta_end - request->tuple_meta;
	uint32_t tuple_len = request->tuple_end - request->tuple;
	uint32_t len = MAP_LEN_MAX + key_len + ops_len + tuple_meta_len +
		       tuple_len;
	char *begin = (char *) region_alloc(region, len);
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
		memcpy(pos, request->tuple, tuple_len);
		pos += tuple_len;
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

void
xrow_encode_synchro(struct xrow_header *row, char *body,
		    const struct synchro_request *req)
{
	assert(iproto_type_is_synchro_request(req->type));

	char *pos = body;

	pos = mp_encode_map(pos,
			    iproto_type_is_promote_request(req->type) ? 3 : 2);

	pos = mp_encode_uint(pos, IPROTO_REPLICA_ID);
	pos = mp_encode_uint(pos, req->replica_id);

	pos = mp_encode_uint(pos, IPROTO_LSN);
	pos = mp_encode_uint(pos, req->lsn);

	if (iproto_type_is_promote_request(req->type)) {
		pos = mp_encode_uint(pos, IPROTO_TERM);
		pos = mp_encode_uint(pos, req->term);
	}

	assert(pos - body < XROW_SYNCHRO_BODY_LEN_MAX);

	memset(row, 0, sizeof(*row));
	row->type = req->type;
	row->body[0].iov_base = body;
	row->body[0].iov_len = pos - body;
	row->bodycnt = 1;
}

int
xrow_decode_synchro(const struct xrow_header *row, struct synchro_request *req)
{
	if (row->bodycnt == 0) {
		diag_set(ClientError, ER_INVALID_MSGPACK, "request body");
		return -1;
	}

	assert(row->bodycnt == 1);

	const char * const data = (const char *)row->body[0].iov_base;
	const char * const end = data + row->body[0].iov_len;
	const char *d = data;
	if (mp_check(&d, end) != 0 || mp_typeof(*data) != MP_MAP) {
		xrow_on_decode_err(data, end, ER_INVALID_MSGPACK,
				   "request body");
		return -1;
	}

	memset(req, 0, sizeof(*req));
	d = data;
	uint32_t map_size = mp_decode_map(&d);
	for (uint32_t i = 0; i < map_size; i++) {
		enum mp_type type = mp_typeof(*d);
		if (type != MP_UINT) {
			mp_next(&d);
			mp_next(&d);
			continue;
		}
		uint8_t key = mp_decode_uint(&d);
		if (key >= IPROTO_KEY_MAX || iproto_key_type[key] != type) {
			xrow_on_decode_err(data, end, ER_INVALID_MSGPACK,
					   "request body");
			return -1;
		}
		switch (key) {
		case IPROTO_REPLICA_ID:
			req->replica_id = mp_decode_uint(&d);
			break;
		case IPROTO_LSN:
			req->lsn = mp_decode_uint(&d);
			break;
		case IPROTO_TERM:
			req->term = mp_decode_uint(&d);
			break;
		default:
			mp_next(&d);
		}
	}

	req->type = row->type;
	req->origin_id = row->replica_id;

	return 0;
}

int
xrow_encode_raft(struct xrow_header *row, struct region *region,
		 const struct raft_request *r)
{
	/*
	 * Terms is encoded always. Sometimes the rest can be even ignored if
	 * the term is too old.
	 */
	int map_size = 1;
	size_t size = mp_sizeof_uint(IPROTO_RAFT_TERM) +
		      mp_sizeof_uint(r->term);
	if (r->vote != 0) {
		++map_size;
		size += mp_sizeof_uint(IPROTO_RAFT_VOTE) +
			mp_sizeof_uint(r->vote);
	}
	if (r->state != 0) {
		++map_size;
		size += mp_sizeof_uint(IPROTO_RAFT_STATE) +
			mp_sizeof_uint(r->state);
	}
	if (r->vclock != NULL) {
		++map_size;
		size += mp_sizeof_uint(IPROTO_RAFT_VCLOCK) +
			mp_sizeof_vclock_ignore0(r->vclock);
	}
	size += mp_sizeof_map(map_size);

	char *buf = region_alloc(region, size);
	if (buf == NULL) {
		diag_set(OutOfMemory, size, "region_alloc", "buf");
		return -1;
	}
	memset(row, 0, sizeof(*row));
	row->type = IPROTO_RAFT;
	row->body[0].iov_base = buf;
	row->group_id = GROUP_LOCAL;
	row->bodycnt = 1;
	const char *begin = buf;

	buf = mp_encode_map(buf, map_size);
	buf = mp_encode_uint(buf, IPROTO_RAFT_TERM);
	buf = mp_encode_uint(buf, r->term);
	if (r->vote != 0) {
		buf = mp_encode_uint(buf, IPROTO_RAFT_VOTE);
		buf = mp_encode_uint(buf, r->vote);
	}
	if (r->state != 0) {
		buf = mp_encode_uint(buf, IPROTO_RAFT_STATE);
		buf = mp_encode_uint(buf, r->state);
	}
	if (r->vclock != NULL) {
		buf = mp_encode_uint(buf, IPROTO_RAFT_VCLOCK);
		buf = mp_encode_vclock_ignore0(buf, r->vclock);
	}
	row->body[0].iov_len = buf - begin;
	return 0;
}

int
xrow_decode_raft(const struct xrow_header *row, struct raft_request *r,
		 struct vclock *vclock)
{
	assert(row->type == IPROTO_RAFT);
	if (row->bodycnt != 1 || row->group_id != GROUP_LOCAL) {
		diag_set(ClientError, ER_INVALID_MSGPACK,
			 "malformed raft request");
		return -1;
	}
	memset(r, 0, sizeof(*r));

	const char *begin = row->body[0].iov_base;
	const char *end = begin + row->body[0].iov_len;
	const char *pos = begin;
	uint32_t map_size = mp_decode_map(&pos);
	for (uint32_t i = 0; i < map_size; ++i)
	{
		if (mp_typeof(*pos) != MP_UINT)
			goto bad_msgpack;
		uint64_t key = mp_decode_uint(&pos);
		switch (key) {
		case IPROTO_RAFT_TERM:
			if (mp_typeof(*pos) != MP_UINT)
				goto bad_msgpack;
			r->term = mp_decode_uint(&pos);
			break;
		case IPROTO_RAFT_VOTE:
			if (mp_typeof(*pos) != MP_UINT)
				goto bad_msgpack;
			r->vote = mp_decode_uint(&pos);
			break;
		case IPROTO_RAFT_STATE:
			if (mp_typeof(*pos) != MP_UINT)
				goto bad_msgpack;
			r->state = mp_decode_uint(&pos);
			break;
		case IPROTO_RAFT_VCLOCK:
			r->vclock = vclock;
			if (r->vclock == NULL)
				mp_next(&pos);
			else if (mp_decode_vclock_ignore0(&pos, vclock) != 0)
				goto bad_msgpack;
			break;
		default:
			mp_next(&pos);
			break;
		}
	}
	return 0;

bad_msgpack:
	xrow_on_decode_err(begin, end, ER_INVALID_MSGPACK, "raft body");
	return -1;
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
	store_u32(data, mp_bswap_u32(len));

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
		xrow_on_decode_err(row->body[0].iov_base, end, ER_INVALID_MSGPACK,
				   "packet body");
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
		xrow_on_decode_err(row->body[0].iov_base, end, ER_INVALID_MSGPACK,
				   "packet end");
		return -1;
	}
	if (row->type == IPROTO_EVAL) {
		if (request->expr == NULL) {
			xrow_on_decode_err(row->body[0].iov_base, end, ER_MISSING_REQUEST_FIELD,
					   iproto_key_name(IPROTO_EXPR));
			return -1;
		}
	} else if (request->name == NULL) {
		assert(row->type == IPROTO_CALL_16 ||
		       row->type == IPROTO_CALL);
		xrow_on_decode_err(row->body[0].iov_base, end, ER_MISSING_REQUEST_FIELD,
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
		xrow_on_decode_err(row->body[0].iov_base, end, ER_INVALID_MSGPACK,
				   "packet body");
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
		xrow_on_decode_err(row->body[0].iov_base, end, ER_INVALID_MSGPACK,
				   "packet end");
		return -1;
	}
	if (request->user_name == NULL) {
		xrow_on_decode_err(row->body[0].iov_base, end, ER_MISSING_REQUEST_FIELD,
				   iproto_key_name(IPROTO_USER_NAME));
		return -1;
	}
	if (request->scramble == NULL) {
		xrow_on_decode_err(row->body[0].iov_base, end, ER_MISSING_REQUEST_FIELD,
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
	bool is_stack_parsed = false;
	for (uint32_t i = 0; i < map_size; i++) {
		if (mp_typeof(*pos) != MP_UINT) {
			mp_next(&pos); /* key */
			mp_next(&pos); /* value */
			continue;
		}
		uint8_t key = mp_decode_uint(&pos);
		if (key == IPROTO_ERROR_24 && mp_typeof(*pos) == MP_STR) {
			/*
			 * Obsolete way of sending error responses.
			 * To be deprecated but still should be supported
			 * to not break backward compatibility.
			 */
			uint32_t len;
			const char *str = mp_decode_str(&pos, &len);
			if (!is_stack_parsed) {
				snprintf(error, sizeof(error), "%.*s", len, str);
				box_error_set(__FILE__, __LINE__, code, error);
			}
		} else if (key == IPROTO_ERROR) {
			struct error *e = error_unpack_unsafe(&pos);
			if (e == NULL)
				goto error;
			is_stack_parsed = true;
			diag_set_error(diag_get(), e);
		} else {
			mp_next(&pos);
			continue;
		}
	}
	return;

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
	ballot->is_ro_cfg = false;
	ballot->is_ro = false;
	ballot->is_anon = false;
	ballot->is_booted = true;
	vclock_create(&ballot->vclock);

	const char *start = NULL;
	const char *end = NULL;

	if (row->bodycnt == 0)
		goto err;
	assert(row->bodycnt == 1);

	const char *data = start = (const char *) row->body[0].iov_base;
	end = data + row->body[0].iov_len;
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
		case IPROTO_BALLOT_IS_RO_CFG:
			if (mp_typeof(*data) != MP_BOOL)
				goto err;
			ballot->is_ro_cfg = mp_decode_bool(&data);
			break;
		case IPROTO_BALLOT_IS_RO:
			if (mp_typeof(*data) != MP_BOOL)
				goto err;
			ballot->is_ro = mp_decode_bool(&data);
			break;
		case IPROTO_BALLOT_IS_ANON:
			if (mp_typeof(*data) != MP_BOOL)
				goto err;
			ballot->is_anon = mp_decode_bool(&data);
			break;
		case IPROTO_BALLOT_VCLOCK:
			if (mp_decode_vclock_ignore0(&data,
						     &ballot->vclock) != 0)
				goto err;
			break;
		case IPROTO_BALLOT_GC_VCLOCK:
			if (mp_decode_vclock_ignore0(&data,
						     &ballot->gc_vclock) != 0)
				goto err;
			break;
		case IPROTO_BALLOT_IS_BOOTED:
			if (mp_typeof(*data) != MP_BOOL)
				goto err;
			ballot->is_booted = mp_decode_bool(&data);
			break;
		default:
			mp_next(&data);
		}
	}
	return 0;
err:
	xrow_on_decode_err(start, end, ER_INVALID_MSGPACK, "packet body");
	return -1;
}

int
xrow_encode_register(struct xrow_header *row,
		     const struct tt_uuid *instance_uuid,
		     const struct vclock *vclock)
{
	memset(row, 0, sizeof(*row));
	size_t size = mp_sizeof_map(2) +
		      mp_sizeof_uint(IPROTO_INSTANCE_UUID) +
		      mp_sizeof_str(UUID_STR_LEN) +
		      mp_sizeof_uint(IPROTO_VCLOCK) +
		      mp_sizeof_vclock_ignore0(vclock);
	char *buf = (char *) region_alloc(&fiber()->gc, size);
	if (buf == NULL) {
		diag_set(OutOfMemory, size, "region_alloc", "buf");
		return -1;
	}
	char *data = buf;
	data = mp_encode_map(data, 2);
	data = mp_encode_uint(data, IPROTO_INSTANCE_UUID);
	data = xrow_encode_uuid(data, instance_uuid);
	data = mp_encode_uint(data, IPROTO_VCLOCK);
	data = mp_encode_vclock_ignore0(data, vclock);
	assert(data <= buf + size);
	row->body[0].iov_base = buf;
	row->body[0].iov_len = (data - buf);
	row->bodycnt = 1;
	row->type = IPROTO_REGISTER;
	return 0;
}

int
xrow_encode_subscribe(struct xrow_header *row,
		      const struct tt_uuid *replicaset_uuid,
		      const struct tt_uuid *instance_uuid,
		      const struct vclock *vclock, bool anon,
		      uint32_t id_filter)
{
	memset(row, 0, sizeof(*row));
	size_t size = XROW_BODY_LEN_MAX +
		      mp_sizeof_vclock_ignore0(vclock);
	char *buf = (char *) region_alloc(&fiber()->gc, size);
	if (buf == NULL) {
		diag_set(OutOfMemory, size, "region_alloc", "buf");
		return -1;
	}
	char *data = buf;
	int filter_size = bit_count_u32(id_filter);
	data = mp_encode_map(data, filter_size != 0 ? 6 : 5);
	data = mp_encode_uint(data, IPROTO_CLUSTER_UUID);
	data = xrow_encode_uuid(data, replicaset_uuid);
	data = mp_encode_uint(data, IPROTO_INSTANCE_UUID);
	data = xrow_encode_uuid(data, instance_uuid);
	data = mp_encode_uint(data, IPROTO_VCLOCK);
	data = mp_encode_vclock_ignore0(data, vclock);
	data = mp_encode_uint(data, IPROTO_SERVER_VERSION);
	data = mp_encode_uint(data, tarantool_version_id());
	data = mp_encode_uint(data, IPROTO_REPLICA_ANON);
	data = mp_encode_bool(data, anon);
	if (filter_size != 0) {
		data = mp_encode_uint(data, IPROTO_ID_FILTER);
		data = mp_encode_array(data, filter_size);
		struct bit_iterator it;
		bit_iterator_init(&it, &id_filter, sizeof(id_filter),
				  true);
		for (size_t id = bit_iterator_next(&it); id < VCLOCK_MAX;
		     id = bit_iterator_next(&it)) {
			data = mp_encode_uint(data, id);
		}
	}
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
		      uint32_t *version_id, bool *anon,
		      uint32_t *id_filter)
{
	if (row->bodycnt == 0) {
		diag_set(ClientError, ER_INVALID_MSGPACK, "request body");
		return -1;
	}
	assert(row->bodycnt == 1);
	const char * const data = (const char *) row->body[0].iov_base;
	const char *end = data + row->body[0].iov_len;
	const char *d = data;
	if (mp_check(&d, end) != 0 || mp_typeof(*data) != MP_MAP) {
		xrow_on_decode_err(data, end, ER_INVALID_MSGPACK,
				   "request body");
		return -1;
	}

	if (anon)
		*anon = false;
	if (id_filter)
		*id_filter = 0;
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
			if (xrow_decode_uuid(&d, replicaset_uuid) != 0) {
				xrow_on_decode_err(data, end, ER_INVALID_MSGPACK,
						   "UUID");
				return -1;
			}
			break;
		case IPROTO_INSTANCE_UUID:
			if (instance_uuid == NULL)
				goto skip;
			if (xrow_decode_uuid(&d, instance_uuid) != 0) {
				xrow_on_decode_err(data, end, ER_INVALID_MSGPACK,
						   "UUID");
				return -1;
			}
			break;
		case IPROTO_VCLOCK:
			if (vclock == NULL)
				goto skip;
			if (mp_decode_vclock_ignore0(&d, vclock) != 0) {
				xrow_on_decode_err(data, end, ER_INVALID_MSGPACK,
						   "invalid VCLOCK");
				return -1;
			}
			break;
		case IPROTO_SERVER_VERSION:
			if (version_id == NULL)
				goto skip;
			if (mp_typeof(*d) != MP_UINT) {
				xrow_on_decode_err(data, end, ER_INVALID_MSGPACK,
						   "invalid VERSION");
				return -1;
			}
			*version_id = mp_decode_uint(&d);
			break;
		case IPROTO_REPLICA_ANON:
			if (anon == NULL)
				goto skip;
			if (mp_typeof(*d) != MP_BOOL) {
				xrow_on_decode_err(data, end, ER_INVALID_MSGPACK,
						   "invalid REPLICA_ANON flag");
				return -1;
			}
			*anon = mp_decode_bool(&d);
			break;
		case IPROTO_ID_FILTER:
			if (id_filter == NULL)
				goto skip;
			if (mp_typeof(*d) != MP_ARRAY) {
id_filter_decode_err:		xrow_on_decode_err(data, end, ER_INVALID_MSGPACK,
						   "invalid ID_FILTER");
				return -1;
			}
			uint32_t len = mp_decode_array(&d);
			for (uint32_t i = 0; i < len; ++i) {
				if (mp_typeof(*d) != MP_UINT)
					goto id_filter_decode_err;
				uint64_t val = mp_decode_uint(&d);
				if (val >= VCLOCK_MAX)
					goto id_filter_decode_err;
				*id_filter |= 1 << val;
			}
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
	size_t size = 8 + mp_sizeof_vclock_ignore0(vclock);
	char *buf = (char *) region_alloc(&fiber()->gc, size);
	if (buf == NULL) {
		diag_set(OutOfMemory, size, "region_alloc", "buf");
		return -1;
	}
	char *data = buf;
	data = mp_encode_map(data, 1);
	data = mp_encode_uint(data, IPROTO_VCLOCK);
	data = mp_encode_vclock_ignore0(data, vclock);
	assert(data <= buf + size);
	row->body[0].iov_base = buf;
	row->body[0].iov_len = (data - buf);
	row->bodycnt = 1;
	row->type = IPROTO_OK;
	return 0;
}

int
xrow_encode_subscribe_response(struct xrow_header *row,
			       const struct tt_uuid *replicaset_uuid,
			       const struct vclock *vclock)
{
	memset(row, 0, sizeof(*row));
	size_t size = mp_sizeof_map(2) +
		      mp_sizeof_uint(IPROTO_VCLOCK) +
		      mp_sizeof_vclock_ignore0(vclock) +
		      mp_sizeof_uint(IPROTO_CLUSTER_UUID) +
		      mp_sizeof_str(UUID_STR_LEN);
	char *buf = (char *) region_alloc(&fiber()->gc, size);
	if (buf == NULL) {
		diag_set(OutOfMemory, size, "region_alloc", "buf");
		return -1;
	}
	char *data = buf;
	data = mp_encode_map(data, 2);
	data = mp_encode_uint(data, IPROTO_VCLOCK);
	data = mp_encode_vclock_ignore0(data, vclock);
	data = mp_encode_uint(data, IPROTO_CLUSTER_UUID);
	data = xrow_encode_uuid(data, replicaset_uuid);
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
