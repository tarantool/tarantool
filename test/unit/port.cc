/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2026, Tarantool AUTHORS, please see AUTHORS file.
 */
extern "C" {
#include "lua_test_utils.h"
}

#include "box/lua/call.h"
#include "box/lua/tuple.h"
#include "box/port.h"
#include "box/session.h"
#include "box/tuple.h"
#include "box/user.h"
#include "fiber.h"
#include "lua/msgpack.h"
#include "lua/serializer.h"
#include "lua/utils.h"
#include "memory.h"
#include "msgpuck.h"
#include "small/obuf.h"

#define UNIT_TAP_COMPATIBLE 1
#include "unit.h"

static void
test_port_lua_msgpack_errors(void)
{
	plan(9);
	header();

	struct lua_State *L = lua_newthread(tarantool_L);
	const char *expr =
		"return setmetatable({42}, {__serialize = function() "
		"error('serialize failure', 0) end})";
	fail_unless(luaT_dostring(L, expr) == 0);
	int top = lua_gettop(L);
	struct port port;
	port_lua_create(&port, L);
	struct obuf obuf;
	size_t region_svp = region_used(&fiber()->gc);
	obuf_create(&obuf, &cord()->slabc, 512);

	uint32_t size;
	is(port_get_msgpack(&port, &size), NULL,
	   "get_msgpack reports serialization failure");
	is(lua_gettop(L), top, "get_msgpack preserves stack height");
	is(strcmp(diag_last_error(diag_get())->errmsg, "serialize failure"), 0,
	   "get_msgpack preserves the diag");
	is(port_dump_msgpack_16(&port, &obuf), -1,
	   "dump_msgpack_16 reports serialization failure");
	is(lua_gettop(L), top, "dump_msgpack_16 preserves stack height");
	is(strcmp(diag_last_error(diag_get())->errmsg, "serialize failure"), 0,
	   "dump_msgpack_16 preserves the diag");
	obuf_reset(&obuf);

	lua_pushnil(L);
	lua_setmetatable(L, top);
	char expected[16];
	char *end = mp_encode_array(expected, 1);
	end = mp_encode_array(end, 1);
	end = mp_encode_uint(end, 42);
	uint32_t expected_size = end - expected;
	const char *mp = port_get_msgpack(&port, &size);
	ok(mp != NULL && size == expected_size &&
	   memcmp(mp, expected, size) == 0,
	   "get_msgpack retry encodes only the original value");
	is(port_dump_msgpack_16(&port, &obuf), 1,
	   "dump_msgpack_16 retry returns one tuple");
	size = obuf_size(&obuf);
	char *data = (char *)region_alloc(&fiber()->gc, size);
	fail_unless(data != NULL);
	char *pos = data;
	for (int i = 0; i < obuf_iovcnt(&obuf); i++) {
		memcpy(pos, obuf.iov[i].iov_base, obuf.iov[i].iov_len);
		pos += obuf.iov[i].iov_len;
	}
	mp = data;
	const char *expected_tuple = expected;
	mp_decode_array(&expected_tuple);
	ok(size == end - expected_tuple &&
	   memcmp(mp, expected_tuple, size) == 0,
	   "dump_msgpack_16 retry encodes only the original value");

	obuf_destroy(&obuf);
	region_truncate(&fiber()->gc, region_svp);
	port_destroy(&port);
	lua_pop(tarantool_L, 1);

	footer();
	check_plan();
}

static void
test_port_lua(void)
{
	plan(1);
	header();

	test_port_lua_msgpack_errors();

	footer();
	check_plan();
}

static int test_result;

static int
main_f(va_list ap)
{
	(void)ap;
	plan(1);
	header();
	test_port_lua();
	footer();
	test_result = check_plan();
	return 0;
}

int
main(void)
{
	memory_init();
	fiber_init(fiber_c_invoke);
	tuple_init(NULL);
	port_init();
	user_cache_init();
	session_init();
	struct lua_State *L = luaT_newteststate();
	tarantool_L = L;
	tarantool_lua_error_init(L);
	tarantool_lua_utils_init(L);
	tarantool_lua_serializer_init(L);
	luaopen_msgpack(L);
	lua_pop(L, 1);
	box_lua_tuple_init(L);
	box_lua_call_init(L);
	lua_settop(L, 0);

	/* Session cleanup runs when this fiber stops. */
	struct fiber *main = fiber_new_system("main", main_f);
	fail_unless(main != NULL);
	fiber_wakeup(main);
	ev_run(loop(), 0);

	lua_close(L);
	tarantool_L = NULL;
	session_free();
	user_cache_free();
	port_free();
	tuple_free();
	fiber_free();
	memory_free();
	return test_result;
}
