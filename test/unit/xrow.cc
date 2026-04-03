/*
 * Copyright 2010-2015, Tarantool AUTHORS, please see AUTHORS file.
 *
 * Redistribution and use in source and iproto forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 *
 * 1. Redistributions of source code must retain the above
 *    copyright notice, this list of conditions and the
 *    following disclaimer.
 *
 * 2. Redistributions in iproto form must reproduce the above
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
extern "C" {
#define UNIT_TAP_COMPATIBLE 1
#include "unit.h"
} /* extern "C" */
#include "trivia/util.h"
#include "box/error.h"
#include "box/xrow.h"
#include "box/iproto_constants.h"
#include "tt_uuid.h"
#include "version.h"
#include "random.h"
#include "memory.h"
#include "mp_extension_types.h"
#include "fiber.h"

int
test_iproto_constants()
{
	/*
	 * Check that there are no gaps in the iproto_key_strs
	 * array. In a case of a gap the iproto_key_strs will be
	 * accessed by an index out of the range
	 * [0, IPROTO_KEY_MAX).
	 */
	for (int i = 0; i < iproto_key_MAX; ++i)
		(void) iproto_key_name((enum iproto_key) i);

	/* Same for iproto_type. */
	for (uint32_t i = 0; i < iproto_type_MAX; ++i)
		(void) iproto_type_name(i);
	return 0;
}

void
test_greeting()
{
	header();
	plan(40);

	char greetingbuf[IPROTO_GREETING_SIZE + 1];
	struct greeting source, greeting;

	/*
	 * Round-trip
	 */

	memset(&source, 0, sizeof(source));
	tt_uuid_create(&source.uuid);
	source.version_id = version_id(2 + rand() % 98, rand() % 100, 9);
	strcpy(source.protocol, "Binary");
	source.salt_len = 20 + rand() % 23;
	random_bytes(source.salt, source.salt_len);

	greeting_encode(greetingbuf, source.version_id, &source.uuid,
			source.salt, source.salt_len);
	int rc = greeting_decode(greetingbuf, &greeting);
	is(rc, 0, "round trip");
	is(greeting.version_id, source.version_id, "roundtrip.version_id");
	ok(strcmp(greeting.protocol, source.protocol) == 0, "roundtrip.protocol");
	ok(tt_uuid_is_equal(&greeting.uuid, &source.uuid), "roundtrip.uuid");
	is(greeting.salt_len, source.salt_len, "roundtrip.salt_len");
	is(memcmp(greeting.salt, source.salt, greeting.salt_len), 0,
		  "roundtrip.salt");

	/*
	 * Iproto greeting
	 */
	const char *greetingbuf_iproto =
	"Tarantool 1.6.7 (Binary) 7170b4af-c72f-4f07-8729-08fc678543a1  \n"
	"Fn5jMWKTvy/Xz4z/A2CYcxsBQhQTL0Ynd8wyqy0hZrs=                   \n";
	uint8_t iproto_salt[] = { 0x16, 0x7e, 0x63, 0x31, 0x62, 0x93, 0xbf, 0x2f,
			       0xd7, 0xcf, 0x8c, 0xff, 0x03, 0x60, 0x98, 0x73,
			       0x1b, 0x01, 0x42, 0x14, 0x13, 0x2f, 0x46, 0x27,
			       0x77, 0xcc, 0x32, 0xab, 0x2d, 0x21, 0x66, 0xbb };
	rc = greeting_decode(greetingbuf_iproto, &greeting);
	is(rc, 0, "decode iproto");
	is(greeting.version_id, version_id(1, 6, 7), "iproto.version_id");
	ok(strcmp(greeting.protocol, "Binary") == 0, "iproto.protocol");
	ok(strcmp(tt_uuid_str(&greeting.uuid),
		  "7170b4af-c72f-4f07-8729-08fc678543a1") == 0, "iproto.uuid");
	is(greeting.salt_len, sizeof(iproto_salt), "iproto.salt_len");
	is(memcmp(greeting.salt, iproto_salt, greeting.salt_len), 0,
		  "iproto.salt");

	/*
	 * Lua greeting
	 */
	const char *greetingbuf_lua =
	"Tarantool 1.6.7 (Lua console)                                  \n"
	"type 'help' for interactive help                               \n";
	rc = greeting_decode(greetingbuf_lua, &greeting);
	is(rc, 0, "decode lua");
	is(greeting.version_id, version_id(1, 6, 7), "lua.version_id");
	ok(strcmp(greeting.protocol, "Lua console") == 0, "lua.protocol");
	ok(tt_uuid_is_nil(&greeting.uuid), "lua.uuid");
	is(greeting.salt_len, 0, "lua.salt_len");

	/*
	 * Iproto greeting < 1.6.6
	 */
	const char *greetingbuf_iproto_166 =
	"Tarantool 1.6.6-201-g2495838                                   \n"
	"Fn5jMWKTvy/Xz4z/A2CYcxsBQhQTL0Ynd8wyqy0hZrs=                   \n";

	rc = greeting_decode(greetingbuf_iproto_166, &greeting);
	is(rc, 0, "decode iproto166");
	is(greeting.version_id, version_id(1, 6, 6), "iproto166.version_id");
	ok(strcmp(greeting.protocol, "Binary") == 0, "iproto166.protocol");
	ok(tt_uuid_is_nil(&greeting.uuid), "iproto166.uuid");
	is(greeting.salt_len, sizeof(iproto_salt), "iproto166.salt_len");
	is(memcmp(greeting.salt, iproto_salt, greeting.salt_len), 0,
		  "iproto166.salt");

	/*
	 * Lua greeting < 1.6.6
	 */
	const char *greetingbuf_lua_166 =
	"Tarantool 1.6.6-201-g2495838 (Lua console)                     \n"
	"type 'help' for interactive help                               \n";
	rc = greeting_decode(greetingbuf_lua_166, &greeting);
	is(rc, 0, "decode lua166");
	is(greeting.version_id, version_id(1, 6, 6), "lua166.version_id");
	ok(strcmp(greeting.protocol, "Lua console") == 0, "lua166.protocol");
	ok(tt_uuid_is_nil(&greeting.uuid), "lua166.uuid");
	is(greeting.salt_len, 0, "lua166.salt_len");

	/*
	 * Invalid
	 */
	const char *invalid[] = {
		"Tarantool 1.6.7 (Binary)                                       \n"
		"Fn5jMWKTvy/Xz4z/A2CYcxsBQhQTL0Ynd8wyqy0hZrs=                   \n",

		"Tarantool1.6.7 (Binary) 7170b4af-c72f-4f07-8729-08fc678543a1   \n"
		"Fn5jMWKTvy/Xz4z/A2CYcxsBQhQTL0Ynd8wyqy0hZrs=                   \n",

		"Tarantool 1.6.7(Binary) 7170b4af-c72f-4f07-8729-08fc678543a1   \n"
		"Fn5jMWKTvy/Xz4z/A2CYcxsBQhQTL0Ynd8wyqy0hZrs=                   \n",

		"Tarantool 1.6.7 (Binary)7170b4af-c72f-4f07-8729-08fc678543a1   \n"
		"Fn5jMWKTvy/Xz4z/A2CYcxsBQhQTL0Ynd8wyqy0hZrs=                   \n",

		"Tarantool 1.6.7 (Binary) 7170b4af-c72f-4f07-8729-08fc678543a1   "
		"Fn5jMWKTvy/Xz4z/A2CYcxsBQhQTL0Ynd8wyqy0hZrs=                    ",

		"Tarantool 1.6.7 (Binary) 7170b4af-c72f-4f07-8729-08fc678543    \n"
		"Fn5jMWKTvy/Xz4z/A2CYcxsBQhQTL0Ynd8wyqy0hZrs=                   \n",

		"Tarantool 1.6.7 (Binary) 7170b4af-c72f-4f07-8729-08fc678543a1  \n"
		"Fn5jMWKTvy/Xz4z                                                \n",

		"Tarantool 1.6.7 (Binary)                                       \n"
		"Fn5jMWKTvy/Xz4z/A2CYcxsBQhQTL0Ynd8wyqy0hZrs=                   \n",

		"Tarantool 1.6.7 (Binary 7170b4af-c72f-4f07-8729-08fc678543a1   \n"
		"Fn5jMWKTvy/Xz4z/A2CYcxsBQhQTL0Ynd8wyqy0hZrs=                   \n",

		"Tarantool 1.6.7 Binary 7170b4af-c72f-4f07-8729-08fc678543a1    \n"
		"Fn5jMWKTvy/Xz4z/A2CYcxsBQhQTL0Ynd8wyqy0hZrs=                   \n",

		"Apache 2.4.6 (Binary) 7170b4af-c72f-4f07-8729-08fc678543a1     \n"
		"Fn5jMWKTvy/Xz4z/A2CYcxsBQhQTL0Ynd8wyqy0hZrs=                   \n",

		"Tarantool 1.6.7                                                \n"
		"Fn5jMWKTvy/Xz4z/A2CYcxsBQhQTL0Ynd8wyqy0hZrs=                   \n",
	};

	int count = sizeof(invalid) / sizeof(invalid[0]);
	for (int i = 0; i < count; i++) {
		rc = greeting_decode(invalid[i], &greeting);
		isnt(rc, 0, "invalid %d", i);
	}

	check_plan();
	footer();
}

