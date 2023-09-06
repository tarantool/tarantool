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
test_xrow_header_encode_decode()
{
	header();
	/* Test all possible 3-bit combinations. */
	const int bit_comb_count = 1 << 3;
	plan(1 + bit_comb_count * 13);
	struct xrow_header header;
	char buffer[2048];
	char *pos = mp_encode_uint(buffer, 300);
	is(xrow_header_decode(&header, (const char **) &pos,
			      buffer + 100, true), -1, "bad msgpack end");

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
		xrow_header_encode(&header, sync, /*fixheader_len=*/200,
				   vec, &iovcnt);
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
		const char *begin = (const char *)vec[0].iov_base;
		begin += fixheader_len;
		const char *end = (const char *)vec[0].iov_base;
		end += vec[0].iov_len;
		is(xrow_header_decode(&decoded_header, &begin, end, true), 0,
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

	is(strcmp("{type: 'SELECT', replica_id: 5, lsn: 100, "
		  "space_id: 512, index_id: 1, "
		  "key: [200], tuple: [300], ops: [400], "
		  "fetch_position: true, after_position: \"position\", "
		  "after_tuple: [300]}",
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
	plan(20);

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

	int iovcnt;
	struct iovec iov[1];
	xrow_encode_dml(&r, &fiber()->gc, iov, &iovcnt);
	is(iovcnt, 1, "xrow_encode_dml rc");
	const char *data = (const char *)iov[0].iov_base;
	int map_size = mp_decode_map(&data);
	is(map_size, 9, "decoded request map");

	is(mp_decode_uint(&data), IPROTO_SPACE_ID, "decoded space id key");
	is(mp_decode_uint(&data), r.space_id, "decoded space id");

	is(mp_decode_uint(&data), IPROTO_INDEX_ID, "decoded index id key");
	is(mp_decode_uint(&data), r.index_id, "decoded index id");

	is(mp_decode_uint(&data), IPROTO_INDEX_BASE, "decoded index base key");
	is(mp_decode_uint(&data), (uint64_t)r.index_base, "decoded index base");

	is(mp_decode_uint(&data), IPROTO_KEY, "decoded iproto key");
	is(memcmp(data, r.key, strlen(r.key)), 0, "decoded key");
	data += strlen(r.key);

	is(mp_decode_uint(&data), IPROTO_OPS, "decoded ops key");
	is(memcmp(data, r.ops, strlen(r.ops)), 0, "decoded ops");
	data += strlen(r.ops);

	is(mp_decode_uint(&data), IPROTO_TUPLE_META, "decoded meta key");
	is(memcmp(data, r.tuple_meta, strlen(r.tuple_meta)), 0,
	   "decoded meta");
	data += strlen(r.tuple_meta);

	is(mp_decode_uint(&data), IPROTO_TUPLE, "decoded tuple key");
	is(memcmp(data, r.tuple, strlen(r.tuple)), 0, "decoded tuple");
	data += strlen(r.tuple);

	is(mp_decode_uint(&data), IPROTO_OLD_TUPLE, "decoded old tuple key");
	is(memcmp(data, r.old_tuple, strlen(r.old_tuple)), 0,
	   "decoded old tuple");
	data += strlen(r.old_tuple);

	is(mp_decode_uint(&data), IPROTO_NEW_TUPLE, "decoded new tuple key");
	is(memcmp(data, r.new_tuple, strlen(r.new_tuple)), 0,
	   "decoded new tuple");

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
	is(xrow_header_decode(&header, &p, end, /*end_is_exact=*/true), 0,
	   "xrow_header_decode");

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
	is(xrow_decode_synchro(&header, &synchro), 0, "xrow_decode_synchro");

	struct raft_request raft;
	header.type = IPROTO_RAFT;
	header.group_id = GROUP_LOCAL;
	is(xrow_decode_raft(&header, &raft, /*vclock=*/NULL), 0,
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

int
main(void)
{
	memory_init();
	fiber_init(fiber_c_invoke);
	header();
	plan(11);

	random_init();

	test_iproto_constants();
	test_greeting();
	test_xrow_header_encode_decode();
	test_request_str();
	test_xrow_fields();
	test_xrow_encode_dml();
	test_xrow_decode_unknown_key();
	test_xrow_decode_error_1();
	test_xrow_decode_error_2();
	test_xrow_decode_error_3();
	test_xrow_decode_error_4();
	test_xrow_decode_error_gh_9098();

	random_free();
	fiber_free();
	memory_free();

	int rc = check_plan();
	footer();
	return rc;
}
