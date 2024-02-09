#include "lua_test_utils.h"

#include "box/lua/call.h"
#include "box/lua/tuple.h"
#include "box/port.h"
#include "box/session.h"
#include "box/tuple.h"
#include "box/user.h"
#include "core/event.h"
#include "core/port.h"
#include "lua/init.h"
#include "lua/minifio.h"
#include "lua/msgpack.h"
#include "lua/utils.h"
#include "memory.h"
#include "msgpuck.h"
#include "small/obuf.h"
#include "trivia/util.h"

#define UNIT_TAP_COMPATIBLE 1
#include "unit.h"

/**
 * STRUCTURE OF THE TEST
 *
 * The test consists of several helper sections (e.g. Lua helpers,
 * MsgPack helpers) and several test sections. Each test section
 * focuses on a particular port implementation.
 *
 * A test section has a function that creates port and fills it with fixed
 * data. Then, every test case, dedicated to a particular port method or group
 * of methods, checks if the empty port works correctly, then fills the
 * port with the filler function and manually creates an expected
 * MsgPack packet/Lua stack/etc and passes the port and the expected
 * object to checker.
 *
 * Method port_dump_plain is not tested because it requires console.lua file,
 * so it's easier to test it from Lua then loading the Lua file in unit test.
 */

/**
 * This helper is placed here to be used in Lua helpers.
 * See actual description near the definition.
 */
static void
test_check_mp_equal(const char *got, uint32_t got_size,
		    const char *expected, uint32_t expected_size,
		    bool no_header);

/**
 * Utils to check port_dump_lua method.
 *
 * Checker for port_dump_lua with mp_object mode is in msgpack
 * helpers section because it is actually dump_msgpack method that
 * pushes its result to Lua.
 */

/**
 * Defines a global Lua function table_eq for table comparison.
 */
static void
lua_table_equal_init(struct lua_State *L)
{
	const char *text = "function table_eq(a, b) "
		"if type(a) ~= 'table' or type(b) ~= 'table' then "
		"    return a == b "
		"end "
		"for k, v in pairs(a) do "
		"    if not table_eq(v, b[k]) then "
		"        return false "
		"    end "
		"end "
		"for k, _ in pairs(b) do "
		"    if type(a[k]) == 'nil' then "
		"        return false "
		"    end "
		"end "
		"return true "
		"end "
		"return table_eq";
	int rc = luaT_dostring(L, text);
	fail_if(rc != 0);
	lua_setglobal(L, "table_eq");
}

/**
 * Returns true if two tables on top of passed Lua stack are equal,
 * false is returned otherwise. Compared tables are popped.
 */
static bool
lua_table_equal(struct lua_State *L)
{
	lua_getglobal(L, "table_eq");
	lua_insert(L, -3);
	int rc = luaT_call(L, 2, 1);
	fail_if(rc != 0);
	bool res = lua_toboolean(L, -1);
	lua_pop(L, 1);
	return res;
}

/**
 * Unpacks Lua table which is on top of passed Lua stack.
 * The table is popped, its contents are pushed to the Lua stack.
 */
static void
lua_table_unpack(struct lua_State *L)
{
	lua_getglobal(L, "unpack");
	lua_insert(L, -2);
	int rc = luaT_call(L, 1, LUA_MULTRET);
	fail_if(rc != 0);
}

/**
 * A handy helper to easily push Lua values to the Lua stack.
 * Argument values is a sequence of values written in Lua syntax.
 */
static void
lua_push_values(struct lua_State *L, const char *values)
{
	const char *text = tt_sprintf("return %s", values);
	int rc = luaT_dostring(L, text);
	fail_if(rc != 0);
}

/**
 * Checks if resulting Lua state is equal to expected one.
 *
 * Tuples are compared by pointers, so if got_L contains a tuple,
 * expected_L must contain the same one.
 */