void
test_xrow_encode_decode()
{
	header();
	/* Test all possible 3-bit combinations. */
	const int bit_comb_count = 1 << 3;
	plan(1 + bit_comb_count * 15);
	struct xrow_header header;
	char buffer[2048];
	char *pos = mp_encode_uint(buffer, 300);
	is(xrow_decode(&header, (const char **)&pos, buffer + 100, true), -1,
	   "bad msgpack end");

	header.type = 100;
	header.replica_id = 200;
	header.lsn = 400;
	header.tm = 123.456;
	header.bodycnt = 0;
	header.tsn = header.lsn;
	uint64_t sync = 100500;
	uint64_t stream_id = 1;
	for (int opt_idx = 0; opt_idx < bit_comb_count; opt_idx++) {
		header.stream_id = stream_id++;
		header.is_commit = opt_idx & 0x01;
		header.wait_sync = opt_idx >> 1 & 0x01;
		header.wait_ack = opt_idx >> 2 & 0x01;
		int iovcnt;
		struct iovec vec[1];
		xrow_encode(&header, sync, /*fixheader_len=*/200, vec, &iovcnt);
		is(1, iovcnt, "encode");
		int fixheader_len = 200;
		pos = (char *)vec[0].iov_base + fixheader_len;
		uint32_t exp_map_size = 6;
		/*
		 * header.is_commit flag isn't encoded, since this row looks
		 * like a single-statement transaction.
		 */
		if (header.wait_sync || header.wait_ack)
			exp_map_size += 1;
		/* tsn is encoded explicitly in this case. */
		if (!header.is_commit)
			exp_map_size += 1;
		uint32_t size = mp_decode_map((const char **)&pos);
		is(size, exp_map_size, "header map size");

		struct xrow_header decoded_header;
		const char *pos = (const char *)vec[0].iov_base;
		pos += fixheader_len;
		const char *const begin = pos;
		const char *end = (const char *)vec[0].iov_base;
		end += vec[0].iov_len;
		is(xrow_decode(&decoded_header, &pos, end, true), 0,
		   "header decode");
		is(header.stream_id, decoded_header.stream_id, "decoded stream_id");
		is(header.is_commit, decoded_header.is_commit, "decoded is_commit");
		is(header.wait_sync, decoded_header.wait_sync, "decoded wait_sync");
		is(header.wait_ack, decoded_header.wait_ack, "decoded wait_ack");
		is(header.type, decoded_header.type, "decoded type");
		is(header.replica_id, decoded_header.replica_id, "decoded replica_id");
		is(header.lsn, decoded_header.lsn, "decoded lsn");
		is(header.tm, decoded_header.tm, "decoded tm");
		is(decoded_header.sync, sync, "decoded sync");
		is(decoded_header.bodycnt, 0, "decoded bodycnt");
		is(decoded_header.header, begin);
		is(decoded_header.header_end, end);
	}

	check_plan();
	footer();
}

void
test_request_str()
{
	header();
	plan(1);
	struct xrow_header header;
	header.replica_id = 5;
	header.lsn = 100;
	struct request request;
	memset(&request, 0, sizeof(request));
	request.header = &header;
	request.type = 1;
	request.space_id = 512;
	request.index_id = 1;

	char buffer[2048];
	request.key = buffer;
	char *pos = mp_encode_array(buffer, 1);
	pos = mp_encode_uint(pos, 200);

	request.tuple = pos;
	pos = mp_encode_array(pos, 1);
	pos = mp_encode_uint(pos, 300);

	request.ops = pos;
	pos = mp_encode_array(pos, 1);
	pos = mp_encode_uint(pos, 400);

	request.fetch_position = true;

	request.after_position = pos;
	pos = mp_encode_str(pos, "position", 8);
	request.after_position_end = pos;

	request.after_tuple = request.tuple;

	request.begin_key = request.key;
	request.end_key = request.key;

	is(strcmp("{type: 'SELECT', replica_id: 5, lsn: 100, "
		  "space_id: 512, index_id: 1, "
		  "key: [200], tuple: [300], ops: [400], "
		  "fetch_position: true, after_position: \"position\", "
		  "after_tuple: [300], begin_key: [200], end_key: [200]}",
		  request_str(&request)), 0, "request_str");

	check_plan();
	footer();
}

/**
 * The compiler doesn't have to preserve bitfields order,
 * still we rely on it for convenience sake.
 */
static void
test_xrow_fields()
{
	header();
	plan(6);

	struct xrow_header header;

	memset(&header, 0, sizeof(header));

	header.is_commit = true;
	is(header.flags, IPROTO_FLAG_COMMIT, "header.is_commit -> COMMIT");
	header.is_commit = false;

	header.wait_sync = true;
	is(header.flags, IPROTO_FLAG_WAIT_SYNC, "header.wait_sync -> WAIT_SYNC");
	header.wait_sync = false;

	header.wait_ack = true;
	is(header.flags, IPROTO_FLAG_WAIT_ACK, "header.wait_ack -> WAIT_ACK");
	header.wait_ack = false;

	header.flags = IPROTO_FLAG_COMMIT;
	ok(header.is_commit && !header.wait_sync && !header.wait_ack, "COMMIT -> header.is_commit");

	header.flags = IPROTO_FLAG_WAIT_SYNC;
	ok(!header.is_commit && header.wait_sync && !header.wait_ack, "WAIT_SYNC -> header.wait_sync");

	header.flags = IPROTO_FLAG_WAIT_ACK;
	ok(!header.is_commit && !header.wait_sync && header.wait_ack, "WAIT_ACK -> header.wait_ack");

	check_plan();
	footer();
}

/**
 * Test that xrow_encode_dml() encodes all request fields properly.
 */
