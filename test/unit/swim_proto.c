/*
 * Copyright 2010-2019, Tarantool AUTHORS, please see AUTHORS file.
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

#include "memory.h"
#include "fiber.h"
#include "uuid/tt_uuid.h"
#include "version.h"
#include "msgpuck.h"
#include "unit.h"
#include "swim/swim_proto.h"
#include "swim_test_utils.h"
#include <fcntl.h>

static char buffer[1024 * 1024];

static void
swim_test_member_def(void)
{
	header();
	plan(12);

	struct swim_member_def mdef;
	const char *pos = buffer;
	char *last_valid, *end = mp_encode_array(buffer, 10);
	is(swim_member_def_decode(&mdef, &pos, end, "test"), -1,
	   "not map header");

	pos = buffer;
	end = mp_encode_map(buffer, 4);
	last_valid = end;
	end = mp_encode_str(end, "str", 3);
	is(swim_member_def_decode(&mdef, &pos, end, "test"), -1,
	   "not uint member key");

	pos = buffer;
	end = mp_encode_uint(last_valid, 10000);
	is(swim_member_def_decode(&mdef, &pos, end, "test"), -1,
	   "too big member key");

	pos = buffer;
	end = mp_encode_uint(last_valid, SWIM_MEMBER_STATUS);
	end = mp_encode_str(end, "str", 3);
	is(swim_member_def_decode(&mdef, &pos, end, "test"), -1,
	   "STATUS is not uint");

	pos = buffer;
	end = mp_encode_uint(last_valid, SWIM_MEMBER_STATUS);
	end = mp_encode_uint(end, 10000);
	is(swim_member_def_decode(&mdef, &pos, end, "test"), -1,
	   "invalid STATUS");

	pos = buffer;
	end = mp_encode_uint(last_valid, SWIM_MEMBER_ADDRESS);
	last_valid = end;
	end = mp_encode_uint(end, (uint64_t)UINT32_MAX + 100);
	is(swim_member_def_decode(&mdef, &pos, end, "test"), -1,
	   "invalid address");

	pos = buffer;
	struct in_addr ipaddr;
	fail_if(inet_aton("127.0.0.1", &ipaddr) == 0);
	end = mp_encode_uint(last_valid, ipaddr.s_addr);
	end = mp_encode_uint(end, SWIM_MEMBER_PORT);
	last_valid = end;
	end = mp_encode_uint(end, 100000);
	is(swim_member_def_decode(&mdef, &pos, end, "test"), -1, "bad port");

	pos = buffer;
	end = mp_encode_uint(last_valid, 1);
	last_valid = end;
	is(swim_member_def_decode(&mdef, &pos, end, "test"), -1,
	   "unexpected buffer end");

	pos = buffer;
	end = mp_encode_uint(last_valid, SWIM_MEMBER_UUID);
	last_valid = end;
	is(swim_member_def_decode(&mdef, &pos, end, "test"), -1,
	   "unexpected buffer end");

	pos = buffer;
	end = mp_encode_bin(last_valid, (const char *) &uuid_nil,
			    sizeof(uuid_nil));
	end = mp_encode_uint(end, SWIM_MEMBER_STATUS);
	end = mp_encode_uint(end, 0);
	is(swim_member_def_decode(&mdef, &pos, end, "test"), -1,
	   "uuid is nil/undefined");

	pos = buffer;
	struct tt_uuid uuid = uuid_nil;
	uuid.time_low = 1;
	end = mp_encode_bin(last_valid, (const char *) &uuid, sizeof(uuid));
	last_valid = end;
	end = mp_encode_uint(end, SWIM_MEMBER_PORT);
	end = mp_encode_uint(end, 0);
	is(swim_member_def_decode(&mdef, &pos, end, "test"), -1,
	   "port is 0/undefined");

	pos = buffer;
	end = mp_encode_uint(last_valid, SWIM_MEMBER_STATUS);
	end = mp_encode_uint(end, 0);
	is(swim_member_def_decode(&mdef, &pos, end, "test"), 0,
	   "normal member def");

	check_plan();
	footer();
}

static void
swim_test_meta(void)
{
	header();
	plan(8);

	struct swim_meta_def mdef;
	const char *pos = buffer;
	char *last_valid, *end = mp_encode_array(buffer, 10);
	is(swim_meta_def_decode(&mdef, &pos, end), -1, "not map header");

	pos = buffer;
	end = mp_encode_map(buffer, 3);
	last_valid = end;
	end = mp_encode_str(end, "str", 3);
	is(swim_meta_def_decode(&mdef, &pos, end), -1, "not uint meta key");

	pos = buffer;
	end = mp_encode_uint(last_valid, 10000);
	is(swim_meta_def_decode(&mdef, &pos, end), -1, "unknown meta key");

	pos = buffer;
	end = mp_encode_uint(last_valid, SWIM_META_TARANTOOL_VERSION);
	last_valid = end;
	is(swim_meta_def_decode(&mdef, &pos, end), -1, "unexpected end");

	pos = buffer;
	end = mp_encode_uint(last_valid, (uint64_t)UINT32_MAX + 100);
	is(swim_meta_def_decode(&mdef, &pos, end), -1, "invalid version");

	pos = buffer;
	end = mp_encode_uint(last_valid, tarantool_version_id());
	end = mp_encode_uint(end, SWIM_META_SRC_ADDRESS);
	struct in_addr ipaddr;
	fail_if(inet_aton("127.0.0.1", &ipaddr) == 0);
	end = mp_encode_uint(end, ipaddr.s_addr);
	last_valid = end;
	end = mp_encode_uint(end, SWIM_META_SRC_PORT);
	end = mp_encode_uint(end, 0);
	is(swim_meta_def_decode(&mdef, &pos, end), -1, "port is 0/undefined");

	pos = buffer;
	end = mp_encode_uint(last_valid, SWIM_META_TARANTOOL_VERSION);
	end = mp_encode_uint(end, 0);
	is(swim_meta_def_decode(&mdef, &pos, end), -1,
	   "version is 0/undefined");

	pos = buffer;
	end = mp_encode_uint(last_valid, SWIM_META_SRC_PORT);
	end = mp_encode_uint(end, 1);
	is(swim_meta_def_decode(&mdef, &pos, end), 0, "normal meta");

	check_plan();
	footer();
}

static void
swim_test_route(void)
{
	header();
	plan(5);

	char buffer[1024];
	struct swim_meta_def mdef;
	struct swim_meta_header_bin header;
	struct sockaddr_in addr;
	addr.sin_port = htons(1234);
	fail_if(inet_aton("127.0.0.1", &addr.sin_addr) == 0);

	swim_meta_header_bin_create(&header, &addr, true);
	memcpy(buffer, &header, sizeof(header));
	char *last_valid = buffer + sizeof(header);
	char *end = last_valid;
	const char *pos = buffer;

	is(swim_meta_def_decode(&mdef, &pos, end), -1,
	   "route was expected, but map is too short");

	end = mp_encode_uint(end, SWIM_META_ROUTING);
	pos = buffer;
	is(swim_meta_def_decode(&mdef, &pos, end), -1, "no route map");

	end = mp_encode_map(end, 0);
	pos = buffer;
	is(swim_meta_def_decode(&mdef, &pos, end), -1, "empty route map");

	struct swim_route_bin route;
	struct sockaddr_in src, dst;
	memset(&src, 0, sizeof(src));
	memset(&dst, 0, sizeof(dst));
	swim_route_bin_create(&route, &src, &dst);
	memcpy(last_valid, &route, sizeof(route));
	end = last_valid + sizeof(route);
	pos = buffer;
	is(swim_meta_def_decode(&mdef, &pos, end), -1, "zero addresses");

	src.sin_port = 1;
	src.sin_addr = addr.sin_addr;
	dst.sin_port = 1;
	dst.sin_addr = addr.sin_addr;
	swim_route_bin_create(&route, &src, &dst);
	memcpy(last_valid, &route, sizeof(route));
	pos = buffer;
	is(swim_meta_def_decode(&mdef, &pos, end), 0, "normal route");

	check_plan();
	footer();
}

int
main()
{
	header();
	plan(3);
	memory_init();
	fiber_init(fiber_c_invoke);
	int fd = open("log.txt", O_TRUNC);
	if (fd != -1)
		close(fd);
	say_logger_init("log.txt", 6, 1, "plain", 0);

	swim_test_member_def();
	swim_test_meta();
	swim_test_route();

	say_logger_free();
	fiber_free();
	memory_free();
	int rc = check_plan();
	footer();
	return rc;
}