static void
test_check_lua_state(struct lua_State *got_L, struct lua_State *expected_L)
{
	struct lua_State *L = expected_L;
	int top = lua_gettop(got_L);
	is(top, lua_gettop(expected_L), "Lua argument number must match");
	lua_xmove(got_L, expected_L, top);
	for (int i = 1; i <= top; i++) {
		struct tuple *tuple = luaT_istuple(L, i);
		size_t mp_size = 0;
		const char *mp = luamp_get(L, i, &mp_size);
		if (tuple != NULL) {
			struct tuple *other = luaT_istuple(L, i + top);
			fail_if(other == NULL);
			is(tuple, other, "The same tuple is expected");
		} else if (mp != NULL) {
			size_t other_size = 0;
			const char *other = luamp_get(L, i + top, &other_size);
			test_check_mp_equal(mp, mp_size, other, other_size,
					    /*no_header=*/false);
		} else if (lua_istable(L, i)) {
			lua_pushvalue(L, i);
			lua_pushvalue(L, i + top);
			ok(lua_table_equal(L), "Tables must be equal");
		} else {
			ok(lua_equal(L, i, i + top), "Elements must be equal");
		}
	}
	lua_settop(L, top);
}

/**
 * Checks if port_dump_lua works correctly with flat mode.
 */
static void
test_check_port_dump_lua_flat(struct port *port, struct lua_State *expected_L)
{
	struct lua_State *L = lua_newthread(tarantool_L);

	port_dump_lua(port, L, PORT_DUMP_LUA_MODE_FLAT);
	int top = lua_gettop(L);

	test_check_lua_state(L, expected_L);
}

/**
 * Checks if port_dump_lua works correctly with table mode.
 *
 * Argument expected_L must contain not expected table but it contents,
 * the table dumped form port will be unpacked and only then resulting
 * Lua state will be compared to expected one.
 */
static void
test_check_port_dump_lua_table(struct port *port, struct lua_State *expected_L)
{
	struct lua_State *L = lua_newthread(tarantool_L);

	port_dump_lua(port, L, PORT_DUMP_LUA_MODE_TABLE);
	is(lua_gettop(L), 1, "Only one table should be dumped");

	/* Unpack the table and check if the contents are the same. */
	lua_table_unpack(L);
	test_check_lua_state(L, expected_L);
}

/**
 * Utils to check MsgPack methods (get, dump).
 *
 * port_dump_msgpack_16 is not tested because it has some bugs
 * (at least, port_c_dump_msgpack_16 does not wrap each value into
 * a tuple, which is against protocol and can break old connector),
 * so we decided not to test this method - probably, we will get rid
 * of it in the future because it is needed to support very old version
 * of IPROTO.
 */

/**
 * Checks that two MsgPack packets are bytewise equal.
 *
 * If no_header is true, MP_ARRAY header of expected packet is
 * not included in the comparison.
 */
static void
test_check_mp_equal(const char *got, uint32_t got_size,
		    const char *expected, uint32_t expected_size,
		    bool no_header)
{
	if (no_header) {
		const char *old_expected = expected;
		mp_decode_array(&expected);
		expected_size -= expected - old_expected;
	}
	is(got_size, expected_size,
	   "Packet lengths should match: got %u, expected %u",
	   got_size, expected_size);
	is(memcmp(got, expected, got_size), 0, "Packets should match");
}

/**
 * A helper that dumps obuf contents to region to process the data easier.
 * Always allocates memory, even if obuf is empty.
 */
static void *
test_obuf_to_region(struct obuf *obuf, struct region *region, uint32_t *size)
{
	size_t alloc_size = obuf_size(obuf);
	/* Allocate memory even if obuf is empty. */
	if (alloc_size == 0)
		alloc_size = 1;
	char *buf = (char *)xregion_alloc(region, alloc_size);
	char *buf_begin = buf;
	for (struct iovec *iov = obuf->iov; iov->iov_len; ++iov) {
		memcpy(buf, iov->iov_base, iov->iov_len);
		buf += iov->iov_len;
	}
	*size = buf - buf_begin;
	return buf_begin;
}

/**
 * Checks port_get_msgpack method.
 * Expected MsgPack packet must be an MP_ARRAY.
 */
static void
test_check_port_get_msgpack(struct port *port, const char *expected_mp,
			    uint32_t expected_mp_size)
{
	uint32_t region_svp = region_used(&fiber()->gc);
	uint32_t got_mp_size = 0;
	const char *got_mp = port_get_msgpack(port, &got_mp_size);
	fail_if(got_mp == NULL);
	test_check_mp_equal(got_mp, got_mp_size, expected_mp, expected_mp_size,
			    /*no_header=*/false);
	region_truncate(&fiber()->gc, region_svp);
}

/**
 * Checks port_dump_msgpack method. Argument no_header is required
 * because some ports dump MsgPack with MP_ARRAY header and some
 * dump without it.
 * Expected MsgPack packet must be an MP_ARRAY.
 */