static void
test_xrow_encode_dml(void)
{
	header();
	plan(30);

	struct request r;
	memset(&r, 0, sizeof(r));
	r.space_id = 666;
	r.index_id = 222;
	r.index_base = 123;
	r.key = "key";
	r.key_end = r.key + strlen(r.key);
	r.ops = "ops";
	r.ops_end = r.ops + strlen(r.ops);
	r.tuple_meta = "meta";
	r.tuple_meta_end = r.tuple_meta + strlen(r.tuple_meta);
	r.tuple = "tuple";
	r.tuple_end = r.tuple + strlen(r.tuple);
	r.old_tuple = "old tuple";
	r.old_tuple_end = r.old_tuple + strlen(r.old_tuple);
	r.new_tuple = "new tuple";
	r.new_tuple_end = r.new_tuple + strlen(r.new_tuple);
	r.arrow_ipc = "Arrow IPC";
	r.arrow_ipc_end = r.arrow_ipc + strlen(r.arrow_ipc);
	r.begin_key = "begin key";
	r.begin_key_end = r.begin_key + strlen(r.begin_key);
	r.end_key = "end key";
	r.end_key_end = r.end_key + strlen(r.end_key);

	int iovcnt;
	struct iovec iov[1];
	xrow_encode_dml(&r, &fiber()->gc, iov, &iovcnt);
	is(iovcnt, 1, "xrow_encode_dml rc");
	const char *data = (const char *)iov[0].iov_base;
	int map_size = mp_decode_map(&data);
	is(map_size, 12, "decoded request map");
	int decoded_key_count = 0;

	is(mp_decode_uint(&data), IPROTO_SPACE_ID, "decoded space id key");
	is(mp_decode_uint(&data), r.space_id, "decoded space id");
	decoded_key_count++;

	is(mp_decode_uint(&data), IPROTO_INDEX_ID, "decoded index id key");
	is(mp_decode_uint(&data), r.index_id, "decoded index id");
	decoded_key_count++;

	is(mp_decode_uint(&data), IPROTO_INDEX_BASE, "decoded index base key");
	is(mp_decode_uint(&data), (uint64_t)r.index_base, "decoded index base");
	decoded_key_count++;

	is(mp_decode_uint(&data), IPROTO_KEY, "decoded iproto key");
	is(memcmp(data, r.key, strlen(r.key)), 0, "decoded key");
	data += strlen(r.key);
	decoded_key_count++;

	is(mp_decode_uint(&data), IPROTO_OPS, "decoded ops key");
	is(memcmp(data, r.ops, strlen(r.ops)), 0, "decoded ops");
	data += strlen(r.ops);
	decoded_key_count++;

	is(mp_decode_uint(&data), IPROTO_TUPLE_META, "decoded meta key");
	is(memcmp(data, r.tuple_meta, strlen(r.tuple_meta)), 0,
	   "decoded meta");
	data += strlen(r.tuple_meta);
	decoded_key_count++;

	is(mp_decode_uint(&data), IPROTO_TUPLE, "decoded tuple key");
	is(memcmp(data, r.tuple, strlen(r.tuple)), 0, "decoded tuple");
	data += strlen(r.tuple);
	decoded_key_count++;

	is(mp_decode_uint(&data), IPROTO_OLD_TUPLE, "decoded old tuple key");
	is(memcmp(data, r.old_tuple, strlen(r.old_tuple)), 0,
	   "decoded old tuple");
	data += strlen(r.old_tuple);
	decoded_key_count++;

	is(mp_decode_uint(&data), IPROTO_NEW_TUPLE, "decoded new tuple key");
	is(memcmp(data, r.new_tuple, strlen(r.new_tuple)), 0,
	   "decoded new tuple");
	data += strlen(r.new_tuple);
	decoded_key_count++;

	is(mp_decode_uint(&data), IPROTO_ARROW, "decoded Arrow key");
	int8_t arrow_ext_type;
	is(mp_decode_extl(&data, &arrow_ext_type), strlen(r.arrow_ipc),
	   "decoded Arrow IPC size");
	is(arrow_ext_type, MP_ARROW, "decoded Arrow IPC type");
	is(memcmp(data, r.arrow_ipc, strlen(r.arrow_ipc)), 0,
	   "decoded Arrow IPC data");
	data += strlen(r.arrow_ipc);
	decoded_key_count++;

	is(mp_decode_uint(&data), IPROTO_BEGIN_KEY, "decoded begin key key");
	is(memcmp(data, r.begin_key, strlen(r.begin_key)), 0,
	   "decoded begin key");
	data += strlen(r.begin_key);
	decoded_key_count++;

	is(mp_decode_uint(&data), IPROTO_END_KEY, "decoded end key key");
	is(memcmp(data, r.end_key, strlen(r.end_key)), 0, "decoded end key");
	data += strlen(r.end_key);
	decoded_key_count++;

	is(data - (char *)iov[0].iov_base, (ptrdiff_t)iov[0].iov_len,
	   "decoded all data");
	is(decoded_key_count, map_size, "decoded all keys");

	check_plan();
	footer();
}

/**
 * Check the error message of the last diag set.
 */
#define diag_is(expected_error_message, ...) do { \
	box_error_t *e = box_error_last(); \
	const char *error_message = box_error_message(e); \
	ok(strcmp(error_message, expected_error_message) == 0, ##__VA_ARGS__); \
} while (false)

/**
 * Call xrow_decode_dml() and expect it to fail with an error message.
 */
static void
xrow_decode_dml_fail(struct xrow_header *header, struct request *r,
		     const char *case_message, const char *error_message)
{
	uint64_t key_map = dml_request_key_map(header->type);
	is(xrow_decode_dml(header, r, key_map), -1, "%s: fail", case_message);
	diag_is(error_message, "%s: diag", case_message);
	box_error_clear();
}

/**
 * Test that xrow_decode_dml() decodes IPROTO keys properly.
 */
