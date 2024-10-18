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
#include "iostream.h"
#include "version.h"
#include "tt_static.h"
#include "trivia/util.h"
#include "error.h"
#include "mp_error.h"
#include "mp_extension_types.h"
#include "iproto_constants.h"
#include "iproto_features.h"
#include "mpstream/mpstream.h"
#include "errinj.h"
#include "core/tweaks.h"

/**
 * Controls whether `IPROTO_FEATURE_CALL_RET_TUPLE_EXTENSION` feature bit is set
 * in `IPROTO_ID` request responses.
 */
bool box_tuple_extension;
TWEAK_BOOL(box_tuple_extension);

/**
 * Min length of the salt sent in a greeting message.
 * Since it's used for authentication, it must be >= AUTH_SALT_SIZE.
 */
enum { GREETING_SALT_LEN_MIN = 20 };

static_assert(IPROTO_DATA < 0x7f && IPROTO_METADATA < 0x7f &&
	      IPROTO_SQL_INFO < 0x7f, "encoded IPROTO_BODY keys must fit into "\
	      "one byte");

uint32_t
mp_sizeof_vclock_ignore0(const struct vclock *vclock)
{
	uint32_t size = vclock_size_ignore0(vclock);
	return mp_sizeof_map(size) + size * (mp_sizeof_uint(UINT32_MAX) +
					     mp_sizeof_uint(UINT64_MAX));
}

static inline uint32_t
mp_sizeof_vclock(const struct vclock *vclock)
{
	uint32_t size = vclock_size(vclock);
	return mp_sizeof_map(size) + size * (mp_sizeof_uint(UINT32_MAX) +
					     mp_sizeof_uint(UINT64_MAX));
}

static inline char *
mp_encode_vclock_impl(char *data, const struct vclock *vclock, bool ignore0)
{
	struct vclock_iterator it;
	vclock_iterator_init(&it, vclock);
	struct vclock_c replica;
	replica = vclock_iterator_next(&it);
	if (replica.id == 0 && ignore0)
		replica = vclock_iterator_next(&it);
	for ( ; replica.id < VCLOCK_MAX; replica = vclock_iterator_next(&it)) {
		data = mp_encode_uint(data, replica.id);
		data = mp_encode_uint(data, replica.lsn);
	}
	return data;
}

char *
mp_encode_vclock_ignore0(char *data, const struct vclock *vclock)
{
	data = mp_encode_map(data, vclock_size_ignore0(vclock));
	return mp_encode_vclock_impl(data, vclock, true);
}

static inline char *
mp_encode_vclock(char *data, const struct vclock *vclock)
{
	data = mp_encode_map(data, vclock_size(vclock));
	return mp_encode_vclock_impl(data, vclock, false);
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
		if (lsn < 0 || id >= VCLOCK_MAX)
			return -1;
		if (lsn > 0)
			vclock_follow(vclock, id, lsn);
	}
	return 0;
}

int
mp_decode_vclock_ignore0(const char **data, struct vclock *vclock)
{
	if (mp_decode_vclock(data, vclock) != 0)
		return -1;
	vclock_reset(vclock, 0, 0);
	return 0;
}

/**
 * If log_level is 'verbose' or greater,
 * dump the corrupted row contents in hex to the log.
 *
 * The format is similar to the xxd utility.
 */
static void
dump_row_hex(const char *start, const char *end) {
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

/**
 * Sets diag and dumps the row body if present.
 */
#define xrow_on_decode_err(row, what, desc_str) do {\
	if (what == ER_ILLEGAL_PARAMS) \
		diag_set(IllegalParams, desc_str);\
	else \
		diag_set(ClientError, what, desc_str);\
	if (row->bodycnt > 0) {\
		dump_row_hex(row->body[0].iov_base,\
			     row->body[0].iov_base + row->body[0].iov_len);\
	}\
} while (0)