static void
test_check_port_dump_msgpack(struct port *port, const char *expected_mp,
			     uint32_t expected_mp_size, bool no_header)
{
	uint32_t region_svp = region_used(&fiber()->gc);
	struct obuf obuf;
	obuf_create(&obuf, &cord()->slabc, 512);
	int rc = port_dump_msgpack(port, &obuf);
	fail_if(rc < 0);
	uint32_t got_mp_size = 0;
	const char *got_mp =
		(const char *)test_obuf_to_region(&obuf, &fiber()->gc,
						  &got_mp_size);
	test_check_mp_equal(got_mp, got_mp_size, expected_mp, expected_mp_size,
			    no_header);
	obuf_destroy(&obuf);
	region_truncate(&fiber()->gc, region_svp);
}

/**
 * A wrapper over dump_msgpack checker without bool parameter.
 * Is needed to pass the checker as a pointer to function.
 */
static void
test_check_port_dump_msgpack_no_header(struct port *port,
				       const char *expected_mp,
				       uint32_t expected_mp_size)
{
	test_check_port_dump_msgpack(port, expected_mp, expected_mp_size,
				     /*no_header=*/true);
}

/**
 * Checks port_dump_lua method with mp object mode. Belongs here because it is
 * actually dump_msgpack method, but the result is pushed onto Lua stack.
 */
static void
test_check_port_dump_lua_mp_object(struct port *port, const char *expected_mp,
				   uint32_t expected_mp_size)
{
	struct lua_State *expected_L = lua_newthread(tarantool_L);
	luamp_push(expected_L, expected_mp, expected_mp + expected_mp_size);
	struct lua_State *got_L = lua_newthread(tarantool_L);
	port_dump_lua(port, got_L, PORT_DUMP_LUA_MODE_MP_OBJECT);
	test_check_lua_state(got_L, expected_L);
}

/**
 * Pointer to port_{get,dump}_msgpack checker.
 */
typedef void
(*test_check_msgpack_method)(struct port *port, const char *expected_mp,
			     uint32_t expected_mp_size);

/** Tests for port_c. */

struct port_c_contents {
	struct tuple *tuple;
	const char *mp_arr;
	const char *mp_arr_end;
	const char *mp_map;
	const char *mp_map_end;
};

static struct port_c_contents
test_port_c_create(struct port *port)
{
	/* Prepare to fill - create all required objects. */
	const char *str = "abc";

	static char mp_arr_begin[32];
	char *mp_arr = mp_arr_begin;
	mp_arr = mp_encode_array(mp_arr, 4);
	mp_arr = mp_encode_str0(mp_arr, str);
	mp_arr = mp_encode_uint(mp_arr, 10);
	mp_arr = mp_encode_bool(mp_arr, true);
	mp_arr = mp_encode_double(mp_arr, 42.12);
	fail_if(mp_arr > mp_arr_begin + lengthof(mp_arr_begin));

	static char mp_map_begin[32];
	char *mp_map = mp_map_begin;
	mp_map = mp_encode_map(mp_map, 2);
	mp_map = mp_encode_str0(mp_map, str);
	mp_map = mp_encode_uint(mp_map, 10);
	mp_map = mp_encode_uint(mp_map, 5);
	mp_map = mp_encode_bool(mp_map, false);
	fail_if(mp_map > mp_map_begin + lengthof(mp_map_begin));

	struct tuple *tuple =
		tuple_new(tuple_format_runtime, mp_arr_begin, mp_arr);

	/* Fill port with created objects. */
	port_c_create(port);
	port_c_add_str(port, str, strlen(str));
	port_c_add_tuple(port, tuple);
	port_c_add_mp(port, mp_arr_begin, mp_arr);
	port_c_add_mp(port, mp_map_begin, mp_map);

	struct port_c_contents contents = {
		.tuple = tuple,
		.mp_arr = mp_arr_begin,
		.mp_arr_end = mp_arr,
		.mp_map = mp_map_begin,
		.mp_map_end = mp_map,
	};
	return contents;
}