static void
test_xrow_decode_dml_keys()
{
	header();
	plan(106);

	const size_t buf_size = 1024;
	char buf[buf_size];

	/* The stuff to pass to xrow_decode_dml calls. */
	uint64_t empty_key_map = 0;
	struct request r;
	struct xrow_header header;
	memset(&header, 0, sizeof(header));
	/* DML with an empty key map. */
	header.type = IPROTO_NOP;
	header.bodycnt = 1;
	header.body[0].iov_base = buf;

	/* Space ID: MP_UINT. */
	header.body[0].iov_len = mp_format(buf, buf_size, "{%u%u}",
					   IPROTO_SPACE_ID, 1);
	is(xrow_decode_dml(&header, &r, empty_key_map), 0,
	   "MP_UINT space ID is valid");
	is(r.space_id, 1, "MP_UINT space ID is decoded");

	/* Space ID: MP_INT. */
	header.body[0].iov_len = mp_format(buf, buf_size, "{%u%d}",
					   IPROTO_SPACE_ID, -1);
	xrow_decode_dml_fail(&header, &r, "MP_INT space ID is invalid",
			     "Invalid MsgPack - packet body");

	/* Index ID: MP_UINT. */
	header.body[0].iov_len = mp_format(buf, buf_size, "{%u%u}",
					   IPROTO_INDEX_ID, 1);
	is(xrow_decode_dml(&header, &r, empty_key_map), 0,
	   "MP_UINT index ID is valid");
	is(r.index_id, 1, "MP_UINT index ID is decoded");

	/* Index ID: MP_STR. */
	header.body[0].iov_len = mp_format(buf, buf_size, "{%u%s}",
					   IPROTO_INDEX_ID, "str");
	xrow_decode_dml_fail(&header, &r, "MP_STR index ID is invalid",
			     "Invalid MsgPack - packet body");

	/* Offset: MP_UINT. */
	header.body[0].iov_len = mp_format(buf, buf_size, "{%u%u}",
					   IPROTO_OFFSET, 1);
	is(xrow_decode_dml(&header, &r, empty_key_map), 0,
	   "MP_UINT offset is valid");
	is(r.offset, 1, "MP_UINT offset is decoded");

	/* Offset: MP_NIL. */
	header.body[0].iov_len = mp_format(buf, buf_size, "{%uNIL}",
					   IPROTO_OFFSET);
	xrow_decode_dml_fail(&header, &r, "MP_NIL offset is invalid",
			     "Invalid MsgPack - packet body");

	/* Index base: MP_UINT. */
	header.body[0].iov_len = mp_format(buf, buf_size, "{%u%u}",
					   IPROTO_INDEX_BASE, 1);
	is(xrow_decode_dml(&header, &r, empty_key_map), 0,
	   "MP_UINT index base is valid");
	is(r.index_base, 1, "MP_UINT index base is decoded");

	/* Index base: MP_BOOL. */
	header.body[0].iov_len = mp_format(buf, buf_size, "{%u%b}",
					   IPROTO_INDEX_BASE, true);
	xrow_decode_dml_fail(&header, &r, "MP_BOOL index base is invalid",
			     "Invalid MsgPack - packet body");

	/* Limit: MP_UINT. */
	header.body[0].iov_len = mp_format(buf, buf_size, "{%u%u}",
					   IPROTO_LIMIT, 1);
	is(xrow_decode_dml(&header, &r, empty_key_map), 0,
	   "MP_UINT limit is valid");
	is(r.limit, 1, "MP_UINT limit is decoded");

	/* Limit: MP_STR. */
	header.body[0].iov_len = mp_format(buf, buf_size, "{%u%s}",
					   IPROTO_LIMIT, "str");
	xrow_decode_dml_fail(&header, &r, "MP_STR limit is invalid",
			     "Invalid MsgPack - packet body");

	/* Iterator: MP_UINT. */
	header.body[0].iov_len = mp_format(buf, buf_size, "{%u%u}",
					   IPROTO_ITERATOR, 1);
	is(xrow_decode_dml(&header, &r, empty_key_map), 0,
	   "MP_UINT iterator is valid");
	is(r.iterator, 1, "MP_UINT iterator is decoded");

	/* Iterator: MP_FLOAT. */
	header.body[0].iov_len = mp_format(buf, buf_size, "{%u%f}",
					   IPROTO_ITERATOR, 0.1);
	xrow_decode_dml_fail(&header, &r, "MP_FLOAT iterator is invalid",
			     "Invalid MsgPack - packet body");

	/* Fetch position: MP_BOOL. */
	header.body[0].iov_len = mp_format(buf, buf_size, "{%u%b}",
					   IPROTO_FETCH_POSITION, true);
	is(xrow_decode_dml(&header, &r, empty_key_map), 0,
	   "MP_BOOL fetch position is valid");
	is(r.fetch_position, true, "MP_BOOL fetch position is decoded");

	/* Fetch position: MP_DOUBLE. */
	header.body[0].iov_len = mp_format(buf, buf_size, "{%u%lf}",
					   IPROTO_FETCH_POSITION, 0.1);
	xrow_decode_dml_fail(&header, &r, "MP_DOUBLE fetch position is invalid",
			     "Invalid MsgPack - packet body");

	/* Tuple: MP_ARRAY. */
	header.body[0].iov_len = mp_format(buf, buf_size, "{%u[%u]}",
					   IPROTO_TUPLE, 2);
	is(xrow_decode_dml(&header, &r, empty_key_map), 0,
	   "MP_ARRAY tuple is valid");
	is(mp_decode_array(&r.tuple), 1, "MP_ARRAY tuple len is valid");
	is(mp_decode_uint(&r.tuple), 2, "MP_ARRAY tuple data is valid");
	is(r.tuple, r.tuple_end, "MP_ARRAY tuple size is valid");

	/* Tuple: MP_MAP. */
	header.body[0].iov_len = mp_format(buf, buf_size, "{%u{%u%u}}",
					   IPROTO_TUPLE, 2, 3);
	xrow_decode_dml_fail(&header, &r, "MP_MAP tuple is invalid",
			     "Invalid MsgPack - packet body");

	/* Key: MP_ARRAY. */
	header.body[0].iov_len = mp_format(buf, buf_size, "{%u[%u]}",
					   IPROTO_KEY, 2);
	is(xrow_decode_dml(&header, &r, empty_key_map), 0,
	   "MP_ARRAY key is valid");
	is(mp_decode_array(&r.key), 1, "MP_ARRAY key len is valid");
	is(mp_decode_uint(&r.key), 2, "MP_ARRAY key data is valid");
	is(r.key, r.key_end, "MP_ARRAY key size is valid");

	/* Key: MP_STR. */
	header.body[0].iov_len = mp_format(buf, buf_size, "{%u%s}",
					   IPROTO_KEY, "str");
	xrow_decode_dml_fail(&header, &r, "MP_STR key is invalid",
			     "Invalid MsgPack - packet body");

	/* Ops: MP_ARRAY. */
	header.body[0].iov_len = mp_format(buf, buf_size, "{%u[%u]}",
					   IPROTO_OPS, 2);
	is(xrow_decode_dml(&header, &r, empty_key_map), 0,
	   "MP_ARRAY ops is valid");
	is(mp_decode_array(&r.ops), 1, "MP_ARRAY ops len is valid");
	is(mp_decode_uint(&r.ops), 2, "MP_ARRAY ops data is valid");
	is(r.ops, r.ops_end, "MP_ARRAY ops size is valid");

	/* Ops: MP_NIL. */
	header.body[0].iov_len = mp_format(buf, buf_size, "{%uNIL}",
					   IPROTO_OPS);
	xrow_decode_dml_fail(&header, &r, "MP_NIL ops is invalid",
			     "Invalid MsgPack - packet body");

	/* Tuple meta: MP_MAP. */
	header.body[0].iov_len = mp_format(buf, buf_size, "{%u{%u%u}}",
					   IPROTO_TUPLE_META, 2, 3);
	is(xrow_decode_dml(&header, &r, empty_key_map), 0,
	   "MP_MAP tuple meta is valid");
	is(mp_decode_map(&r.tuple_meta), 1, "MP_MAP tuple meta len is valid");
	is(mp_decode_uint(&r.tuple_meta), 2, "MP_MAP tuple meta key is valid");
	is(mp_decode_uint(&r.tuple_meta), 3, "MP_MAP tuple meta val is valid");
	is(r.tuple_meta, r.tuple_meta_end, "MP_MAP tuple meta size is valid");

	/* Tuple meta: MP_ARRAY. */
	header.body[0].iov_len = mp_format(buf, buf_size, "{%u[%u]}",
					   IPROTO_TUPLE_META, 2);
	xrow_decode_dml_fail(&header, &r, "MP_ARRAY tuple meta is invalid",
			     "Invalid MsgPack - packet body");

	/* Old tuple: MP_ARRAY. */
	header.body[0].iov_len = mp_format(buf, buf_size, "{%u[%u]}",
					   IPROTO_OLD_TUPLE, 2);
	is(xrow_decode_dml(&header, &r, empty_key_map), 0,
	   "MP_ARRAY old tuple is valid");
	is(mp_decode_array(&r.old_tuple), 1, "MP_ARRAY old tuple len is valid");
	is(mp_decode_uint(&r.old_tuple), 2, "MP_ARRAY old tuple data is valid");
	is(r.old_tuple, r.old_tuple_end, "MP_ARRAY old tuple size is valid");

	/* Old tuple: MP_MAP. */
	header.body[0].iov_len = mp_format(buf, buf_size, "{%u{%u%u}}",
					   IPROTO_OLD_TUPLE, 2, 3);
	xrow_decode_dml_fail(&header, &r, "MP_MAP old tuple is invalid",
			     "Invalid MsgPack - packet body");

	/* New tuple: MP_ARRAY. */
	header.body[0].iov_len = mp_format(buf, buf_size, "{%u[%u]}",
					   IPROTO_NEW_TUPLE, 2);
	is(xrow_decode_dml(&header, &r, empty_key_map), 0,
	   "MP_ARRAY new tuple is valid");
	is(mp_decode_array(&r.new_tuple), 1, "MP_ARRAY new tuple len is valid");
	is(mp_decode_uint(&r.new_tuple), 2, "MP_ARRAY new tuple data is valid");
	is(r.new_tuple, r.new_tuple_end, "MP_ARRAY new tuple size is valid");

	/* New tuple: MP_MAP. */
	header.body[0].iov_len = mp_format(buf, buf_size, "{%u{%u%u}}",
					   IPROTO_NEW_TUPLE, 2, 3);
	xrow_decode_dml_fail(&header, &r, "MP_MAP new tuple is invalid",
			     "Invalid MsgPack - packet body");

	/* After position: MP_STR. */
	header.body[0].iov_len = mp_format(buf, buf_size, "{%u%s}",
					   IPROTO_AFTER_POSITION, "str");
	is(xrow_decode_dml(&header, &r, empty_key_map), 0,
	   "MP_STR after position is valid");
	is(mp_decode_strl(&r.after_position), strlen("str"),
	   "MP_STR after position len is valid");
	is(memcmp(r.after_position, "str", strlen("str")), 0,
	   "MP_STR after position data is valid");
	r.after_position += strlen("str");
	is(r.after_position, r.after_position_end,
	   "MP_STR after position size is valid");

	/* After position: MP_UINT. */
	header.body[0].iov_len = mp_format(buf, buf_size, "{%u%u}",
					   IPROTO_AFTER_POSITION, 2);
	xrow_decode_dml_fail(&header, &r, "MP_UINT after position",
			     "Invalid MsgPack - packet body");

	/* After tuple: MP_ARRAY. */
	header.body[0].iov_len = mp_format(buf, buf_size, "{%u[%u]}",
					   IPROTO_AFTER_TUPLE, 2);
	is(xrow_decode_dml(&header, &r, empty_key_map), 0,
	   "MP_ARRAY after tuple is valid");
	is(mp_decode_array(&r.after_tuple), 1,
	   "MP_ARRAY after tuple len is valid");
	is(mp_decode_uint(&r.after_tuple), 2,
	   "MP_ARRAY after tuple data is valid");
	is(r.after_tuple, r.after_tuple_end,
	   "MP_ARRAY after tuple size is valid");

	/* After tuple: MP_MAP. */
	header.body[0].iov_len = mp_format(buf, buf_size, "{%u{%u%u}}",
					   IPROTO_AFTER_TUPLE, 2, 3);
	xrow_decode_dml_fail(&header, &r, "MP_MAP after tuple is invalid",
			     "Invalid MsgPack - packet body");

	/* Space name: MP_STR. */
	header.body[0].iov_len = mp_format(buf, buf_size, "{%u%s}",
					   IPROTO_SPACE_NAME, "str");
	is(xrow_decode_dml(&header, &r, empty_key_map), 0,
	   "MP_STR space name is valid");
	is(r.space_name_len, strlen("str"), "MP_STR space name len is valid");
	is(memcmp(r.space_name, "str", strlen("str")), 0,
	   "MP_STR space name data is valid");

	/* Space name: MP_UINT. */
	header.body[0].iov_len = mp_format(buf, buf_size, "{%u%u}",
					   IPROTO_SPACE_NAME, 2);
	xrow_decode_dml_fail(&header, &r, "MP_UINT space name is invalid",
			     "Invalid MsgPack - packet body");

	/* Index name: MP_STR. */
	header.body[0].iov_len = mp_format(buf, buf_size, "{%u%s}",
					   IPROTO_INDEX_NAME, "str");
	is(xrow_decode_dml(&header, &r, empty_key_map), 0,
	   "MP_STR index name is valid");
	is(r.index_name_len, strlen("str"), "MP_STR index name len is valid");
	is(memcmp(r.index_name, "str", strlen("str")), 0,
	   "MP_STR index name data is valid");

	/* Index name: MP_UINT. */
	header.body[0].iov_len = mp_format(buf, buf_size, "{%u%u}",
					   IPROTO_INDEX_NAME, 2);
	xrow_decode_dml_fail(&header, &r, "MP_UINT index name is invalid",
			     "Invalid MsgPack - packet body");

	/* Arrow: MP_EXT(MP_ARROW). */
	char mp_ext_arrow[6]; /* Prevents clang's extension warning. */
	fail_unless(lengthof(mp_ext_arrow) == mp_sizeof_ext(strlen("ext")));
	mp_encode_ext(mp_ext_arrow, MP_ARROW, "ext", strlen("ext"));
	header.body[0].iov_len = mp_format(buf, buf_size, "{%u%p}",
					   IPROTO_ARROW, mp_ext_arrow);
	is(xrow_decode_dml(&header, &r, empty_key_map), 0,
	   "MP_EXT(MP_ARROW) Arrow IPC is valid");
	is(r.arrow_ipc_end - r.arrow_ipc, strlen("ext"),
	   "MP_EXT(MP_ARROW) Arrow IPC size is valid");
	is(memcmp(r.arrow_ipc, "ext", strlen("ext")), 0,
	   "MP_EXT(MP_ARROW) Arrow IPC data is valid");

	/* Arrow: MP_EXT(MP_TUPLE). */
	char mp_ext_tuple[6]; /* Prevents clang's extension warning. */
	fail_unless(lengthof(mp_ext_tuple) == mp_sizeof_ext(strlen("ext")));
	mp_encode_ext(mp_ext_tuple, MP_TUPLE, "ext", strlen("ext"));
	header.body[0].iov_len = mp_format(buf, buf_size, "{%u%p}",
					   IPROTO_ARROW, mp_ext_tuple);
	xrow_decode_dml_fail(&header, &r,
			     "MP_EXT(MP_TUPLE) Arrow IPC is invalid",
			     "Invalid MsgPack - packet body");

	/* Arrow: MP_MAP. */
	header.body[0].iov_len = mp_format(buf, buf_size, "{%u{%u%u}}",
					   IPROTO_ARROW, 2, 3);
	xrow_decode_dml_fail(&header, &r, "MP_MAP Arrow IPC is invalid",
			     "Invalid MsgPack - packet body");

	/* Begin key: MP_ARRAY. */
	header.body[0].iov_len = mp_format(buf, buf_size, "{%u[%u]}",
					   IPROTO_BEGIN_KEY, 2);
	is(xrow_decode_dml(&header, &r, empty_key_map), 0,
	   "MP_ARRAY begin key is valid");
	is(mp_decode_array(&r.begin_key), 1, "MP_ARRAY begin key len is valid");
	is(mp_decode_uint(&r.begin_key), 2, "MP_ARRAY begin key data is valid");
	is(r.begin_key, r.begin_key_end, "MP_ARRAY begin key size is valid");

	/* Begin key: MP_UINT. */
	header.body[0].iov_len = mp_format(buf, buf_size, "{%u%u}",
					   IPROTO_BEGIN_KEY, 1);
	xrow_decode_dml_fail(&header, &r, "MP_UINT begin key is invalid",
			     "Invalid MsgPack - packet body");

	/* End key: MP_ARRAY. */
	header.body[0].iov_len = mp_format(buf, buf_size, "{%u[%u]}",
					   IPROTO_END_KEY, 2);
	is(xrow_decode_dml(&header, &r, empty_key_map), 0,
	   "MP_ARRAY end key is valid");
	is(mp_decode_array(&r.end_key), 1, "MP_ARRAY end key len is valid");
	is(mp_decode_uint(&r.end_key), 2, "MP_ARRAY end key data is valid");
	is(r.end_key, r.end_key_end, "MP_ARRAY end key size is valid");

	/* End key: MP_MAP. */
	header.body[0].iov_len = mp_format(buf, buf_size, "{%u{%u%u}}",
					   IPROTO_END_KEY, 2, 3);
	xrow_decode_dml_fail(&header, &r, "MP_MAP end key is invalid",
			     "Invalid MsgPack - packet body");

	check_plan();
	footer();
}

