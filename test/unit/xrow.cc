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
#include "unit.h"
} /* extern "C" */
#include "trivia/util.h"
#include "box/xrow.h"
#include "box/iproto_constants.h"
#include "tt_uuid.h"
#include "version.h"
#include "random.h"

int
test_greeting()
{
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

	return check_plan();
}

int
main(void)
{
	plan(1);

	random_init();

	test_greeting();

	random_free();

	return check_plan();
}