static void
test_port_c_dump_lua(void)
{
	plan(14);
	header();

	struct port port;
	struct lua_State *L = lua_newthread(tarantool_L);
	fail_if(lua_gettop(L) != 0);

	/* Check if an empty port is dumped correctly. */
	port_c_create(&port);
	test_check_port_dump_lua_flat(&port, L);
	test_check_port_dump_lua_table(&port, L);
	port_destroy(&port);
	lua_settop(L, 0);

	struct port_c_contents contents = test_port_c_create(&port);

	lua_pushstring(L, "abc");
	luaT_pushtuple(L, contents.tuple);
	lua_push_values(L, "{'abc', 10, true, 42.12}");
	lua_push_values(L, "{abc = 10, [5] = false}");

	test_check_port_dump_lua_flat(&port, L);
	test_check_port_dump_lua_table(&port, L);

	footer();
	check_plan();
}

static void
test_port_c_all_msgpack_methods(void)
{
	plan(14);
	header();

	char buf[128];
	char *mp = buf;
	mp = mp_encode_array(mp, 0);

	struct port port;
	port_c_create(&port);
	test_check_port_get_msgpack(&port, buf, mp - buf);
	test_check_port_dump_msgpack(&port, buf, mp - buf, /*no_header=*/true);
	test_check_port_dump_lua_mp_object(&port, buf, mp - buf);
	struct port_c_contents contents = test_port_c_create(&port);

	/* Rewind MsgPack cursor. */
	mp = buf;
	mp = mp_encode_array(mp, 4);

	mp = mp_encode_str0(mp, "abc");

	uint32_t size;
	const char *data = tuple_data_range(contents.tuple, &size);
	memcpy(mp, data, size);
	mp += size;

	size = contents.mp_arr_end - contents.mp_arr;
	memcpy(mp, contents.mp_arr, size);
	mp += size;

	size = contents.mp_map_end - contents.mp_map;
	memcpy(mp, contents.mp_map, size);
	mp += size;

	fail_if(mp > buf + lengthof(buf));

	test_check_port_get_msgpack(&port, buf, mp - buf);
	test_check_port_dump_msgpack(&port, buf, mp - buf, /*no_header=*/true);
	test_check_port_dump_lua_mp_object(&port, buf, mp - buf);

	footer();
	check_plan();
}

static void
test_port_c(void)
{
	plan(2);
	header();

	test_port_c_dump_lua();
	test_port_c_all_msgpack_methods();

	footer();
	check_plan();
}

/** Tests for port_lua. */
struct port_lua_contents {
	double number;
	const char *str;
	struct tuple *tuple;
	bool boolean;
};

/**
 * Creates port_lua and fills it.
 * Flag push_cdata is required because MsgPack methods do
 * not support tuples.
 * Flag with_bottom is used to test both port_lua_create and
 * port_lua_create_at which dumps Lua values starting from
 * bottom index - if it is set, port is created with function
 * port_lua_create_at with bottom greater than 1.
 */
static struct port_lua_contents
test_port_lua_create(struct port *port, bool push_cdata, bool with_bottom)
{
	struct lua_State *L = lua_newthread(tarantool_L);

	/* Prepare to fill - create all required objects. */
	double number = 3.14;
	bool boolean = false;
	const char *str = "abc";
	struct tuple *tuple = NULL;
	int bottom = 4;

	if (with_bottom) {
		/* Fill space under bottom with numbers. */
		for (int i = 1; i < bottom; i++)
			lua_pushnumber(L, i);
	}
	lua_pushnil(L);
	luaL_pushnull(L);
	lua_pushnumber(L, number);
	lua_pushstring(L, str);
	lua_pushboolean(L, boolean);
	if (push_cdata) {
		static char mp_arr_begin[32];
		char *mp_arr = mp_arr_begin;
		mp_arr = mp_encode_array(mp_arr, 4);
		mp_arr = mp_encode_str0(mp_arr, str);
		mp_arr = mp_encode_uint(mp_arr, 10);
		mp_arr = mp_encode_bool(mp_arr, true);
		mp_arr = mp_encode_double(mp_arr, 42.12);
		fail_if(mp_arr > mp_arr_begin + lengthof(mp_arr_begin));
		tuple = tuple_new(tuple_format_runtime, mp_arr_begin, mp_arr);
		luaT_pushtuple(L, tuple);
	}
	if (with_bottom)
		port_lua_create_at(port, L, bottom);
	else
		port_lua_create(port, L);

	struct port_lua_contents contents = {
		.number = number,
		.str = str,
		.tuple = tuple,
		.boolean = boolean,
	};
	return contents;
}

/**
 * Creates an empty port_lua.
 * Flag with_bottom is used to test both port_lua_create and
 * port_lua_create_with_bottom.
 */