/**
 * Test that xrow_decode_dml() decodes IPROTO requests properly.
 */
static void
test_xrow_decode_dml_requests()
{
	header();
	plan(105);

	const size_t buf_size = 1024;
	char buf[buf_size];

	/* The stuff to pass to xrow_decode_dml calls. */
	struct request r;
	uint64_t empty_key_map = 0;
	struct xrow_header header;
	memset(&header, 0, sizeof(header));
	header.bodycnt = 1;
	header.body[0].iov_base = buf;

	/* select() */
	header.type = IPROTO_SELECT;
	header.body[0].iov_len = mp_format(buf, buf_size, "{}");
	xrow_decode_dml_fail(&header, &r, "select()",
			     "Missing mandatory field 'SPACE_ID' in request");

	/* select(space_id) */
	header.body[0].iov_len = mp_format(buf, buf_size, "{%u%u}",
					   IPROTO_SPACE_ID, 1);
	xrow_decode_dml_fail(&header, &r, "select(space_id)",
			     "Missing mandatory field 'LIMIT' in request");

	/* select(space_id, limit) */
	header.body[0].iov_len = mp_format(buf, buf_size, "{%u%u%u%u}",
					   IPROTO_SPACE_ID, 1,
					   IPROTO_LIMIT, 2);
	xrow_decode_dml_fail(&header, &r, "select(space_id, limit)",
			     "Missing mandatory field 'KEY' in request");

	/* select(key, space_id, limit) */
	header.body[0].iov_len = mp_format(buf, buf_size, "{%u[%u]%u%u%u%u}",
					   IPROTO_KEY, 2,
					   IPROTO_SPACE_ID, 3,
					   IPROTO_LIMIT, 4);
	is(xrow_decode_dml(&header, &r, dml_request_key_map(header.type)), 0,
	   "select(key, space_id, limit): success");
	is(r.type, IPROTO_SELECT, "select(key, space_id, limit): request type");
	is(r.space_id, 3, "select(key, space_id, limit): space ID");
	is(mp_decode_array(&r.key), 1, "select(key, space_id, limit): key len");
	is(mp_decode_uint(&r.key), 2, "select(key, space_id, limit): key[0]");
	is(r.key, r.key_end, "select(key, space_id, limit): key size");
	is(r.limit, 4, "select(key, space_id, limit): limit");

	/* insert() */
	header.type = IPROTO_INSERT;
	header.body[0].iov_len = mp_format(buf, buf_size, "{}");
	xrow_decode_dml_fail(&header, &r, "insert()",
			     "Missing mandatory field 'SPACE_ID' in request");

	/* insert(space_id) */
	header.body[0].iov_len = mp_format(buf, buf_size, "{%u%u}",
					   IPROTO_SPACE_ID, 1);
	xrow_decode_dml_fail(&header, &r, "insert(space_id)",
			     "Missing mandatory field 'TUPLE' in request");

	/* insert(space_id, tuple) */
	header.body[0].iov_len = mp_format(buf, buf_size, "{%u%u%u[%u%u]}",
					   IPROTO_SPACE_ID, 1,
					   IPROTO_TUPLE, 3, 4);
	is(xrow_decode_dml(&header, &r, dml_request_key_map(header.type)), 0,
	   "insert(space_id, tuple): success");
	is(r.type, IPROTO_INSERT, "insert(space_id, tuple): request type");
	is(r.space_id, 1, "insert(space_id, tuple): space ID");
	is(mp_decode_array(&r.tuple), 2, "insert(space_id, tuple): tuple len");
	is(mp_decode_uint(&r.tuple), 3, "insert(space_id, tuple): tuple[0]");
	is(mp_decode_uint(&r.tuple), 4, "insert(space_id, tuple): tuple[1]");
	is(r.tuple, r.tuple_end, "insert(space_id, tuple): tuple size");

	/* replace() */
	header.type = IPROTO_REPLACE;
	header.body[0].iov_len = mp_format(buf, buf_size, "{}");
	xrow_decode_dml_fail(&header, &r, "replace()",
			     "Missing mandatory field 'SPACE_ID' in request");

	/* replace(space_id) */
	header.body[0].iov_len = mp_format(buf, buf_size, "{%u%u}",
					   IPROTO_SPACE_ID, 1);
	xrow_decode_dml_fail(&header, &r, "replace(space_id)",
			     "Missing mandatory field 'TUPLE' in request");

	/* replace(space_id, tuple) */
	header.body[0].iov_len = mp_format(buf, buf_size, "{%u%u%u[%u%u]}",
					   IPROTO_SPACE_ID, 1,
					   IPROTO_TUPLE, 3, 4);
	is(xrow_decode_dml(&header, &r, dml_request_key_map(header.type)), 0,
	   "replace(space_id, tuple): success");
	is(r.type, IPROTO_REPLACE, "replace(space_id, tuple): request type");
	is(r.space_id, 1, "replace(space_id, tuple): space ID");
	is(mp_decode_array(&r.tuple), 2, "replace(space_id, tuple): tuple len");
	is(mp_decode_uint(&r.tuple), 3, "replace(space_id, tuple): tuple[0]");
	is(mp_decode_uint(&r.tuple), 4, "replace(space_id, tuple): tuple[1]");
	is(r.tuple, r.tuple_end, "replace(space_id, tuple): tuple size");

	/* update() */
	header.type = IPROTO_UPDATE;
	header.body[0].iov_len = mp_format(buf, buf_size, "{}");
	xrow_decode_dml_fail(&header, &r, "update()",
			     "Missing mandatory field 'SPACE_ID' in request");

	/* update(space_id) */
	header.body[0].iov_len = mp_format(buf, buf_size, "{%u%u}",
					   IPROTO_SPACE_ID, 1);
	xrow_decode_dml_fail(&header, &r, "update(space_id)",
			     "Missing mandatory field 'KEY' in request");

	/* update(space_id, key) */
	header.body[0].iov_len = mp_format(buf, buf_size, "{%u%u%u[%u%u]}",
					   IPROTO_SPACE_ID, 1,
					   IPROTO_KEY, 3, 4);
	xrow_decode_dml_fail(&header, &r, "update(space_id, key)",
			     "Missing mandatory field 'TUPLE' in request");

	/* update(space_id, key, tuple) */
	header.body[0].iov_len = mp_format(buf, buf_size,
					   "{%u%u%u[%u%u]%u[%u%u]}",
					   IPROTO_SPACE_ID, 1,
					   IPROTO_KEY, 3, 4,
					   IPROTO_TUPLE, 5, 6);
	is(xrow_decode_dml(&header, &r, dml_request_key_map(header.type)), 0,
	   "update(space_id, key, tuple): success");
	is(r.type, IPROTO_UPDATE, "update(space_id, key, tuple): request type");
	is(r.space_id, 1, "update(space_id, key, tuple): space ID");
	is(mp_decode_array(&r.key), 2, "update(space_id, key, tuple): key len");
	is(mp_decode_uint(&r.key), 3, "update(space_id, key, tuple): key[0]");
	is(mp_decode_uint(&r.key), 4, "update(space_id, key, tuple): key[1]");
	is(r.key, r.key_end, "update(space_id, key, tuple): key size");
	is(mp_decode_array(&r.tuple), 2,
	   "update(space_id, key, tuple): tuple len");
	is(mp_decode_uint(&r.tuple), 5,
	   "update(space_id, key, tuple): tuple[0]");
	is(mp_decode_uint(&r.tuple), 6,
	   "update(space_id, key, tuple): tuple[1]");
	is(r.tuple, r.tuple_end,
	   "update(space_id, key, tuple): tuple size");

	/* delete() */
	header.type = IPROTO_DELETE;
	header.body[0].iov_len = mp_format(buf, buf_size, "{}");
	xrow_decode_dml_fail(&header, &r, "delete()",
			     "Missing mandatory field 'SPACE_ID' in request");

	/* delete(space_id) */
	header.body[0].iov_len = mp_format(buf, buf_size, "{%u%u}",
					   IPROTO_SPACE_ID, 1);
	xrow_decode_dml_fail(&header, &r, "delete(space_id)",
			     "Missing mandatory field 'KEY' in request");

	/* delete(space_id, key) */
	header.body[0].iov_len = mp_format(buf, buf_size, "{%u%u%u[%u%u]}",
					   IPROTO_SPACE_ID, 1,
					   IPROTO_KEY, 3, 4);
	is(xrow_decode_dml(&header, &r, dml_request_key_map(header.type)), 0,
	   "delete(space_id, key): success");
	is(r.type, IPROTO_DELETE, "delete(space_id, key): request type");
	is(r.space_id, 1, "delete(space_id, key): space ID");
	is(mp_decode_array(&r.key), 2, "delete(space_id, key): key len");
	is(mp_decode_uint(&r.key), 3, "delete(space_id, key): key[0]");
	is(mp_decode_uint(&r.key), 4, "delete(space_id, key): key[1]");
	is(r.key, r.key_end, "delete(space_id, key): key size");

	/* upsert() */
	header.type = IPROTO_UPSERT;
	header.body[0].iov_len = mp_format(buf, buf_size, "{}");
	xrow_decode_dml_fail(&header, &r, "upsert()",
			     "Missing mandatory field 'SPACE_ID' in request");

	/* upsert(space_id) */
	header.body[0].iov_len = mp_format(buf, buf_size, "{%u%u}",
					   IPROTO_SPACE_ID, 1);
	xrow_decode_dml_fail(&header, &r, "upsert(tuple)",
			     "Missing mandatory field 'TUPLE' in request");

	/* upsert(space_id, tuple) */
	header.body[0].iov_len = mp_format(buf, buf_size, "{%u%u%u[%u%u]}",
					   IPROTO_SPACE_ID, 1,
					   IPROTO_TUPLE, 3, 4);
	xrow_decode_dml_fail(&header, &r, "upsert(space_id, tuple)",
			     "Missing mandatory field 'OPS' in request");

	/* upsert(space_id, ops, tuple) */
	header.body[0].iov_len = mp_format(buf, buf_size,
					   "{%u%u%u[%u%u]%u[%u%u]}",
					   IPROTO_SPACE_ID, 1,
					   IPROTO_OPS, 3, 4,
					   IPROTO_TUPLE, 5, 6);
	is(xrow_decode_dml(&header, &r, dml_request_key_map(header.type)), 0,
	   "upsert(space_id, ops, tuple): success");
	is(r.type, IPROTO_UPSERT, "upsert(space_id, ops, tuple): request type");
	is(r.space_id, 1, "upsert(space_id, ops, tuple): space ID");
	is(mp_decode_array(&r.ops), 2, "upsert(space_id, ops, tuple): ops len");
	is(mp_decode_uint(&r.ops), 3, "upsert(space_id, ops, tuple): ops[0]");
	is(mp_decode_uint(&r.ops), 4, "upsert(space_id, ops, tuple): ops[1]");
	is(r.ops, r.ops_end, "upsert(space_id, ops, tuple): ops size");
	is(mp_decode_array(&r.tuple), 2,
	   "upsert(space_id, ops, tuple): tuple len");
	is(mp_decode_uint(&r.tuple), 5,
	   "upsert(space_id, ops, tuple): tuple[0]");
	is(mp_decode_uint(&r.tuple), 6,
	   "upsert(space_id, ops, tuple): tuple[1]");
	is(r.tuple, r.tuple_end,
	   "upsert(space_id, ops, tuple): tuple size");

	/* insert_arrow() */
	header.type = IPROTO_INSERT_ARROW;
	header.body[0].iov_len = mp_format(buf, buf_size, "{}");
	xrow_decode_dml_fail(&header, &r, "insert_arrow()",
			     "Missing mandatory field 'SPACE_ID' in request");

	/* insert_arrow(space_id) */
	header.body[0].iov_len = mp_format(buf, buf_size, "{%u%u}",
					   IPROTO_SPACE_ID, 1);
	xrow_decode_dml_fail(&header, &r, "insert_arrow(space_id)",
			     "Missing mandatory field 'ARROW' in request");

	/* insert_arrow(space_id, arrow) */
	char mp_ext_arrow[6]; /* Prevents clang's extension warning. */
	fail_unless(lengthof(mp_ext_arrow) == mp_sizeof_ext(strlen("ext")));
	mp_encode_ext(mp_ext_arrow, MP_ARROW, "ext", strlen("ext"));
	header.body[0].iov_len = mp_format(buf, buf_size, "{%u%u%u%p}",
					   IPROTO_SPACE_ID, 1,
					   IPROTO_ARROW, mp_ext_arrow);
	is(xrow_decode_dml(&header, &r, dml_request_key_map(header.type)), 0,
	   "insert_arrow(space_id, arrow): success");
	is(r.type, IPROTO_INSERT_ARROW,
	   "insert_arrow(space_id, arrow): request type");
	is(r.space_id, 1, "insert_arrow(space_id, arrow): space ID");
	is(r.arrow_ipc_end - r.arrow_ipc, strlen("ext"),
	   "insert_arrow(space_id, arrow): Arrow IPC size");
	is(memcmp(r.arrow_ipc, "ext", strlen("ext")), 0,
	   "insert_arrow(space_id, arrow): Arrow IPC");

	/* delete_range() */
	header.type = IPROTO_DELETE_RANGE;
	header.body[0].iov_len = mp_format(buf, buf_size, "{}");
	xrow_decode_dml_fail(&header, &r, "delete_range()",
			     "Missing mandatory field 'SPACE_ID' in request");

	/* delete_range(space_id) */
	header.body[0].iov_len = mp_format(buf, buf_size, "{%u%u}",
					   IPROTO_SPACE_ID, 1);
	xrow_decode_dml_fail(&header, &r, "delete_range(space_id)",
			     "Missing mandatory field 'KEY' in request"),

	/* delete_range(space_id, begin_key) */
	header.body[0].iov_len = mp_format(buf, buf_size, "{%u%u%u[%u%u]}",
					   IPROTO_SPACE_ID, 1,
					   IPROTO_BEGIN_KEY, 3, 4);
	xrow_decode_dml_fail(&header, &r, "delete_range(space_id, begin_key)",
			     "Missing mandatory field 'END_KEY' in request");

	/* delete_range(space_id, begin_key, end_key) */
	header.body[0].iov_len = mp_format(buf, buf_size,
					   "{%u%u%u[%u%u]%u[%u%u]}",
					   IPROTO_SPACE_ID, 1,
					   IPROTO_BEGIN_KEY, 3, 4,
					   IPROTO_END_KEY, 5, 6);
	is(xrow_decode_dml(&header, &r, dml_request_key_map(header.type)), 0,
	   "delete_range(space_id, begin_key, end_key): success");
	is(r.space_id, 1,
	   "delete_range(space_id, begin_key, end_key): space ID");
	is(mp_decode_array(&r.begin_key), 2,
	   "delete_range(space_id, begin_key, end_key): begin_key len");
	is(mp_decode_uint(&r.begin_key), 3,
	   "delete_range(space_id, begin_key, end_key): begin_key[0]");
	is(mp_decode_uint(&r.begin_key), 4,
	   "delete_range(space_id, begin_key, end_key): begin_key[1]");
	is(r.begin_key, r.begin_key_end,
	   "delete_range(space_id, begin_key, end_key): begin_key size");
	is(mp_decode_array(&r.end_key), 2,
	   "delete_range(space_id, begin_key, end_key): end_key len");
	is(mp_decode_uint(&r.end_key), 5,
	   "delete_range(space_id, begin_key, end_key): end_key[0]");
	is(mp_decode_uint(&r.end_key), 6,
	   "delete_range(space_id, begin_key, end_key): end_key[1]");
	is(r.end_key, r.end_key_end,
	   "delete_range(space_id, begin_key, end_key): end_key size");

	check_plan();
	footer();
}