int
xrow_decode(struct xrow_header *header, const char **pos,
	    const char *end, bool end_is_exact)
{
	memset(header, 0, sizeof(struct xrow_header));
	const char *tmp = *pos;
	const char * const start = *pos;
	if (mp_check(&tmp, end) != 0)
		goto bad_header;
	if (mp_typeof(**pos) != MP_MAP)
		goto bad_header;
	header->header = start;
	header->header_end = tmp;
	bool has_tsn = false;
	uint32_t flags = 0;

	uint32_t size = mp_decode_map(pos);
	for (uint32_t i = 0; i < size; i++) {
		if (mp_typeof(**pos) != MP_UINT)
			goto bad_header;
		uint64_t key = mp_decode_uint(pos);
		if (key < iproto_key_MAX &&
		    iproto_key_type[key] != mp_typeof(**pos))
			goto bad_header;
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
		case IPROTO_STREAM_ID:
			header->stream_id = mp_decode_uint(pos);
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
		if (mp_check(pos, end))
			goto bad_body;
		header->bodycnt = 1;
		header->body[0].iov_base = (void *) body;
		header->body[0].iov_len = *pos - body;
	}
	if (end_is_exact && *pos < end)
		goto bad_body;
	return 0;
bad_header:
	diag_set(ClientError, ER_INVALID_MSGPACK, "packet header");
	goto dump;
bad_body:
	diag_set(ClientError, ER_INVALID_MSGPACK, "packet body");
dump:
	dump_row_hex(start, end);
	return -1;
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

/** Decode an optional node name. */
static inline int
xrow_decode_node_name(const char **pos, char *out)
{
	enum mp_type type = mp_typeof(**pos);
	if (type != MP_STR)
		return -1;
	uint32_t len;
	const char *str = mp_decode_str(pos, &len);
	if (!node_name_is_valid_n(str, len))
		return -1;
	memcpy(out, str, len);
	out[len] = 0;
	return 0;
}

size_t
xrow_header_encode(const struct xrow_header *header, uint64_t sync, char *data)
{
	/* Header */
	char *d = data + 1; /* Skip 1 byte for MP_MAP */
	int map_size = 0;

	ERROR_INJECT(ERRINJ_XLOG_WRITE_INVALID_KEY, {
		d = mp_encode_bool(d, true);
		d = mp_encode_uint(d, 1);
		map_size++;
	});
	ERROR_INJECT(ERRINJ_XLOG_WRITE_INVALID_VALUE, {
		d = mp_encode_uint(d, IPROTO_KEY);
		d = mp_encode_uint(d, 1);
		map_size++;
	});
	ERROR_INJECT(ERRINJ_XLOG_WRITE_UNKNOWN_KEY, {
		d = mp_encode_uint(d, 666);
		d = mp_encode_uint(d, 1);
		map_size++;
	});

	uint32_t type = header->type;
	ERROR_INJECT(ERRINJ_XLOG_WRITE_UNKNOWN_TYPE, {
		type = 777;
	});
	if (true) {
		d = mp_encode_uint(d, IPROTO_REQUEST_TYPE);
		d = mp_encode_uint(d, type);
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
	if (header->stream_id != 0) {
		d = mp_encode_uint(d, IPROTO_STREAM_ID);
		d = mp_encode_uint(d, header->stream_id);
		map_size++;
	}
	if (flags_to_encode != 0) {
		d = mp_encode_uint(d, IPROTO_FLAGS);
		d = mp_encode_uint(d, flags_to_encode);
		map_size++;
	}
	assert(d <= data + XROW_HEADER_LEN_MAX);
	mp_encode_map(data, map_size);
	ERROR_INJECT(ERRINJ_XLOG_WRITE_INVALID_HEADER, {
		mp_encode_array(data, 0);
	});
	ERROR_INJECT(ERRINJ_XLOG_WRITE_CORRUPTED_HEADER, {
		*data = 0xc1;
	});

	return d - data;
}

void
xrow_encode(const struct xrow_header *header, uint64_t sync,
	    size_t fixheader_len, struct iovec *out, int *iovcnt)
{
	/* allocate memory for sign + header */
	out->iov_base = xregion_alloc(&fiber()->gc, XROW_HEADER_LEN_MAX +
				      fixheader_len);
	char *data = (char *)out->iov_base + fixheader_len;

	out->iov_len = fixheader_len + xrow_header_encode(header, sync, data);
	out++;

	memcpy(out, header->body, sizeof(*out) * header->bodycnt);
	*iovcnt = 1 + header->bodycnt;
	assert(*iovcnt <= XROW_IOVMAX);
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
	uint64_t v_schema_version;              /* schema_version */
};

static_assert(sizeof(struct iproto_header_bin) == IPROTO_HEADER_LEN,
	      "sizeof(iproto_header_bin)");

void
iproto_header_encode(char *out, uint16_t type, uint64_t sync,
		     uint64_t schema_version, uint32_t body_length)
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
	header.m_schema_version = 0xcf;
	header.v_schema_version = mp_bswap_u64(schema_version);
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

static const struct iproto_body_bin iproto_body_bin_with_position = {
	0x82, IPROTO_DATA, 0xdd, 0
};

/** Return a 4-byte numeric error code, with status flags. */
static inline uint32_t
iproto_encode_error(uint32_t error)
{
	return error | IPROTO_TYPE_ERROR;
}

void
iproto_reply_ok(struct obuf *out, uint64_t sync, uint64_t schema_version)
{
	char *buf = xobuf_alloc(out, IPROTO_HEADER_LEN + 1);
	iproto_header_encode(buf, IPROTO_OK, sync, schema_version, 1);
	buf[IPROTO_HEADER_LEN] = 0x80; /* empty MessagePack Map */
}

void
iproto_reply_id(struct obuf *out, const char *auth_type,
		uint64_t sync, uint64_t schema_version)
{
	assert(auth_type != NULL);
	uint32_t auth_type_len = strlen(auth_type);
	unsigned version = IPROTO_CURRENT_VERSION;
	struct iproto_features features = IPROTO_CURRENT_FEATURES;
	if (!box_tuple_extension) {
		iproto_features_clear(&features,
				      IPROTO_FEATURE_CALL_RET_TUPLE_EXTENSION);
		iproto_features_clear(&features,
				      IPROTO_FEATURE_CALL_ARG_TUPLE_EXTENSION);
	}
#ifndef NDEBUG
	struct errinj *errinj;
	errinj = errinj(ERRINJ_IPROTO_SET_VERSION, ERRINJ_INT);
	if (errinj->iparam >= 0)
		version = errinj->iparam;
	errinj = errinj(ERRINJ_IPROTO_FLIP_FEATURE, ERRINJ_INT);
	if (errinj->iparam >= 0 && errinj->iparam < iproto_feature_id_MAX) {
		int feature_id = errinj->iparam;
		if (iproto_features_test(&features, feature_id))
			iproto_features_clear(&features, feature_id);
		else
			iproto_features_set(&features, feature_id);
	}
#endif

	size_t size = IPROTO_HEADER_LEN;
	size += mp_sizeof_map(3);
	size += mp_sizeof_uint(IPROTO_VERSION);
	size += mp_sizeof_uint(version);
	size += mp_sizeof_uint(IPROTO_FEATURES);
	size += mp_sizeof_iproto_features(&features);
	size += mp_sizeof_uint(IPROTO_AUTH_TYPE);
	size += mp_sizeof_str(auth_type_len);

	char *buf = xobuf_alloc(out, size);
	char *data = buf + IPROTO_HEADER_LEN;
	data = mp_encode_map(data, 3);
	data = mp_encode_uint(data, IPROTO_VERSION);
	data = mp_encode_uint(data, version);
	data = mp_encode_uint(data, IPROTO_FEATURES);
	data = mp_encode_iproto_features(data, &features);
	data = mp_encode_uint(data, IPROTO_AUTH_TYPE);
	data = mp_encode_str(data, auth_type, auth_type_len);
	assert(size == (size_t)(data - buf));

	iproto_header_encode(buf, IPROTO_OK, sync, schema_version,
			     size - IPROTO_HEADER_LEN);
}

void
iproto_reply_vclock(struct obuf *out, const struct vclock *vclock,
		    uint64_t sync, uint64_t schema_version)
{
	size_t max_size = IPROTO_HEADER_LEN + mp_sizeof_map(1) +
		mp_sizeof_uint(UINT32_MAX) + mp_sizeof_vclock_ignore0(vclock);

	char *buf = xobuf_reserve(out, max_size);
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
}

size_t
mp_sizeof_ballot_max(const struct ballot *ballot)
{
	int registered_uuids_size = ballot->registered_replica_uuids_size;
	return mp_sizeof_map(1) + mp_sizeof_uint(IPROTO_BALLOT) +
	       mp_sizeof_map(10) + mp_sizeof_uint(IPROTO_BALLOT_IS_RO_CFG) +
	       mp_sizeof_bool(ballot->is_ro_cfg) +
	       mp_sizeof_uint(IPROTO_BALLOT_IS_RO) +
	       mp_sizeof_bool(ballot->is_ro) +
	       mp_sizeof_uint(IPROTO_BALLOT_IS_ANON) +
	       mp_sizeof_bool(ballot->is_anon) +
	       mp_sizeof_uint(IPROTO_BALLOT_IS_BOOTED) +
	       mp_sizeof_bool(ballot->is_booted) +
	       mp_sizeof_uint(IPROTO_BALLOT_VCLOCK) +
	       mp_sizeof_vclock_ignore0(&ballot->vclock) +
	       mp_sizeof_uint(IPROTO_BALLOT_GC_VCLOCK) +
	       mp_sizeof_vclock_ignore0(&ballot->gc_vclock) +
	       mp_sizeof_uint(IPROTO_BALLOT_CAN_LEAD) +
	       mp_sizeof_bool(ballot->can_lead) +
	       mp_sizeof_uint(IPROTO_BALLOT_BOOTSTRAP_LEADER_UUID) +
	       mp_sizeof_str(UUID_STR_LEN) +
	       mp_sizeof_uint(IPROTO_BALLOT_INSTANCE_NAME) +
	       mp_sizeof_str(NODE_NAME_LEN_MAX) +
	       mp_sizeof_uint(IPROTO_BALLOT_REGISTERED_REPLICA_UUIDS) +
	       mp_sizeof_array(registered_uuids_size) +
	       registered_uuids_size * mp_sizeof_str(UUID_STR_LEN);

}

char *
mp_encode_ballot(char *data, const struct ballot *ballot)
{
	data = mp_encode_map(data, 1);
	data = mp_encode_uint(data, IPROTO_BALLOT);
	bool has_name = *ballot->instance_name != '\0';
	data = mp_encode_map(data, has_name ? 10 : 9);
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
	data = mp_encode_uint(data, IPROTO_BALLOT_CAN_LEAD);
	data = mp_encode_bool(data, ballot->can_lead);
	data = mp_encode_uint(data, IPROTO_BALLOT_BOOTSTRAP_LEADER_UUID);
	data = xrow_encode_uuid(data, &ballot->bootstrap_leader_uuid);
	if (has_name) {
		data = mp_encode_uint(data, IPROTO_BALLOT_INSTANCE_NAME);
		data = mp_encode_str0(data, ballot->instance_name);
	}
	data = mp_encode_uint(data, IPROTO_BALLOT_REGISTERED_REPLICA_UUIDS);
	data = mp_encode_array(data, ballot->registered_replica_uuids_size);
	for (int i = 0; i < ballot->registered_replica_uuids_size; i++) {
		const struct tt_uuid *uu = &ballot->registered_replica_uuids[i];
		data = xrow_encode_uuid(data, uu);
	}
	return data;
}

void
iproto_reply_vote(struct obuf *out, const struct ballot *ballot,
		  uint64_t sync, uint64_t schema_version)
{
	size_t max_size = IPROTO_HEADER_LEN + mp_sizeof_ballot_max(ballot);

	char *buf = xobuf_reserve(out, max_size);
	char *data = buf + IPROTO_HEADER_LEN;
	data = mp_encode_ballot(data, ballot);
	size_t size = data - buf;
	assert(size <= max_size);

	iproto_header_encode(buf, IPROTO_OK, sync, schema_version,
			     size - IPROTO_HEADER_LEN);

	char *ptr = obuf_alloc(out, size);
	(void) ptr;
	assert(ptr == buf);
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

void
iproto_reply_error(struct obuf *out, const struct error *e, uint64_t sync,
		   uint64_t schema_version)
{
	char *header = xobuf_alloc(out, IPROTO_HEADER_LEN);

	struct mpstream stream;
	mpstream_init(&stream, out, obuf_reserve_cb, obuf_alloc_cb,
		      mpstream_panic_cb, NULL);

	uint32_t used = obuf_size(out);
	mpstream_iproto_encode_error(&stream, e);
	mpstream_flush(&stream);

	uint32_t errcode = box_error_code(e);
	iproto_header_encode(header, iproto_encode_error(errcode), sync,
			     schema_version, obuf_size(out) - used);
}

void
iproto_do_write_error(struct iostream *io, const struct error *e,
		      uint64_t schema_version, uint64_t sync)
{
	struct mpstream stream;
	struct region *region = &fiber()->gc;
	mpstream_init(&stream, region, region_reserve_cb, region_alloc_cb,
		      mpstream_panic_cb, NULL);

	size_t region_svp = region_used(region);
	mpstream_iproto_encode_error(&stream, e);
	mpstream_flush(&stream);

	size_t payload_size = region_used(region) - region_svp;
	char *payload = xregion_join(region, payload_size);

	uint32_t errcode = box_error_code(e);
	char header[IPROTO_HEADER_LEN];
	iproto_header_encode(header, iproto_encode_error(errcode), sync,
			     schema_version, payload_size);

	ssize_t unused;

	ERROR_INJECT_YIELD(ERRINJ_IPROTO_WRITE_ERROR_DELAY);
	unused = iostream_write(io, header, sizeof(header));
	unused = iostream_write(io, payload, payload_size);
	(void) unused;

	region_truncate(region, region_svp);
}

void
iproto_prepare_header(struct obuf *buf, struct obuf_svp *svp, size_t size)
{
	/**
	 * Reserve memory before taking a savepoint.
	 * This ensures that we get a contiguous chunk of memory
	 * and the savepoint is pointing at the beginning of it.
	 */
	xobuf_reserve(buf, size);
	*svp = obuf_create_svp(buf);
	void *ptr = obuf_alloc(buf, size);
	assert(ptr != NULL);
	(void)ptr;
}

/** Reply select with IPROTO_DATA. */
void
iproto_reply_select(struct obuf *buf, struct obuf_svp *svp, uint64_t sync,
		    uint64_t schema_version, uint32_t count,
		    bool box_tuple_as_ext)
{
	char *pos = (char *) obuf_svp_to_ptr(buf, svp);
	iproto_header_encode(pos, IPROTO_OK, sync, schema_version,
			     obuf_size(buf) - svp->used -
			     IPROTO_HEADER_LEN);

	struct iproto_body_bin body = iproto_body_bin;
	body.m_body += box_tuple_as_ext;
	body.v_data_len = mp_bswap_u32(count);

	memcpy(pos + IPROTO_HEADER_LEN, &body, sizeof(body));
}

/** Reply select with IPROTO_DATA and IPROTO_POSITION. */
void
iproto_reply_select_with_position(struct obuf *buf, struct obuf_svp *svp,
				  uint64_t sync, uint32_t schema_version,
				  uint32_t count, const char *packed_pos,
				  const char *packed_pos_end,
				  bool box_tuple_as_ext)
{
	size_t packed_pos_size = packed_pos_end - packed_pos;
	size_t key_size = mp_sizeof_uint(IPROTO_POSITION);
	size_t alloc_size = key_size + mp_sizeof_strl(packed_pos_size);
	char *ptr = xobuf_alloc(buf, alloc_size);
	ptr = mp_encode_uint(ptr, IPROTO_POSITION);
	mp_encode_strl(ptr, packed_pos_size);
	xobuf_dup(buf, packed_pos, packed_pos_size);

	char *pos = (char *)obuf_svp_to_ptr(buf, svp);
	iproto_header_encode(pos, IPROTO_OK, sync, schema_version,
			     obuf_size(buf) - svp->used -
			     IPROTO_HEADER_LEN);

	struct iproto_body_bin body = iproto_body_bin_with_position;
	body.m_body += box_tuple_as_ext;
	body.v_data_len = mp_bswap_u32(count);

	memcpy(pos + IPROTO_HEADER_LEN, &body, sizeof(body));
}

int
xrow_decode_sql(const struct xrow_header *row, struct sql_request *request)
{
	assert(row->type == IPROTO_EXECUTE || row->type == IPROTO_PREPARE);
	if (row->bodycnt == 0) {
		diag_set(ClientError, ER_INVALID_MSGPACK, "missing request body");
		return 1;
	}
	assert(row->bodycnt == 1);
	const char *data = (const char *) row->body[0].iov_base;
	if (mp_typeof(*data) != MP_MAP) {
		xrow_on_decode_err(row, ER_INVALID_MSGPACK, "packet body");
		return -1;
	}

	uint32_t map_size = mp_decode_map(&data);
	request->execute = row->type == IPROTO_EXECUTE;
	request->sql_text = NULL;
	request->bind = NULL;
	request->stmt_id = NULL;
	for (uint32_t i = 0; i < map_size; ++i) {
		uint8_t key = *data;
		if (key != IPROTO_SQL_BIND && key != IPROTO_SQL_TEXT &&
		    key != IPROTO_STMT_ID) {
			mp_next(&data);         /* skip the key */
			mp_next(&data);         /* skip the value */
			continue;
		}
		const char *value = ++data;     /* skip the key */
		mp_next(&data);                 /* skip the value */
		if (key == IPROTO_SQL_BIND)
			request->bind = value;
		else if (key == IPROTO_SQL_TEXT)
			request->sql_text = value;
		else
			request->stmt_id = value;
	}
	if (request->sql_text != NULL && request->stmt_id != NULL) {
		xrow_on_decode_err(row, ER_INVALID_MSGPACK,
				   "SQL text and statement id are incompatible "
				   "options in one request: choose one");
		return -1;
	}
	if (request->sql_text == NULL && request->stmt_id == NULL) {
		xrow_on_decode_err(row, ER_MISSING_REQUEST_FIELD,
				   tt_sprintf("%s or %s",
					      iproto_key_name(IPROTO_SQL_TEXT),
					      iproto_key_name(IPROTO_STMT_ID)));
		return -1;
	}
	return 0;
}

void
iproto_reply_sql(struct obuf *buf, struct obuf_svp *svp, uint64_t sync,
		 uint64_t schema_version)
{
	char *pos = (char *) obuf_svp_to_ptr(buf, svp);
	iproto_header_encode(pos, IPROTO_OK, sync, schema_version,
			     obuf_size(buf) - svp->used - IPROTO_HEADER_LEN);
}

void
iproto_reply_chunk(struct obuf *buf, struct obuf_svp *svp, uint64_t sync,
		   uint64_t schema_version)
{
	char *pos = (char *) obuf_svp_to_ptr(buf, svp);
	iproto_header_encode(pos, IPROTO_CHUNK, sync, schema_version,
			     obuf_size(buf) - svp->used - IPROTO_HEADER_LEN);
	struct iproto_body_bin body = iproto_body_bin;
	body.v_data_len = mp_bswap_u32(1);
	memcpy(pos + IPROTO_HEADER_LEN, &body, sizeof(body));
}

void
iproto_send_event(struct obuf *out, uint64_t sync,
		  const char *key, size_t key_len,
		  const char *data, const char *data_end)
{
	/* Calculate the packet size. */
	size_t size = 5;
	/* Packet header. Note: no schema version. */
	size += mp_sizeof_map(2);
	size += mp_sizeof_uint(IPROTO_REQUEST_TYPE);
	size += mp_sizeof_uint(IPROTO_EVENT);
	size += mp_sizeof_uint(IPROTO_SYNC);
	size += mp_sizeof_uint(sync);
	/* Packet body. */
	size += mp_sizeof_map(data != NULL ? 2 : 1);
	size += mp_sizeof_uint(IPROTO_EVENT_KEY);
	size += mp_sizeof_str(key_len);
	if (data != NULL) {
		size += mp_sizeof_uint(IPROTO_EVENT_DATA);
		size += data_end - data;
	}
	/* Encode the packet. */
	char *buf = xobuf_alloc(out, size);
	char *p = buf;
	/* Fix header. */
	*(p++) = 0xce;
	mp_store_u32(p, size - 5);
	p += 4;
	/* Packet header. */
	p = mp_encode_map(p, 2);
	p = mp_encode_uint(p, IPROTO_REQUEST_TYPE);
	p = mp_encode_uint(p, IPROTO_EVENT);
	p = mp_encode_uint(p, IPROTO_SYNC);
	p = mp_encode_uint(p, sync);
	/* Packet body. */
	p = mp_encode_map(p, data != NULL ? 2 : 1);
	p = mp_encode_uint(p, IPROTO_EVENT_KEY);
	p = mp_encode_str(p, key, key_len);
	if (data != NULL) {
		p = mp_encode_uint(p, IPROTO_EVENT_DATA);
		memcpy(p, data, data_end - data);
		p += data_end - data;
	}
	assert(size == (size_t)(p - buf));
	(void)p;
}

int
xrow_decode_dml_internal(struct xrow_header *row, struct request *request,
			 uint64_t key_map, bool accept_space_name)
{
	memset(request, 0, sizeof(*request));
	request->header = row;
	request->type = row->type;

	if (row->bodycnt == 0)
		goto done;

	assert(row->bodycnt == 1);
	const char *data = (const char *) row->body[0].iov_base;
	if (mp_typeof(*data) != MP_MAP) {
error:
		xrow_on_decode_err(row, ER_INVALID_MSGPACK, "packet body");
		return -1;
	}

	uint32_t size = mp_decode_map(&data);
	for (uint32_t i = 0; i < size; i++) {
		if (mp_typeof(*data) != MP_UINT) {
			mp_next(&data);
			mp_next(&data);
			continue;
		}
		uint64_t key = mp_decode_uint(&data);
		const char *value = data;
		mp_next(&data);
		if (key < iproto_key_MAX &&
		    iproto_key_type[key] != mp_typeof(*value))
			goto error;
		if (key < 64)
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
		case IPROTO_FETCH_POSITION:
			request->fetch_position = mp_decode_bool(&value);
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
		case IPROTO_OLD_TUPLE:
			request->old_tuple = value;
			request->old_tuple_end = data;
			break;
		case IPROTO_NEW_TUPLE:
			request->new_tuple = value;
			request->new_tuple_end = data;
			break;
		case IPROTO_AFTER_POSITION:
			request->after_position = value;
			request->after_position_end = data;
			break;
		case IPROTO_AFTER_TUPLE:
			request->after_tuple = value;
			request->after_tuple_end = data;
			break;
		case IPROTO_SPACE_NAME:
			request->space_name =
				mp_decode_str(&value, &request->space_name_len);
			break;
		case IPROTO_INDEX_NAME:
			request->index_name =
				mp_decode_str(&value, &request->index_name_len);
			break;
		case IPROTO_ARROW: {
			int8_t type;
			uint32_t size;
			const char *data = mp_decode_ext(&value, &type, &size);
			if (type != MP_ARROW)
				goto error;
			request->arrow_ipc = data;
			request->arrow_ipc_end = data + size;
			break;
		}
		default:
			break;
		}
	}
	if (accept_space_name && request->space_name != NULL)
		key_map &= ~iproto_key_bit(IPROTO_SPACE_ID);
done:
	if (key_map) {
		enum iproto_key key = (enum iproto_key) bit_ctz_u64(key_map);
		xrow_on_decode_err(row, ER_MISSING_REQUEST_FIELD,
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
	if (request->old_tuple != NULL) {
		SNPRINT(total, snprintf, buf, size, ", old_tuple: ");
		SNPRINT(total, mp_snprint, buf, size, request->old_tuple);
	}
	if (request->new_tuple != NULL) {
		SNPRINT(total, snprintf, buf, size, ", new_tuple: ");
		SNPRINT(total, mp_snprint, buf, size, request->new_tuple);
	}
	if (request->fetch_position) {
		SNPRINT(total, snprintf, buf, size, ", fetch_position: true");
	}
	if (request->after_position != NULL) {
		SNPRINT(total, snprintf, buf, size, ", after_position: ");
		SNPRINT(total, mp_snprint, buf, size, request->after_position);
	}
	if (request->after_tuple != NULL) {
		SNPRINT(total, snprintf, buf, size, ", after_tuple: ");
		SNPRINT(total, mp_snprint, buf, size, request->after_tuple);
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

void
xrow_encode_dml(const struct request *request, struct region *region,
		struct iovec *iov, int *iovcnt)
{
	assert(request != NULL);
	/* Select is unexpected here. Hence, pagination option too. */
	assert(request->header == NULL ||
	       request->header->type != IPROTO_SELECT);
	assert(request->after_position == NULL);
	assert(request->after_tuple == NULL);
	assert(!request->fetch_position);
	const int MAP_LEN_MAX = 40;
	uint32_t key_len = request->key_end - request->key;
	uint32_t ops_len = request->ops_end - request->ops;
	uint32_t tuple_meta_len = request->tuple_meta_end - request->tuple_meta;
	uint32_t tuple_len = request->tuple_end - request->tuple;
	uint32_t old_tuple_len = request->old_tuple_end - request->old_tuple;
	uint32_t new_tuple_len = request->new_tuple_end - request->new_tuple;
	ssize_t arrow_len = request->arrow_ipc_end - request->arrow_ipc;
	uint32_t len = MAP_LEN_MAX + key_len + ops_len + tuple_meta_len +
		       tuple_len + old_tuple_len + new_tuple_len + arrow_len;
	assert(request->arrow_ipc == NULL || arrow_len > 0);
	char *begin = xregion_alloc(region, len);
	char *pos = begin + 1;     /* skip 1 byte for MP_MAP */
	int map_size = 0;
	ERROR_INJECT(ERRINJ_XLOG_WRITE_INVALID_KEY, {
		pos = mp_encode_bool(pos, true);
		pos = mp_encode_uint(pos, 2);
		map_size++;
	});
	ERROR_INJECT(ERRINJ_XLOG_WRITE_INVALID_VALUE, {
		pos = mp_encode_uint(pos, IPROTO_KEY);
		pos = mp_encode_uint(pos, 2);
		map_size++;
	});
	ERROR_INJECT(ERRINJ_XLOG_WRITE_UNKNOWN_KEY, {
		pos = mp_encode_uint(pos, 666);
		pos = mp_encode_uint(pos, 2);
		map_size++;
	});
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
	if (request->old_tuple != NULL) {
		pos = mp_encode_uint(pos, IPROTO_OLD_TUPLE);
		memcpy(pos, request->old_tuple, old_tuple_len);
		pos += old_tuple_len;
		map_size++;
	}
	if (request->new_tuple != NULL) {
		pos = mp_encode_uint(pos, IPROTO_NEW_TUPLE);
		memcpy(pos, request->new_tuple, new_tuple_len);
		pos += new_tuple_len;
		map_size++;
	}
	if (request->arrow_ipc != NULL) {
		assert(request->type == IPROTO_INSERT_ARROW);
		pos = mp_encode_uint(pos, IPROTO_ARROW);
		pos = mp_encode_ext(pos, MP_ARROW, request->arrow_ipc,
				    arrow_len);
		map_size++;
	}

	if (map_size == 0) {
		*iovcnt = 0;
		return;
	}

	assert(pos <= begin + len);
	mp_encode_map(begin, map_size);
	ERROR_INJECT(ERRINJ_XLOG_WRITE_INVALID_BODY, {
		mp_encode_array(begin, 0);
	});
	ERROR_INJECT(ERRINJ_XLOG_WRITE_CORRUPTED_BODY, {
		*begin = 0xc1;
	});
	iov[0].iov_base = begin;
	iov[0].iov_len = pos - begin;
	*iovcnt = 1;
}

int
xrow_decode_id(const struct xrow_header *row, struct id_request *request)
{
	if (row->bodycnt == 0) {
		diag_set(ClientError, ER_INVALID_MSGPACK, "request body");
		return -1;
	}

	assert(row->bodycnt == 1);
	const char *p = (const char *)row->body[0].iov_base;
	if (mp_typeof(*p) != MP_MAP)
		goto error;

	request->version = 0;
	iproto_features_create(&request->features);
	request->auth_type = NULL;
	request->auth_type_len = 0;

	uint32_t map_size = mp_decode_map(&p);
	for (uint32_t i = 0; i < map_size; i++) {
		if (mp_typeof(*p) != MP_UINT)
			goto error;
		uint64_t key = mp_decode_uint(&p);
		if (key < iproto_key_MAX &&
		    iproto_key_type[key] != mp_typeof(*p))
			goto error;
		switch (key) {
		case IPROTO_VERSION:
			request->version = mp_decode_uint(&p);
			break;
		case IPROTO_FEATURES:
			if (mp_decode_iproto_features(
					&p, &request->features) != 0)
				goto error;
			break;
		case IPROTO_AUTH_TYPE:
			request->auth_type = mp_decode_str(
					&p, &request->auth_type_len);
			break;
		default:
			/* Ignore unknown keys for forward compatibility. */
			mp_next(&p);
		}
	}
	return 0;
error:
	xrow_on_decode_err(row, ER_INVALID_MSGPACK, "request body");
	return -1;
}

void
xrow_encode_id(struct xrow_header *row)
{
	memset(row, 0, sizeof(*row));
	row->type = IPROTO_ID;
	size_t size = mp_sizeof_map(2);
	size += mp_sizeof_uint(IPROTO_VERSION) +
		mp_sizeof_uint(IPROTO_CURRENT_VERSION);
	size += mp_sizeof_uint(IPROTO_FEATURES) +
		mp_sizeof_iproto_features(&IPROTO_CURRENT_FEATURES);
	char *buf = xregion_alloc(&fiber()->gc, size);
	char *p = buf;
	p = mp_encode_map(p, 2);
	p = mp_encode_uint(p, IPROTO_VERSION);
	p = mp_encode_uint(p, IPROTO_CURRENT_VERSION);
	p = mp_encode_uint(p, IPROTO_FEATURES);
	p = mp_encode_iproto_features(p, &IPROTO_CURRENT_FEATURES);
	assert((size_t)(p - buf) == size);
	(void)p;
	row->bodycnt = 1;
	row->body[0].iov_base = buf;
	row->body[0].iov_len = size;
}

void
xrow_encode_synchro(struct xrow_header *row, char *body,
		    const struct synchro_request *req)
{
	assert(iproto_type_is_synchro_request(req->type));

	char *pos = body;

	/* Skip one byte for the map. */
	pos++;
	uint32_t map_size = 0;

	pos = mp_encode_uint(pos, IPROTO_REPLICA_ID);
	pos = mp_encode_uint(pos, req->replica_id);
	map_size++;

	pos = mp_encode_uint(pos, IPROTO_LSN);
	pos = mp_encode_uint(pos, req->lsn);
	map_size++;

	if (req->term != 0) {
		pos = mp_encode_uint(pos, IPROTO_TERM);
		pos = mp_encode_uint(pos, req->term);
		map_size++;
	}

	if (req->confirmed_vclock != NULL) {
		pos = mp_encode_uint(pos, IPROTO_VCLOCK);
		pos = mp_encode_vclock_ignore0(pos, req->confirmed_vclock);
		map_size++;
	}

	mp_encode_map(body, map_size);

	assert(pos - body < XROW_BODY_LEN_MAX);

	memset(row, 0, sizeof(*row));
	row->type = req->type;
	row->body[0].iov_base = body;
	row->body[0].iov_len = pos - body;
	row->bodycnt = 1;
}

int
xrow_decode_synchro(const struct xrow_header *row, struct synchro_request *req,
		    struct vclock *vclock)
{
	if (row->bodycnt == 0) {
		diag_set(ClientError, ER_INVALID_MSGPACK, "request body");
		return -1;
	}

	assert(row->bodycnt == 1);

	const char *d = (const char *)row->body[0].iov_base;
	if (mp_typeof(*d) != MP_MAP) {
		xrow_on_decode_err(row, ER_INVALID_MSGPACK, "request body");
		return -1;
	}

	memset(req, 0, sizeof(*req));
	uint32_t map_size = mp_decode_map(&d);
	for (uint32_t i = 0; i < map_size; i++) {
		enum mp_type type = mp_typeof(*d);
		if (type != MP_UINT) {
			mp_next(&d);
			mp_next(&d);
			continue;
		}
		uint8_t key = mp_decode_uint(&d);
		if (key < iproto_key_MAX &&
		    iproto_key_type[key] != mp_typeof(*d)) {
bad_msgpack:
			xrow_on_decode_err(row, ER_INVALID_MSGPACK,
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
		case IPROTO_VCLOCK:
			if (vclock == NULL)
				mp_next(&d);
			else if (mp_decode_vclock_ignore0(&d, vclock) != 0)
				goto bad_msgpack;
			req->confirmed_vclock = vclock;
			break;
		default:
			mp_next(&d);
		}
	}

	req->type = row->type;
	req->origin_id = row->replica_id;

	return 0;
}

void
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
	if (r->leader_id != 0) {
		++map_size;
		size += mp_sizeof_uint(IPROTO_RAFT_LEADER_ID) +
			mp_sizeof_uint(r->leader_id);
	}
	if (r->is_leader_seen) {
		++map_size;
		size += mp_sizeof_uint(IPROTO_RAFT_IS_LEADER_SEEN) +
			mp_sizeof_bool(r->is_leader_seen);
	}
	if (r->vclock != NULL) {
		++map_size;
		size += mp_sizeof_uint(IPROTO_RAFT_VCLOCK) +
			mp_sizeof_vclock_ignore0(r->vclock);
	}
	size += mp_sizeof_map(map_size);

	char *buf = xregion_alloc(region, size);
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
	if (r->leader_id != 0) {
		buf = mp_encode_uint(buf, IPROTO_RAFT_LEADER_ID);
		buf = mp_encode_uint(buf, r->leader_id);
	}
	if (r->is_leader_seen) {
		buf = mp_encode_uint(buf, IPROTO_RAFT_IS_LEADER_SEEN);
		buf = mp_encode_bool(buf, true);
	}
	if (r->vclock != NULL) {
		buf = mp_encode_uint(buf, IPROTO_RAFT_VCLOCK);
		buf = mp_encode_vclock_ignore0(buf, r->vclock);
	}
	row->body[0].iov_len = buf - begin;
}

int
xrow_decode_raft(const struct xrow_header *row, struct raft_request *r,
		 struct vclock *vclock)
{
	if (row->type != IPROTO_RAFT)
		goto bad_msgpack;
	if (row->bodycnt != 1 || row->group_id != GROUP_LOCAL) {
		diag_set(ClientError, ER_INVALID_MSGPACK,
			 "malformed raft request");
		return -1;
	}
	memset(r, 0, sizeof(*r));

	const char *pos = row->body[0].iov_base;
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
		case IPROTO_RAFT_LEADER_ID:
			if (mp_typeof(*pos) != MP_UINT)
				goto bad_msgpack;
			r->leader_id = mp_decode_uint(&pos);
			break;
		case IPROTO_RAFT_IS_LEADER_SEEN:
			if (mp_typeof(*pos) != MP_BOOL)
				goto bad_msgpack;
			r->is_leader_seen = mp_decode_bool(&pos);
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
	xrow_on_decode_err(row, ER_INVALID_MSGPACK, "raft body");
	return -1;
}

void
xrow_to_iovec(const struct xrow_header *row, struct iovec *out, int *iovcnt)
{
	assert(mp_sizeof_uint(UINT32_MAX) == 5);
	xrow_encode(row, row->sync, /*fixheader_len=*/5, out, iovcnt);
	ssize_t len = -5;
	for (int i = 0; i < *iovcnt; i++)
		len += out[i].iov_len;

	/* Encode length */
	char *data = (char *) out[0].iov_base;
	*(data++) = 0xce; /* MP_UINT32 */
	store_u32(data, mp_bswap_u32(len));

	assert(*iovcnt <= XROW_IOVMAX);
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
	if (mp_typeof(*data) != MP_MAP) {
error:
		xrow_on_decode_err(row, ER_INVALID_MSGPACK, "packet body");
		return -1;
	}

	memset(request, 0, sizeof(*request));

	uint32_t map_size = mp_decode_map(&data);
	for (uint32_t i = 0; i < map_size; ++i) {
		if (mp_typeof(*data) != MP_UINT)
			goto error;

		uint64_t key = mp_decode_uint(&data);
		const char *value = data;
		mp_next(&data);

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
		case IPROTO_TUPLE_FORMATS:
			if (mp_typeof(*value) != MP_MAP)
				goto error;
			request->tuple_formats = value;
			request->tuple_formats_end = data;
		default:
			continue; /* unknown key */
		}
	}
	if (row->type == IPROTO_EVAL) {
		if (request->expr == NULL) {
			xrow_on_decode_err(row, ER_MISSING_REQUEST_FIELD,
					   iproto_key_name(IPROTO_EXPR));
			return -1;
		}
	} else if (request->name == NULL) {
		assert(row->type == IPROTO_CALL_16 ||
		       row->type == IPROTO_CALL);
		xrow_on_decode_err(row, ER_MISSING_REQUEST_FIELD,
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

void
xrow_encode_watch_key(struct xrow_header *row, const char *key, uint16_t type)
{
	memset(row, 0, sizeof(*row));
	size_t size = mp_sizeof_map(1) +
		      mp_sizeof_uint(IPROTO_EVENT_KEY) +
		      mp_sizeof_str(strlen(key));
	char *buf = xregion_alloc(&fiber()->gc, size);
	row->body[0].iov_base = buf;
	buf = mp_encode_map(buf, 1);
	buf = mp_encode_uint(buf, IPROTO_EVENT_KEY);
	buf = mp_encode_str0(buf, key);
	row->body[0].iov_len = buf - (char *)row->body[0].iov_base;
	row->bodycnt = 1;
	row->type = type;
}

int
xrow_decode_watch(const struct xrow_header *row, struct watch_request *request)
{
	if (row->bodycnt == 0) {
		diag_set(ClientError, ER_INVALID_MSGPACK,
			 "missing request body");
		return -1;
	}
	assert(row->bodycnt == 1);
	const char *data = (const char *)row->body[0].iov_base;
	if (mp_typeof(*data) != MP_MAP) {
error:
		xrow_on_decode_err(row, ER_INVALID_MSGPACK, "packet body");
		return -1;
	}
	memset(request, 0, sizeof(*request));
	uint32_t map_size = mp_decode_map(&data);
	for (uint32_t i = 0; i < map_size; i++) {
		if (mp_typeof(*data) != MP_UINT)
			goto error;
		uint64_t key = mp_decode_uint(&data);
		if (key < iproto_key_MAX &&
		    iproto_key_type[key] != MP_NIL &&
		    iproto_key_type[key] != mp_typeof(*data))
			goto error;
		switch (key) {
		case IPROTO_EVENT_KEY:
			request->key = mp_decode_str(&data, &request->key_len);
			break;
		case IPROTO_EVENT_DATA:
			request->data = data;
			mp_next(&data);
			request->data_end = data;
			break;
		default:
			mp_next(&data);
			break;
		}
	}
	if (request->key == NULL) {
		xrow_on_decode_err(row, ER_MISSING_REQUEST_FIELD,
				   iproto_key_name(IPROTO_EVENT_KEY));
		return -1;
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
	if (mp_typeof(*data) != MP_MAP) {
error:
		xrow_on_decode_err(row, ER_INVALID_MSGPACK, "packet body");
		return -1;
	}

	memset(request, 0, sizeof(*request));

	uint32_t map_size = mp_decode_map(&data);
	for (uint32_t i = 0; i < map_size; ++i) {
		if (mp_typeof(*data) != MP_UINT)
			goto error;

		uint64_t key = mp_decode_uint(&data);
		const char *value = data;
		mp_next(&data);

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
	if (request->user_name == NULL) {
		xrow_on_decode_err(row, ER_MISSING_REQUEST_FIELD,
				   iproto_key_name(IPROTO_USER_NAME));
		return -1;
	}
	if (request->scramble == NULL) {
		xrow_on_decode_err(row, ER_MISSING_REQUEST_FIELD,
				   iproto_key_name(IPROTO_TUPLE));
		return -1;
	}
	return 0;
}

void
xrow_encode_auth(struct xrow_header *packet,
		 const char *login, size_t login_len,
		 const char *method, size_t method_len,
		 const char *data, const char *data_end)
{
	assert(login != NULL);
	assert(data != NULL);
	memset(packet, 0, sizeof(*packet));
	size_t data_size = data_end - data;
	size_t buf_size = XROW_BODY_LEN_MAX + login_len + data_size;
	char *buf = xregion_alloc(&fiber()->gc, buf_size);
	char *d = buf;
	d = mp_encode_map(d, 2);
	d = mp_encode_uint(d, IPROTO_USER_NAME);
	d = mp_encode_str(d, login, login_len);
	d = mp_encode_uint(d, IPROTO_TUPLE);
	d = mp_encode_array(d, 2);
	d = mp_encode_str(d, method, method_len);
	memcpy(d, data, data_size);
	d += data_size;
	assert(d <= buf + buf_size);
	packet->body[0].iov_base = buf;
	packet->body[0].iov_len = (d - buf);
	packet->bodycnt = 1;
	packet->type = IPROTO_AUTH;
}

void
xrow_decode_error(const struct xrow_header *row)
{
	uint32_t code = row->type & (IPROTO_TYPE_ERROR - 1);

	const char *pos;
	uint32_t map_size;

	if (row->bodycnt == 0)
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
				box_error_set(__FILE__, __LINE__, code, "%.*s",
					      len, str);
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
	box_error_set(__FILE__, __LINE__, code, "");
}

int
xrow_decode_begin(const struct xrow_header *row, struct begin_request *request)
{
	if (row->type != IPROTO_BEGIN)
		goto bad_msgpack;
	memset(request, 0, sizeof(*request));
	request->is_sync = false;

	/** Request without extra options. */
	if (row->bodycnt == 0)
		return 0;

	const char *d = row->body[0].iov_base;
	if (mp_typeof(*d) != MP_MAP)
		goto bad_msgpack;

	uint32_t map_size = mp_decode_map(&d);
	for (uint32_t i = 0; i < map_size; ++i) {
		if (mp_typeof(*d) != MP_UINT)
			goto bad_msgpack;
		uint64_t key = mp_decode_uint(&d);
		if (key < iproto_key_MAX &&
		    mp_typeof(*d) != iproto_key_type[key])
			goto bad_msgpack;
		switch (key) {
		case IPROTO_TIMEOUT:
			request->timeout = mp_decode_double(&d);
			break;
		case IPROTO_TXN_ISOLATION:
			request->txn_isolation = mp_decode_uint(&d);
			break;
		case IPROTO_IS_SYNC:
			if (mp_decode_bool(&d)) {
				request->is_sync = true;
				break;
			} else {
				xrow_on_decode_err(row, ER_ILLEGAL_PARAMS,
						   "is_sync can only be true");
				return -1;
			}
			break;
		default:
			mp_next(&d);
			break;
		}
	}
	return 0;

bad_msgpack:
	xrow_on_decode_err(row, ER_INVALID_MSGPACK, "request body");
	return -1;
}

int
xrow_decode_commit(const struct xrow_header *row, struct commit_request *request)
{
	assert(row->type == IPROTO_COMMIT);
	memset(request, 0, sizeof(*request));

	/** Request without extra options. */
	if (row->bodycnt == 0)
		return 0;

	const char *d = row->body[0].iov_base;
	if (mp_typeof(*d) != MP_MAP)
		goto bad_msgpack;

	uint32_t map_size = mp_decode_map(&d);
	for (uint32_t i = 0; i < map_size; ++i) {
		if (mp_typeof(*d) != MP_UINT)
			goto bad_msgpack;
		uint64_t key = mp_decode_uint(&d);
		if (key < iproto_key_MAX &&
		    mp_typeof(*d) != iproto_key_type[key])
			goto bad_msgpack;
		switch (key) {
		case IPROTO_IS_SYNC:
			if (mp_decode_bool(&d)) {
				request->is_sync = true;
				break;
			} else {
				xrow_on_decode_err(row, ER_ILLEGAL_PARAMS,
						   "is_sync can only be true");
				return -1;
			}
			break;
		default:
			mp_next(&d);
			break;
		}
	}
	return 0;

bad_msgpack:
	xrow_on_decode_err(row, ER_INVALID_MSGPACK, "request body");
	return -1;
}

void
xrow_encode_vote(struct xrow_header *row)
{
	memset(row, 0, sizeof(*row));
	row->type = IPROTO_VOTE;
}

/** Decode the remote instance's IPROTO_VOTE response body. */
static int
mp_decode_ballot(const char *data, const char *end,
		 struct ballot *ballot, bool *is_empty);

int
xrow_decode_ballot(const struct xrow_header *row, struct ballot *ballot)
{
	ballot->is_ro_cfg = false;
	ballot->can_lead = false;
	ballot->is_ro = false;
	ballot->is_anon = false;
	ballot->is_booted = true;
	vclock_create(&ballot->vclock);
	vclock_create(&ballot->gc_vclock);
	*ballot->instance_name = '\0';

	if (row->bodycnt == 0)
		goto err;
	assert(row->bodycnt == 1);

	const char *data = (const char *)row->body[0].iov_base;
	const char *end = data + row->body[0].iov_len;

	bool is_empty;
	if (mp_decode_ballot(data, end, ballot, &is_empty) < 0) {
err:
		xrow_on_decode_err(row, ER_INVALID_MSGPACK, "packet body");
		return -1;
	}
	return 0;
}

static int
mp_decode_ballot(const char *data, const char *end,
		 struct ballot *ballot, bool *is_empty)
{
	*is_empty = true;
	if (mp_typeof(*data) != MP_MAP)
		return -1;

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
				return -1;
			ballot->is_ro_cfg = mp_decode_bool(&data);
			*is_empty = false;
			break;
		case IPROTO_BALLOT_IS_RO:
			if (mp_typeof(*data) != MP_BOOL)
				return -1;
			ballot->is_ro = mp_decode_bool(&data);
			*is_empty = false;
			break;
		case IPROTO_BALLOT_IS_ANON:
			if (mp_typeof(*data) != MP_BOOL)
				return -1;
			ballot->is_anon = mp_decode_bool(&data);
			*is_empty = false;
			break;
		case IPROTO_BALLOT_VCLOCK:
			if (mp_decode_vclock_ignore0(&data,
						     &ballot->vclock) != 0)
				return -1;
			*is_empty = false;
			break;
		case IPROTO_BALLOT_GC_VCLOCK:
			if (mp_decode_vclock_ignore0(&data,
						     &ballot->gc_vclock) != 0)
				return -1;
			*is_empty = false;
			break;
		case IPROTO_BALLOT_IS_BOOTED:
			if (mp_typeof(*data) != MP_BOOL)
				return -1;
			ballot->is_booted = mp_decode_bool(&data);
			*is_empty = false;
			break;
		case IPROTO_BALLOT_CAN_LEAD:
			if (mp_typeof(*data) != MP_BOOL)
				return -1;
			ballot->can_lead = mp_decode_bool(&data);
			*is_empty = false;
			break;
		case IPROTO_BALLOT_BOOTSTRAP_LEADER_UUID: {
			struct tt_uuid *uuid = &ballot->bootstrap_leader_uuid;
			if (xrow_decode_uuid(&data, uuid) != 0)
				return -1;
			*is_empty = false;
			break;
		}
		case IPROTO_BALLOT_INSTANCE_NAME:
			if (xrow_decode_node_name(&data,
						  ballot->instance_name) != 0) {
				return -1;
			}
			break;
		case IPROTO_BALLOT_REGISTERED_REPLICA_UUIDS: {
			if (mp_typeof(*data) != MP_ARRAY)
				return -1;
			int size = mp_decode_array(&data);
			if (size >= VCLOCK_MAX || size < 0)
				return -1;
			ballot->registered_replica_uuids_size = size;
			for (int i = 0; i < size; i++) {
				struct tt_uuid *uuid =
					&ballot->registered_replica_uuids[i];
				if (xrow_decode_uuid(&data, uuid) != 0)
					return -1;
			}
			*is_empty = false;
			break;
		}
		default:
			mp_next(&data);
		}
	}
	return 0;
}

int
xrow_decode_ballot_event(const struct watch_request *req,
			 struct ballot *ballot, bool *is_empty)
{
	assert(req->data != NULL);
	assert(req->data_end > req->data);
	/*
	 * Note that in contrary to xrow_decode_ballot() we do not nullify the
	 * ballot here. If some of the fields are omitted in the event, their
	 * previous values hold.
	 */
	if (mp_decode_ballot(req->data, req->data_end, ballot, is_empty) < 0) {
		diag_set(ClientError, ER_INVALID_MSGPACK, "packet body");
		dump_row_hex(req->data, req->data_end);
		return -1;
	}
	return 0;
}

/**
 * A template which can represent any replication request - join, register,
 * subscribe, etc. All fields are optional - when left NULL, they are not
 * encoded. Each specific request simply uses a subset of these fields + its own
 * iproto request type. Meaning of each field depends on the original request
 * type, but the iproto keys are fixed.
 */
struct replication_request {
	/** IPROTO_REPLICASET_UUID. */
	struct tt_uuid *replicaset_uuid;
	/** IPROTO_REPLICASET_NAME. */
	char *replicaset_name;
	/** IPROTO_INSTANCE_UUID. */
	struct tt_uuid *instance_uuid;
	/** IPROTO_INSTANCE_NAME. */
	char *instance_name;
	/** IPROTO_VCLOCK. */
	struct vclock *vclock_ignore0;
	/** IPROTO_VCLOCK. */
	struct vclock *vclock;
	/** IPROTO_ID_FILTER. */
	uint32_t *id_filter;
	/** IPROTO_SERVER_VERSION. */
	uint32_t *version_id;
	/** IPROTO_REPLICA_ANON. */
	bool *is_anon;
	/** IPROTO_IS_CHECKPOINT_JOIN. */
	bool *is_checkpoint_join;
	/** IPROTO_CHECKPOINT_VCLOCK. */
	struct vclock *checkpoint_vclock;
	/** IPROTO_CHECKPOINT_LSN. */
	uint64_t *checkpoint_lsn;
};

/** Encode a replication request template. */
static void
xrow_encode_replication_request(struct xrow_header *row,
				const struct replication_request *req,
				uint16_t type)
{
	memset(row, 0, sizeof(*row));
	size_t size = XROW_BODY_LEN_MAX;
	if (req->vclock_ignore0 != NULL) {
		size += mp_sizeof_vclock_ignore0(req->vclock_ignore0);
		assert(req->vclock == NULL);
	}
	if (req->vclock != NULL) {
		size += mp_sizeof_vclock(req->vclock);
		assert(req->vclock_ignore0 == NULL);
	}
	char *buf = xregion_alloc(&fiber()->gc, size);
	/* Skip one byte for future map header. */
	char *data = buf + 1;
	uint32_t map_size = 0;
	if (req->replicaset_uuid != NULL) {
		++map_size;
		data = mp_encode_uint(data, IPROTO_REPLICASET_UUID);
		data = xrow_encode_uuid(data, req->replicaset_uuid);
	}
	if (req->replicaset_name != NULL && *req->replicaset_name != 0) {
		++map_size;
		data = mp_encode_uint(data, IPROTO_REPLICASET_NAME);
		data = mp_encode_str0(data, req->replicaset_name);
	}
	if (req->instance_uuid != NULL) {
		++map_size;
		data = mp_encode_uint(data, IPROTO_INSTANCE_UUID);
		data = xrow_encode_uuid(data, req->instance_uuid);
	}
	if (req->instance_name != NULL && *req->instance_name != 0) {
		++map_size;
		data = mp_encode_uint(data, IPROTO_INSTANCE_NAME);
		data = mp_encode_str0(data, req->instance_name);
	}
	if (req->vclock_ignore0 != NULL) {
		++map_size;
		data = mp_encode_uint(data, IPROTO_VCLOCK);
		data = mp_encode_vclock_ignore0(data, req->vclock_ignore0);
	}
	if (req->vclock != NULL) {
		++map_size;
		data = mp_encode_uint(data, IPROTO_VCLOCK);
		data = mp_encode_vclock(data, req->vclock);
	}
	if (req->version_id != NULL) {
		++map_size;
		data = mp_encode_uint(data, IPROTO_SERVER_VERSION);
		data = mp_encode_uint(data, *req->version_id);
	}
	if (req->is_anon != NULL) {
		++map_size;
		data = mp_encode_uint(data, IPROTO_REPLICA_ANON);
		data = mp_encode_bool(data, *req->is_anon);
	}
	if (req->id_filter != NULL) {
		++map_size;
		uint32_t id_filter = *req->id_filter;
		data = mp_encode_uint(data, IPROTO_ID_FILTER);
		data = mp_encode_array(data, bit_count_u32(id_filter));
		struct bit_iterator it;
		bit_iterator_init(&it, &id_filter, sizeof(id_filter), true);
		for (size_t id = bit_iterator_next(&it); id < VCLOCK_MAX;
		     id = bit_iterator_next(&it)) {
			data = mp_encode_uint(data, id);
		}
	}
	assert(data <= buf + size);
	assert(map_size <= 15);
	char *map_header_end = mp_encode_map(buf, map_size);
	assert(map_header_end - buf == 1);
	(void)map_header_end;
	row->body[0].iov_base = buf;
	row->body[0].iov_len = (data - buf);
	row->bodycnt = 1;
	row->type = type;
}

/** Decode a replication request template. */
static int
xrow_decode_replication_request(const struct xrow_header *row,
				struct replication_request *req)
{
	if (row->bodycnt == 0) {
		diag_set(ClientError, ER_INVALID_MSGPACK, "request body");
		return -1;
	}
	assert(row->bodycnt == 1);
	const char *d = (const char *)row->body[0].iov_base;
	if (mp_typeof(*d) != MP_MAP) {
		xrow_on_decode_err(row, ER_INVALID_MSGPACK, "request body");
		return -1;
	}
	uint32_t map_size = mp_decode_map(&d);
	for (uint32_t i = 0; i < map_size; i++) {
		if (mp_typeof(*d) != MP_UINT) {
			mp_next(&d); /* key */
			mp_next(&d); /* value */
			continue;
		}
		uint8_t key = mp_decode_uint(&d);
		switch (key) {
		case IPROTO_REPLICASET_UUID:
			if (req->replicaset_uuid == NULL)
				goto skip;
			if (xrow_decode_uuid(&d, req->replicaset_uuid) != 0) {
				xrow_on_decode_err(row, ER_INVALID_MSGPACK,
						   "replicaset UUID");
				return -1;
			}
			break;
		case IPROTO_REPLICASET_NAME:
			if (req->replicaset_name == NULL)
				goto skip;
			if (xrow_decode_node_name(
				&d, req->replicaset_name) != 0) {
				xrow_on_decode_err(row, ER_INVALID_MSGPACK,
						   "invalid REPLICASET_NAME");
				return -1;
			}
			break;
		case IPROTO_INSTANCE_UUID:
			if (req->instance_uuid == NULL)
				goto skip;
			if (xrow_decode_uuid(&d, req->instance_uuid) != 0) {
				xrow_on_decode_err(row, ER_INVALID_MSGPACK,
						   "instance UUID");
				return -1;
			}
			break;
		case IPROTO_INSTANCE_NAME:
			if (req->instance_name == NULL)
				goto skip;
			if (xrow_decode_node_name(
				&d, req->instance_name) != 0) {
				xrow_on_decode_err(row, ER_INVALID_MSGPACK,
						   "invalid INSTANCE_NAME");
				return -1;
			}
			break;
		case IPROTO_VCLOCK:
			if (req->vclock_ignore0 == NULL)
				goto skip;
			if (mp_decode_vclock_ignore0(
					&d, req->vclock_ignore0) != 0) {
				xrow_on_decode_err(row, ER_INVALID_MSGPACK,
						   "invalid VCLOCK");
				return -1;
			}
			break;
		case IPROTO_SERVER_VERSION:
			if (req->version_id == NULL)
				goto skip;
			if (mp_typeof(*d) != MP_UINT) {
				xrow_on_decode_err(row, ER_INVALID_MSGPACK,
						   "invalid VERSION");
				return -1;
			}
			*req->version_id = mp_decode_uint(&d);
			break;
		case IPROTO_REPLICA_ANON:
			if (req->is_anon == NULL)
				goto skip;
			if (mp_typeof(*d) != MP_BOOL) {
				xrow_on_decode_err(row, ER_INVALID_MSGPACK,
						   "invalid REPLICA_ANON flag");
				return -1;
			}
			*req->is_anon = mp_decode_bool(&d);
			break;
		case IPROTO_ID_FILTER:
			if (req->id_filter == NULL)
				goto skip;
			if (mp_typeof(*d) != MP_ARRAY) {
id_filter_decode_err:		xrow_on_decode_err(row, ER_INVALID_MSGPACK,
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
				*req->id_filter |= 1 << val;
			}
			break;
		case IPROTO_IS_CHECKPOINT_JOIN:
			if (req->is_checkpoint_join == NULL)
				goto skip;
			if (mp_typeof(*d) != MP_BOOL) {
				xrow_on_decode_err(
					row, ER_INVALID_MSGPACK,
					"invalid IS_CHECKPOINT_JOIN");
				return -1;
			}
			*req->is_checkpoint_join = mp_decode_bool(&d);
			break;
		case IPROTO_CHECKPOINT_VCLOCK:
			if (req->checkpoint_vclock == NULL)
				goto skip;
			if (mp_decode_vclock(&d, req->checkpoint_vclock) != 0) {
				xrow_on_decode_err(row, ER_INVALID_MSGPACK,
						   "invalid CHECKPOINT_VCLOCK");
				return -1;
			}
			break;
		case IPROTO_CHECKPOINT_LSN:
			if (req->checkpoint_lsn == NULL)
				goto skip;
			if (mp_typeof(*d) != MP_UINT) {
				xrow_on_decode_err(row, ER_INVALID_MSGPACK,
						   "invalid CHECKPOINT_LSN");
				return -1;
			}
			*req->checkpoint_lsn = mp_decode_uint(&d);
			break;
		default: skip:
			mp_next(&d); /* value */
		}
	}
	return 0;
}

void
xrow_encode_register(struct xrow_header *row,
		     const struct register_request *req)
{
	struct register_request *cast = (struct register_request *)req;
	const struct replication_request base_req = {
		.instance_uuid = &cast->instance_uuid,
		.instance_name = cast->instance_name,
		.vclock_ignore0 = &cast->vclock,
	};
	xrow_encode_replication_request(row, &base_req, IPROTO_REGISTER);
}

int
xrow_decode_register(const struct xrow_header *row,
		     struct register_request *req)
{
	memset(req, 0, sizeof(*req));
	struct replication_request base_req = {
		.instance_uuid = &req->instance_uuid,
		.instance_name = req->instance_name,
		.vclock_ignore0 = &req->vclock,
	};
	return xrow_decode_replication_request(row, &base_req);
}

void
xrow_encode_subscribe(struct xrow_header *row,
		      const struct subscribe_request *req)
{
	struct subscribe_request *cast = (struct subscribe_request *)req;
	const struct replication_request base_req = {
		.replicaset_uuid = &cast->replicaset_uuid,
		.replicaset_name = cast->replicaset_name,
		.instance_uuid = &cast->instance_uuid,
		.instance_name = cast->instance_name,
		.vclock_ignore0 = &cast->vclock,
		.is_anon = &cast->is_anon,
		.id_filter = &cast->id_filter,
		.version_id = &cast->version_id,
	};
	xrow_encode_replication_request(row, &base_req, IPROTO_SUBSCRIBE);
}

int
xrow_decode_subscribe(const struct xrow_header *row,
		      struct subscribe_request *req)
{
	memset(req, 0, sizeof(*req));
	struct replication_request base_req = {
		.replicaset_uuid = &req->replicaset_uuid,
		.replicaset_name = req->replicaset_name,
		.instance_uuid = &req->instance_uuid,
		.instance_name = req->instance_name,
		.vclock_ignore0 = &req->vclock,
		.version_id = &req->version_id,
		.is_anon = &req->is_anon,
		.id_filter = &req->id_filter,
	};
	return xrow_decode_replication_request(row, &base_req);
}

void
xrow_encode_join(struct xrow_header *row, const struct join_request *req)
{
	struct join_request *cast = (struct join_request *)req;
	const struct replication_request base_req = {
		.instance_uuid = &cast->instance_uuid,
		.instance_name = cast->instance_name,
		.version_id = &cast->version_id,
	};
	xrow_encode_replication_request(row, &base_req, IPROTO_JOIN);
}

int
xrow_decode_join(const struct xrow_header *row, struct join_request *req)
{
	memset(req, 0, sizeof(*req));
	struct replication_request base_req = {
		.instance_uuid = &req->instance_uuid,
		.instance_name = req->instance_name,
		.version_id = &req->version_id,
	};
	return xrow_decode_replication_request(row, &base_req);
}

void
xrow_encode_fetch_snapshot(struct xrow_header *row,
			   const struct fetch_snapshot_request *req)
{
	struct fetch_snapshot_request *cast =
		(struct fetch_snapshot_request *)req;
	const struct replication_request base_req = {
		.version_id = &cast->version_id,
		.instance_uuid = &cast->instance_uuid,
	};
	xrow_encode_replication_request(row, &base_req, IPROTO_FETCH_SNAPSHOT);
}

int
xrow_decode_fetch_snapshot(const struct xrow_header *row,
			   struct fetch_snapshot_request *req)
{
	memset(req, 0, sizeof(*req));
	struct replication_request base_req = {
		.version_id = &req->version_id,
		.is_checkpoint_join = &req->is_checkpoint_join,
		.checkpoint_vclock = &req->checkpoint_vclock,
		.checkpoint_lsn = &req->checkpoint_lsn,
		.instance_uuid = &req->instance_uuid,
	};
	/*
	 * Vclock must be cleared, as it sets -1 signature, which cannot be
	 * done by memset above. This is done in order to distinguish not
	 * initialized vclock from the zero one.
	 */
	vclock_clear(&req->checkpoint_vclock);
	return xrow_decode_replication_request(row, &base_req);
}

void
xrow_encode_relay_heartbeat(struct xrow_header *row,
			    const struct relay_heartbeat *req)
{
	/*
	 * Not using replication_request, because heartbeats are too simple and
	 * are used often.
	 */
	memset(row, 0, sizeof(*row));
	row->type = IPROTO_OK;
	size_t size = 0;
	size_t map_size = 0;
	if (req->vclock_sync != 0) {
		map_size += 1;
		size += mp_sizeof_uint(IPROTO_VCLOCK_SYNC);
		size += mp_sizeof_uint(req->vclock_sync);
	}
	if (map_size == 0)
		return;
	size += mp_sizeof_map(map_size);
	char *buf = xregion_alloc(&fiber()->gc, size);
	char *data = buf;
	data = mp_encode_map(data, map_size);
	assert(req->vclock_sync != 0);
	data = mp_encode_uint(data, IPROTO_VCLOCK_SYNC);
	data = mp_encode_uint(data, req->vclock_sync);
	assert(data <= buf + size);
	row->body[0].iov_base = buf;
	row->body[0].iov_len = (data - buf);
	row->bodycnt = 1;
}

int
xrow_decode_relay_heartbeat(const struct xrow_header *row,
			    struct relay_heartbeat *req)
{
	/*
	 * Not using replication_request, because heartbeats are too simple and
	 * are used often.
	 */
	memset(req, 0, sizeof(*req));
	if (row->bodycnt == 0)
		return 0;
	const char *d = row->body[0].iov_base;
	if (mp_typeof(*d) != MP_MAP) {
		xrow_on_decode_err(row, ER_INVALID_MSGPACK, "request body");
		return -1;
	}
	uint32_t map_size = mp_decode_map(&d);
	for (uint32_t i = 0; i < map_size; i++) {
		if (mp_typeof(*d) != MP_UINT) {
			mp_next(&d);
			mp_next(&d);
			continue;
		}
		uint64_t key = mp_decode_uint(&d);
		switch (key) {
		case IPROTO_VCLOCK_SYNC:
			if (mp_typeof(*d) != MP_UINT) {
				xrow_on_decode_err(row, ER_INVALID_MSGPACK,
						   "invalid vclock sync");
				return -1;
			}
			req->vclock_sync = mp_decode_uint(&d);
			break;
		default:
			mp_next(&d);
		}
	}
	return 0;
}

void
xrow_encode_applier_heartbeat(struct xrow_header *row,
			      const struct applier_heartbeat *req)
{
	/*
	 * Not using replication_request, because heartbeats are too simple and
	 * are used often.
	 */
	memset(row, 0, sizeof(*row));
	size_t size = 0;
	size_t map_size = 2;
	size += mp_sizeof_uint(IPROTO_VCLOCK);
	size += mp_sizeof_vclock_ignore0(&req->vclock);
	size += mp_sizeof_uint(IPROTO_TERM);
	size += mp_sizeof_uint(req->term);
	if (req->vclock_sync != 0) {
		map_size += 1;
		size += mp_sizeof_uint(IPROTO_VCLOCK_SYNC);
		size += mp_sizeof_uint(req->vclock_sync);
	}
	size += mp_sizeof_map(map_size);
	char *buf = xregion_alloc(&fiber()->gc, size);
	char *data = buf;
	data = mp_encode_map(data, map_size);
	data = mp_encode_uint(data, IPROTO_VCLOCK);
	data = mp_encode_vclock_ignore0(data, &req->vclock);
	data = mp_encode_uint(data, IPROTO_TERM);
	data = mp_encode_uint(data, req->term);
	if (req->vclock_sync != 0) {
		data = mp_encode_uint(data, IPROTO_VCLOCK_SYNC);
		data = mp_encode_uint(data, req->vclock_sync);
	}
	assert(data <= buf + size);
	row->body[0].iov_base = buf;
	row->body[0].iov_len = (data - buf);
	row->bodycnt = 1;
	row->type = IPROTO_OK;
}

int
xrow_decode_applier_heartbeat(const struct xrow_header *row,
			      struct applier_heartbeat *req)
{
	/*
	 * Not using replication_request, because heartbeats are too simple and
	 * are used often.
	 */
	memset(req, 0, sizeof(*req));
	if (row->bodycnt == 0) {
		diag_set(ClientError, ER_INVALID_MSGPACK, "request body");
		return -1;
	}
	const char *d = row->body[0].iov_base;
	if (mp_typeof(*d) != MP_MAP) {
		xrow_on_decode_err(row, ER_INVALID_MSGPACK, "request body");
		return -1;
	}
	uint32_t map_size = mp_decode_map(&d);
	for (uint32_t i = 0; i < map_size; i++) {
		if (mp_typeof(*d) != MP_UINT) {
			mp_next(&d);
			mp_next(&d);
			continue;
		}
		uint64_t key = mp_decode_uint(&d);
		switch (key) {
		case IPROTO_VCLOCK:
			if (mp_decode_vclock_ignore0(&d, &req->vclock) != 0) {
				xrow_on_decode_err(row, ER_INVALID_MSGPACK,
						   "invalid vclock");
				return -1;
			}
			break;
		case IPROTO_VCLOCK_SYNC:
			if (mp_typeof(*d) != MP_UINT) {
				xrow_on_decode_err(row, ER_INVALID_MSGPACK,
						   "invalid vclock sync");
				return -1;
			}
			req->vclock_sync = mp_decode_uint(&d);
			break;
		case IPROTO_TERM:
			if (mp_typeof(*d) != MP_UINT) {
				xrow_on_decode_err(row, ER_INVALID_MSGPACK,
						   "invalid term");
				return -1;
			}
			req->term = mp_decode_uint(&d);
			break;
		default:
			mp_next(&d);
		}
	}
	return 0;
}

void
xrow_encode_vclock_ignore0(struct xrow_header *row, const struct vclock *vclock)
{
	const struct replication_request base_req = {
		.vclock_ignore0 = (struct vclock *)vclock,
	};
	xrow_encode_replication_request(row, &base_req, IPROTO_OK);
}

void
xrow_encode_vclock(struct xrow_header *row, const struct vclock *vclock)
{
	const struct replication_request base_req = {
		.vclock = (struct vclock *)vclock,
	};
	xrow_encode_replication_request(row, &base_req, IPROTO_OK);
}

int
xrow_decode_vclock_ignore0(const struct xrow_header *row, struct vclock *vclock)
{
	vclock_create(vclock);
	struct replication_request base_req = {
		.vclock_ignore0 = vclock,
	};
	return xrow_decode_replication_request(row, &base_req);
}

void
xrow_encode_subscribe_response(struct xrow_header *row,
			       const struct subscribe_response *rsp)
{
	struct subscribe_response *cast = (struct subscribe_response *)rsp;
	const struct replication_request base_req = {
		.replicaset_uuid = &cast->replicaset_uuid,
		.replicaset_name = cast->replicaset_name,
		.vclock_ignore0 = &cast->vclock,
	};
	xrow_encode_replication_request(row, &base_req, IPROTO_OK);
}

int
xrow_decode_subscribe_response(const struct xrow_header *row,
			       struct subscribe_response *rsp)
{
	memset(rsp, 0, sizeof(*rsp));
	struct replication_request base_req = {
		.replicaset_uuid = &rsp->replicaset_uuid,
		.replicaset_name = rsp->replicaset_name,
		.vclock_ignore0 = &rsp->vclock,
	};
	return xrow_decode_replication_request(row, &base_req);
}

void
xrow_encode_type(struct xrow_header *row, uint16_t type)
{
	memset(row, 0, sizeof(*row));
	row->type = type;
}

void
greeting_encode(char *greetingbuf, uint32_t version_id,
		const struct tt_uuid *uuid, const char *salt, uint32_t salt_len)
{
	int h = IPROTO_GREETING_SIZE / 2;
	int r = snprintf(greetingbuf, h + 1, "Tarantool %s (Binary) ",
			 version_id_to_string(version_id));

	assert(r + UUID_STR_LEN < h);
	tt_uuid_to_string(uuid, greetingbuf + r);
	r += UUID_STR_LEN;
	memset(greetingbuf + r, ' ', h - r - 1);
	greetingbuf[h - 1] = '\n';

	assert(base64_encode_bufsize(salt_len, 0) + 1 < h);
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
		strlcpy(greeting->protocol, "Binary",
			sizeof(greeting->protocol));
	} else {
		return -1; /* Sorry, don't want to parse this greeting */
	}

	/* Decode salt for binary protocol */
	greeting->salt_len = base64_decode(greetingbuf + h, h - 1,
					   greeting->salt,
					   sizeof(greeting->salt));
	if (greeting->salt_len < GREETING_SALT_LEN_MIN ||
	    greeting->salt_len >= (uint32_t)h)
		return -1;

	return 0;
}