static void
test_port_lua_create_empty(struct port *port, bool with_bottom)
{
	struct lua_State *L = lua_newthread(tarantool_L);
	fail_if(lua_gettop(L) != 0);
	int bottom = 3;

	if (with_bottom) {
		/* Fill space under bottom with numbers. */
		for (int i = 1; i < bottom; i++)
			lua_pushnumber(L, i);
		port_lua_create_at(port, L, bottom);
	} else {
		port_lua_create(port, L);
	}
}

/*
 * Checks port_lua_dump_lua method.
 * If flag with_bottom is true, all Lua stacks are created with
 * port_lua_create_at method with bottom greater than 1.
 */
static void
test_port_lua_dump_lua_impl(bool with_bottom)
{
	struct port port;
	struct lua_State *empty_L = lua_newthread(tarantool_L);
	fail_if(lua_gettop(empty_L) != 0);

	test_port_lua_create_empty(&port, with_bottom);
	test_check_port_dump_lua_flat(&port, empty_L);
	port_destroy(&port);

	test_port_lua_create(&port, /*push_cdata=*/true, with_bottom);
	struct port_lua *port_lua = (struct port_lua *)&port;
	struct lua_State *L = port_lua->L;
	struct lua_State *copy_L = lua_newthread(tarantool_L);
	int top = lua_gettop(L);
	for (int i = port_lua->bottom; i <= top; i++)
		lua_pushvalue(L, i);
	lua_xmove(L, copy_L, top - port_lua->bottom + 1);
	test_check_port_dump_lua_flat(&port, copy_L);
	port_destroy(&port);
}

static void
test_port_lua_dump_lua(void)
{
	plan(8);
	header();
	test_port_lua_dump_lua_impl(/*with_bottom=*/false);
	footer();
	check_plan();
}

static void
test_port_lua_dump_lua_with_bottom(void)
{
	plan(8);
	header();
	test_port_lua_dump_lua_impl(/*with_bottom=*/true);
	footer();
	check_plan();
}

/**
 * Checks port_lua_{dump,get}_msgpack methods.
 * The for loop is required because port_lua is truncated on dump or get.
 * If flag with_bottom is true, all Lua stacks are created with
 * port_lua_create_at method with bottom greater than 1.
 */
static void
test_port_lua_all_msgpack_methods_impl(bool with_bottom)
{
	test_check_msgpack_method checkers[] = {
		test_check_port_get_msgpack,
		test_check_port_dump_msgpack_no_header,
		test_check_port_dump_lua_mp_object,
	};

	for (size_t i = 0; i < lengthof(checkers); i++) {
		struct port port;
		struct lua_State *empty_port_L = lua_newthread(tarantool_L);
		fail_if(lua_gettop(empty_port_L) != 0);

		char buf[256];
		char *mp = buf;
		mp = mp_encode_array(mp, 0);

		test_port_lua_create_empty(&port, with_bottom);
		checkers[i](&port, buf, mp - buf);
		port_destroy(&port);

		struct port_lua_contents contents =
			test_port_lua_create(&port, /*push_cdata=*/false,
					     with_bottom);

		/* Rewind MsgPack cursor. */
		mp = buf;
		mp = mp_encode_array(mp, 5);
		mp = mp_encode_nil(mp);
		mp = mp_encode_nil(mp);
		mp = mp_encode_double(mp, contents.number);
		mp = mp_encode_str0(mp, contents.str);
		mp = mp_encode_bool(mp, false);

		fail_if(mp > buf + lengthof(buf));

		checkers[i](&port, buf, mp - buf);

		port_destroy(&port);
	}
}

static void
test_port_lua_all_msgpack_methods(void)
{
	plan(14);
	header();
	test_port_lua_all_msgpack_methods_impl(/*with_bottom=*/false);
	footer();
	check_plan();
}

static void
test_port_lua_all_msgpack_methods_with_bottom(void)
{
	plan(14);
	header();
	test_port_lua_all_msgpack_methods_impl(/*with_bottom=*/true);
	footer();
	check_plan();
}

static void
test_port_lua(void)
{
	plan(4);
	header();

	test_port_lua_dump_lua();
	test_port_lua_dump_lua_with_bottom();
	test_port_lua_all_msgpack_methods();
	test_port_lua_all_msgpack_methods_with_bottom();

	footer();
	check_plan();
}