/**
 * Check that xrow_decode_* functions silently ignore unknown keys.
 */
static void
test_xrow_decode_unknown_key(void)
{
	header();
	plan(16);

	char buf[128];

	const char *p = buf;
	const char *end = buf + mp_format(buf, sizeof(buf), "{%u%s}",
					  0xDEAD, "foobar");
	struct xrow_header header;
	is(xrow_decode(&header, &p, end, /*end_is_exact=*/true), 0,
	   "xrow_decode");

	memset(&header, 0, sizeof(header));
	header.bodycnt = 1;
	header.body[0].iov_base = buf;
	header.body[0].iov_len = mp_format(buf, sizeof(buf), "{%u%s}",
					   0xDEAD, "foobar");

	struct request dml;
	header.type = IPROTO_SELECT;
	is(xrow_decode_dml(&header, &dml, /*key_map=*/0), 0, "xrow_decode_dml");

	struct begin_request begin;
	header.type = IPROTO_BEGIN;
	is(xrow_decode_begin(&header, &begin), 0, "xrow_decode_begin");

	struct id_request id;
	header.type = IPROTO_ID;
	is(xrow_decode_id(&header, &id), 0, "xrow_decode_id");

	struct register_request reg;
	header.type = IPROTO_REGISTER;
	is(xrow_decode_register(&header, &reg), 0, "xrow_decode_register");

	struct subscribe_request sub;
	header.type = IPROTO_SUBSCRIBE;
	is(xrow_decode_subscribe(&header, &sub), 0, "xrow_decode_subscribe");

	struct join_request join;
	header.type = IPROTO_JOIN;
	is(xrow_decode_join(&header, &join), 0, "xrow_decode_join");

	struct relay_heartbeat relay_heartbeat;
	header.type = IPROTO_OK;
	is(xrow_decode_relay_heartbeat(&header, &relay_heartbeat), 0,
	   "xrow_decode_relay_heartbeat");

	struct applier_heartbeat applier_heartbeat;
	header.type = IPROTO_OK;
	is(xrow_decode_applier_heartbeat(&header, &applier_heartbeat), 0,
	   "xrow_decode_applier_heartbeat");

	struct synchro_request synchro;
	header.type = IPROTO_RAFT_PROMOTE;
	is(xrow_decode_synchro(&header, &synchro), 0,
	   "xrow_decode_synchro");

	struct raft_request raft;
	header.type = IPROTO_RAFT;
	header.group_id = GROUP_LOCAL;
	is(xrow_decode_raft(&header, &raft), 0,
	   "xrow_decode_raft");

	struct ballot ballot;
	header.type = IPROTO_OK;
	header.body[0].iov_len = mp_format(buf, sizeof(buf), "{%u{%u%b}%u%s}",
					   IPROTO_BALLOT, IPROTO_BALLOT_IS_RO,
					   true, 0xDEAD, "foobar");
	is(xrow_decode_ballot(&header, &ballot), 0, "xrow_decode_ballot");

	struct call_request call;
	header.type = IPROTO_CALL;
	header.body[0].iov_len = mp_format(buf, sizeof(buf), "{%u%s%u%s}",
					   IPROTO_FUNCTION_NAME, "foo",
					   0xDEAD, "foobar");
	is(xrow_decode_call(&header, &call), 0, "xrow_decode_call");

	struct watch_request watch;
	header.type = IPROTO_WATCH;
	header.body[0].iov_len = mp_format(buf, sizeof(buf), "{%u%s%u%s}",
					   IPROTO_EVENT_KEY, "foo",
					   0xDEAD, "foobar");
	is(xrow_decode_watch(&header, &watch), 0, "xrow_decode_watch");

	struct sql_request sql;
	header.type = IPROTO_EXECUTE;
	header.body[0].iov_len = mp_format(buf, sizeof(buf), "{%u%s%u%s}",
					   IPROTO_SQL_TEXT, "SELECT 1",
					   0xDEAD, "foobar");
	is(xrow_decode_sql(&header, &sql), 0, "xrow_decode_sql");

	struct auth_request auth;
	header.type = IPROTO_AUTH;
	header.body[0].iov_len = mp_format(buf, sizeof(buf), "{%u%s%u[]%u%s}",
					   IPROTO_USER_NAME, "guest",
					   IPROTO_TUPLE, 0xDEAD, "foobar");
	is(xrow_decode_auth(&header, &auth), 0, "xrow_decode_auth");

	check_plan();
	footer();
}

static void
test_xrow_decode_synchro_types(void)
{
	header();
	plan(1);

	char buf[128];

	const char *p = buf;
	const char *end = buf + mp_format(buf, sizeof(buf), "{%u%s}",
					  IPROTO_INSTANCE_NAME, "somename");
	struct xrow_header header;
	memset(&header, 0, sizeof(header));
	header.bodycnt = 1;
	header.body[0].iov_base = buf;
	header.body[0].iov_len = mp_format(buf, sizeof(buf), "{%u%s}",
					   IPROTO_INSTANCE_NAME, "somename");
	struct synchro_request synchro;
	is(xrow_decode_synchro(&header, &synchro), 0,
	   "xrow_decode_synchro correctly handles key types");

	check_plan();
	footer();
}

static void
test_xrow_decode_error_1(void)
{
	header();
	plan(1);

	uint8_t data[] = {
		0x81, /* MP_MAP of 1 element */
		0x52, /* IPROTO_ERROR: */
		0x00  /* MP_INT instead of MP_MAP */
	};

	struct iovec body;
	body.iov_base = (void *)data;
	body.iov_len = sizeof(data);

	struct xrow_header row;
	row.type = IPROTO_TYPE_ERROR | 111;
	row.body[0] = body;
	row.bodycnt = 1;

	xrow_decode_error(&row);

	struct error *e = diag_last_error(diag_get());
	is(e->code, 111, "xrow_decode_error");
	diag_destroy(diag_get());

	check_plan();
	footer();
}