/** Tests for port_msgpack. */

struct port_msgpack_contents {
	const char *mp;
	const char *mp_end;
};

static struct port_msgpack_contents
test_port_msgpack_create(struct port *port)
{
	/* Prepare to fill - create all required objects */
	const char *str = "abc";
	double number = 3.14;
	unsigned uint = 10;
	bool boolean = false;

	static char mp_begin[128];
	char *mp = mp_begin;
	mp = mp_encode_array(mp, 5);
	mp = mp_encode_str0(mp, str);
	mp = mp_encode_uint(mp, uint);
	mp = mp_encode_bool(mp, boolean);

	/* 4th element - array of 3 elements */
	mp = mp_encode_array(mp, 3);
	mp = mp_encode_double(mp, number);
	mp = mp_encode_str0(mp, str);
	mp = mp_encode_map(mp, 1);
	mp = mp_encode_str0(mp, str);
	mp = mp_encode_uint(mp, uint);

	/* 5th element - map of 2 elements */
	mp = mp_encode_map(mp, 2);
	mp = mp_encode_str0(mp, str);
	mp = mp_encode_double(mp, number);
	mp = mp_encode_uint(mp, uint);
	mp = mp_encode_str0(mp, str);

	fail_if(mp > mp_begin + lengthof(mp_begin));

	/* Fill port with created objects. */
	port_msgpack_create(port, mp_begin, mp - mp_begin);

	struct port_msgpack_contents contents = {
		.mp = mp_begin,
		.mp_end = mp,
	};
	return contents;
}

static void
test_port_msgpack_dump_lua(void)
{
	plan(6);
	header();

	struct port port;
	struct port_msgpack_contents contents = test_port_msgpack_create(&port);

	struct lua_State *L = lua_newthread(tarantool_L);
	lua_push_values(L, "'abc', 10, false, {3.14, 'abc', {abc = 10}}, "
			"{abc = 3.14, [10] = 'abc'}");

	test_check_port_dump_lua_flat(&port, L);

	footer();
	check_plan();
}

static void
test_port_msgpack_all_msgpack_methods(void)
{
	plan(7);
	header();

	struct port port;
	struct port_msgpack_contents contents = test_port_msgpack_create(&port);

	uint32_t size = contents.mp_end - contents.mp;
	test_check_port_get_msgpack(&port, contents.mp, size);
	test_check_port_dump_msgpack(&port, contents.mp, size,
				     /*no_header=*/false);
	test_check_port_dump_lua_mp_object(&port, contents.mp, size);

	footer();
	check_plan();
}

static void
test_port_msgpack(void)
{
	plan(2);
	header();

	test_port_msgpack_dump_lua();
	test_port_msgpack_all_msgpack_methods();

	footer();
	check_plan();
}

static void
test_main(void)
{
	plan(3);
	header();

	test_port_c();
	test_port_lua();
	test_port_msgpack();

	footer();
	check_plan();
}

int
main(void)
{
	memory_init();
	tuple_init(NULL);
	port_init();
	event_init();
	user_cache_init();
	session_init();

	struct lua_State *L = luaT_newteststate();
	tarantool_L = L;
	tarantool_lua_error_init(L);
	tarantool_lua_utils_init(L);
	luaopen_msgpack(L);
	lua_pop(L, 1);
	box_lua_tuple_init(L);
	box_lua_call_init(L);
	lua_table_equal_init(L);

	/*
	 * luaT_newmodule() assumes that tarantool has a special
	 * loader for built-in modules. That's true, when all the
	 * initialization code is executed. However, in the unit
	 * test we don't do that.
	 *
	 * In particular, tarantool_lua_init() function is not
	 * called in a unit test.
	 *
	 * Assign the module into package.loaded directly instead.
	 *
	 *  | local mod = loaders.builtin['msgpack']
	 *  | package.loaded['msgpack'] = mod
	 */
	lua_getfield(L, LUA_REGISTRYINDEX, "_LOADED");
	lua_getfield(L, LUA_REGISTRYINDEX, "_TARANTOOL_BUILTIN");
	lua_getfield(L, -1, "msgpack");
	lua_setfield(L, -3, "msgpack");
	lua_pop(L, 2);

	fail_unless(luaT_dostring(
			L, "mp = require('msgpack')") == 0);

	test_main();

	session_free();
	user_cache_free();
	event_free();
	port_free();
	tuple_free();
	memory_free();
}