static void
test_xrow_decode_error_2(void)
{
	header();
	plan(1);

	uint8_t data[] = {
		0x81, /* MP_MAP of 1 element */
		0x52, /* IPROTO_ERROR: */
		0x81, /* MP_MAP of 1 element */
		0xa1  /* MP_STR instead of MP_UINT */
	};

	struct iovec body;
	body.iov_base = (void *)data;
	body.iov_len = sizeof(data);

	struct xrow_header row;
	row.type = IPROTO_TYPE_ERROR | 222;
	row.body[0] = body;
	row.bodycnt = 1;

	xrow_decode_error(&row);

	struct error *e = diag_last_error(diag_get());
	is(e->code, 222, "xrow_decode_error");
	diag_destroy(diag_get());

	check_plan();
	footer();
}

static void
test_xrow_decode_error_3(void)
{
	header();
	plan(1);

	uint8_t data[] = {
		0x81, /* MP_MAP of 1 element */
		0x52, /* IPROTO_ERROR: */
		0x81, /* MP_MAP of 1 element */
		0x00, /* MP_ERROR_STACK: */
		0x00  /* MP_INT instead of MP_ARRAY */
	};

	struct iovec body;
	body.iov_base = (void *)data;
	body.iov_len = sizeof(data);

	struct xrow_header row;
	row.type = IPROTO_TYPE_ERROR | 333;
	row.body[0] = body;
	row.bodycnt = 1;

	xrow_decode_error(&row);

	struct error *e = diag_last_error(diag_get());
	is(e->code, 333, "xrow_decode_error");
	diag_destroy(diag_get());

	check_plan();
	footer();
}

static void
test_xrow_decode_error_4(void)
{
	header();
	plan(1);

	uint8_t data[] = {
		0x81, /* MP_MAP of 1 element */
		0x52, /* IPROTO_ERROR: */
		0x81, /* MP_MAP of 1 element */
		0x00, /* MP_ERROR_STACK: */
		0x93, /* MP_ARRAY of 3 elements */
		0x83, /* MP_MAP of 3 elements */
		0x00, 0xa1, 0x00, /* MP_ERROR_TYPE: "" */
		0x01, 0xa1, 0x00, /* MP_ERROR_FILE: "" */
		0x03, 0xa1, 0x00, /* MP_ERROR_MESSAGE: "" */
		0x83, /* MP_MAP of 3 elements */
		0x00, 0xa1, 0x00, /* MP_ERROR_TYPE: "" */
		0x01, 0xa1, 0x00, /* MP_ERROR_FILE: "" */
		0x03, 0xa1, 0x00, /* MP_ERROR_MESSAGE: "" */
		0x00 /* MP_INT instead of MP_MAP */
	};

	struct iovec body;
	body.iov_base = (void *)data;
	body.iov_len = sizeof(data);

	struct xrow_header row;
	row.type = IPROTO_TYPE_ERROR | 444;
	row.body[0] = body;
	row.bodycnt = 1;

	xrow_decode_error(&row);

	struct error *e = diag_last_error(diag_get());
	is(e->code, 444, "xrow_decode_error");
	diag_destroy(diag_get());

	check_plan();
	footer();
}

static void
test_xrow_decode_error_gh_9098(void)
{
	header();
	plan(1);

	uint8_t data[] = {
		0x81, /* MP_MAP of 1 element */
		0x52, /* IPROTO_ERROR: */
		0x81, /* MP_MAP of 1 element */
		0x00, /* MP_ERROR_STACK: */
		0x91, /* MP_ARRAY of 1 element */
		0x84, /* MP_MAP of 4 elements */
		0x00, 0xa1, 0x00, /* MP_ERROR_TYPE: "" */
		0x01, 0xa1, 0x00, /* MP_ERROR_FILE: "" */
		0x03, 0xa1, 0x00, /* MP_ERROR_MESSAGE: "" */
		0x06, /* MP_ERROR_FIELDS: */
		0x81, /* MP_MAP of 1 element */
		0xa1, 0x78, 0x2a /* "x": 42 */
	};

	struct iovec body;
	body.iov_base = (void *)data;
	body.iov_len = sizeof(data);

	struct xrow_header row;
	row.type = IPROTO_TYPE_ERROR;
	row.body[0] = body;
	row.bodycnt = 1;

	xrow_decode_error(&row);

	uint64_t payload_value;
	struct error *e = diag_last_error(diag_get());
	error_get_uint(e, "x", &payload_value);
	is(payload_value, 42, "decoded payload");
	diag_destroy(diag_get());

	check_plan();
	footer();
}

static void
test_xrow_decode_error_gh_9136(void)
{
	header();
	plan(1);

	uint8_t data[] = {
		0x81, /* MP_MAP of 1 element */
		0x52, /* IPROTO_ERROR: */
		0x82, /* MP_MAP of 2 elements */
		0x00, /* MP_ERROR_STACK: */
		0x91, /* MP_ARRAY of 1 element */
		0x83, /* MP_MAP of 3 elements */
		0x00, 0xa1, 0x00, /* MP_ERROR_TYPE: "" */
		0x01, 0xa1, 0x00, /* MP_ERROR_FILE: "" */
		0x03, 0xa1, 0x00, /* MP_ERROR_MESSAGE: "" */
		0x00, /* MP_ERROR_STACK: */
		0x91, /* MP_ARRAY of 1 element */
		0x83, /* MP_MAP of 3 elements */
		0x00, 0xa1, 0x00, /* MP_ERROR_TYPE: "" */
		0x01, 0xa1, 0x00, /* MP_ERROR_FILE: "" */
		0x03, 0xa1, 0x00, /* MP_ERROR_MESSAGE: "" */
	};

	struct iovec body;
	body.iov_base = (void *)data;
	body.iov_len = sizeof(data);

	struct xrow_header row;
	row.type = IPROTO_TYPE_ERROR | 9136;
	row.body[0] = body;
	row.bodycnt = 1;

	xrow_decode_error(&row);

	struct error *e = diag_last_error(diag_get());
	is(e->code, 9136, "xrow_decode_error");
	diag_destroy(diag_get());

	check_plan();
	footer();
}

int
main(void)
{
	memory_init();
	fiber_init(fiber_c_invoke);
	header();
	plan(15);

	random_init();

	test_iproto_constants();
	test_greeting();
	test_xrow_encode_decode();
	test_request_str();
	test_xrow_fields();
	test_xrow_encode_dml();
	test_xrow_decode_dml_keys();
	test_xrow_decode_dml_requests();
	test_xrow_decode_unknown_key();
	test_xrow_decode_error_1();
	test_xrow_decode_error_2();
	test_xrow_decode_error_3();
	test_xrow_decode_error_4();
	test_xrow_decode_error_gh_9098();
	test_xrow_decode_error_gh_9136();
	test_xrow_decode_synchro_types();

	random_free();
	fiber_free();
	memory_free();

	int rc = check_plan();
	footer();
	return rc;
}
