#include <stdbool.h>
#include <module.h>

#include <small/ibuf.h>
#include <msgpuck/msgpuck.h>

#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include <lua.h>
#include <lauxlib.h>

#define STR2(x) #x
#define STR(x) STR2(x)

#ifndef lengthof
#define lengthof(array) (sizeof(array) / sizeof((array)[0]))
#endif

/* Test for constants */
static const char *consts[] = {
	PACKAGE_VERSION,
	STR(PACKAGE_VERSION_MINOR),
	STR(PACKAGE_VERSION_MAJOR),
	STR(PACKAGE_VERSION_PATCH),
	TARANTOOL_C_FLAGS,
	TARANTOOL_CXX_FLAGS,
	MODULE_LIBDIR,
	MODULE_LUADIR,
	MODULE_INCLUDEDIR
};

static int
test_say(lua_State *L)
{
	say_debug("test debug");
	say_info("test info");
	say_verbose("test verbose");
	say_warn("test warn");
	say_crit("test crit");
	say_error("test error");
	errno = 0;
	say_syserror("test sysserror");
	lua_pushboolean(L, 1);
	return 1;
}

static ssize_t
coio_call_func(va_list ap)
{
	return va_arg(ap, int);
}

static int
test_coio_call(lua_State *L)
{
	ssize_t rc = coio_call(coio_call_func, 48);
	lua_pushboolean(L, rc == 48);
	return 1;
}

static int
test_coio_getaddrinfo(lua_State *L)
{
	struct addrinfo hints;
	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_UNSPEC; /* Allow IPv4 or IPv6 */
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_ADDRCONFIG|AI_PASSIVE;
	hints.ai_protocol = 0;
	struct addrinfo *ai = NULL;
	if (coio_getaddrinfo("localhost", "80", &hints, &ai, 0.1) == 0)
		freeaddrinfo(ai);
	lua_pushboolean(L, 1);
	return 1;
}

static int
test_pushcheck_cdata(lua_State *L)
{
	uint32_t uint64_ctypeid = luaL_ctypeid(L, "uint64_t");
	*(uint64_t *) luaL_pushcdata(L, uint64_ctypeid) = 48;
	uint32_t test_ctypeid = 0;
	luaL_checkcdata(L, -1, &test_ctypeid);
	lua_pushboolean(L, test_ctypeid != 0 && uint64_ctypeid == test_ctypeid);
	return 1;
}

static int
test_pushuint64(lua_State *L)
{
	uint32_t ctypeid = 0;
	uint64_t num = 18446744073709551615ULL;
	luaL_pushuint64(L, num);
	uint64_t r = *(uint64_t *) luaL_checkcdata(L, -1, &ctypeid);
	lua_pushboolean(L, r == num && ctypeid == luaL_ctypeid(L, "uint64_t"));
	return 1;
}

static int
test_pushint64(lua_State *L)
{
	uint32_t ctypeid = 0;
	int64_t num = 9223372036854775807LL;
	luaL_pushint64(L, num);
	int64_t r = *(int64_t *) luaL_checkcdata(L, -1, &ctypeid);
	lua_pushboolean(L, r == num && ctypeid == luaL_ctypeid(L, "int64_t"));
	return 1;
}

static int
test_checkuint64(lua_State *L)
{
	lua_pushnumber(L, 12345678);
	if (luaL_checkuint64(L, -1) != 12345678)
		return 0;
	lua_pop(L, 1);

	lua_pushliteral(L, "18446744073709551615");
	if (luaL_checkuint64(L, -1) != 18446744073709551615ULL)
		return 0;
	lua_pop(L, 1);

	luaL_pushuint64(L, 18446744073709551615ULL);
	if (luaL_checkuint64(L, -1) != 18446744073709551615ULL)
		return 0;
	lua_pop(L, 1);

	lua_pushboolean(L, 1);
	return 1;
}

static int
test_checkint64(lua_State *L)
{
	lua_pushnumber(L, 12345678);
	if (luaL_checkint64(L, -1) != 12345678)
		return 0;
	lua_pop(L, 1);

	lua_pushliteral(L, "9223372036854775807");
	if (luaL_checkint64(L, -1) != 9223372036854775807LL)
		return 0;
	lua_pop(L, 1);

	luaL_pushint64(L, 9223372036854775807LL);
	if (luaL_checkint64(L, -1) != 9223372036854775807LL)
		return 0;
	lua_pop(L, 1);

	lua_pushboolean(L, 1);
	return 1;
}

static int
test_touint64(lua_State *L)
{
	lua_pushliteral(L, "xxx");
	if (luaL_touint64(L, -1) != 0)
		return 0;
	lua_pop(L, 1);

	luaL_pushuint64(L, 18446744073709551615ULL);
	if (luaL_touint64(L, -1) != 18446744073709551615ULL)
		return 0;
	lua_pop(L, 1);

	lua_pushliteral(L, "not a cdata");
	luaL_pushuint64(L, 18446744073709551615ULL);
	if (luaL_touint64(L, -1) != 18446744073709551615ULL)
		return 0;
	lua_pop(L, 2);

	lua_pushboolean(L, 1);
	return 1;
}

static int
test_toint64(lua_State *L)
{
	lua_pushliteral(L, "xxx");
	if (luaL_toint64(L, -1) != 0)
		return 0;
	lua_pop(L, 1);

	luaL_pushint64(L, 9223372036854775807);
	if (luaL_toint64(L, -1) != 9223372036854775807)
		return 0;
	lua_pop(L, 1);

	lua_pushliteral(L, "not a cdata");
	luaL_pushuint64(L, 18446744073709551615ULL);
	if (luaL_touint64(L, -1) != 18446744073709551615ULL)
		return 0;
	lua_pop(L, 2);

	lua_pushboolean(L, 1);
	return 1;
}

int fiber_test_func(va_list va)
{
	(void) va;
	do {
		fiber_set_cancellable(true);
		fiber_sleep(0.01);
		if (fiber_is_cancelled()) {
			box_error_set(__FILE__, __LINE__, 10, "test error");
			return -1;
		}
		fiber_set_cancellable(false);
	} while (1);
	return 0;
}


static int
test_fiber(lua_State *L)
{
	struct fiber *fiber = fiber_new("test fiber", fiber_test_func);
	fiber_set_joinable(fiber, true);
	fiber_start(fiber);
	fiber_cancel(fiber);
	int ret = fiber_join(fiber);
	box_error_t *err = box_error_last();
	lua_pushboolean(L, (int)(ret != 0 && box_error_code(err) == 10));
	return 1;
}

static int
test_cord(lua_State *L)
{
	struct slab_cache *slabc = cord_slab_cache();
	assert(slabc != NULL);
	struct ibuf ibuf;
	ibuf_create(&ibuf, slabc, 16320);
	ibuf_destroy(&ibuf);

	lua_pushboolean(L, 1);
	return 1;
}

static int
test_pushcdata(lua_State *L)
{
	if (lua_gettop(L) < 1)
		luaL_error(L, "invalid arguments");
	uint32_t ctypeid = lua_tointeger(L, 1);
	void *data = luaL_pushcdata(L, ctypeid);
	lua_pushlightuserdata(L, data);
	return 2;
}

static int
test_checkcdata(lua_State *L)
{
	if (lua_gettop(L) < 1)
		luaL_error(L, "invalid arguments");
	uint32_t ctypeid = 0;
	void *data = luaL_checkcdata(L, 1, &ctypeid);
	lua_pushinteger(L, ctypeid);
	lua_pushlightuserdata(L, data);
	return 2;
}

static int
test_clock(lua_State *L)
{
	/* Test compilation */
	clock_realtime();
	clock_monotonic();
	clock_process();
	clock_thread();

	clock_realtime64();
	clock_monotonic64();
	clock_process64();
	clock_thread64();

	lua_pushboolean(L, 1);
	return 1;
}

static int
test_pushtuple(lua_State *L)
{
	char tuple_buf[64];
	char *tuple_end = tuple_buf;
	tuple_end = mp_encode_array(tuple_end, 3);
	tuple_end = mp_encode_uint(tuple_end, 456734643353);
	tuple_end = mp_encode_str(tuple_end, "abcddcba", 8);
	tuple_end = mp_encode_array(tuple_end, 2);
	tuple_end = mp_encode_map(tuple_end, 2);
	tuple_end = mp_encode_uint(tuple_end, 8);
	tuple_end = mp_encode_uint(tuple_end, 4);
	tuple_end = mp_encode_array(tuple_end, 1);
	tuple_end = mp_encode_str(tuple_end, "a", 1);
	tuple_end = mp_encode_str(tuple_end, "b", 1);
	tuple_end = mp_encode_nil(tuple_end);
	assert(tuple_end <= tuple_buf + sizeof(tuple_buf));
	box_tuple_format_t *fmt = box_tuple_format_default();
	luaT_pushtuple(L, box_tuple_new(fmt, tuple_buf, tuple_end));
	struct tuple *tuple = luaT_istuple(L, -1);
	if (tuple == NULL)
		goto error;

	char lua_buf[sizeof(tuple_buf)];
	int lua_buf_size = box_tuple_to_buf(tuple, lua_buf, sizeof(lua_buf));
	if (lua_buf_size != tuple_end - tuple_buf)
		goto error;
	if (memcmp(tuple_buf, lua_buf, lua_buf_size) != 0)
		goto error;
	lua_pushboolean(L, true);
	return 1;
error:
	lua_pushboolean(L, false);
	return 1;
}

static int
test_key_def_api(lua_State *L)
{
	uint32_t fieldno1[] = {3, 0};
	uint32_t type1[] = {FIELD_TYPE_UNSIGNED, FIELD_TYPE_STRING};
	uint32_t fieldno2[] = {1};
	uint32_t type2[] = {FIELD_TYPE_UNSIGNED};
	box_key_def_t *key_defs[] = {
		box_key_def_new(fieldno1, type1, 2),
		box_key_def_new(fieldno2, type2, 1)};
	box_tuple_format_t *format = box_tuple_format_new(key_defs, 2);
	char buf[64], *buf_end;
	buf_end = buf;
	buf_end = mp_encode_array(buf_end, 4);
	buf_end = mp_encode_str(buf_end, "bb", 2);
	buf_end = mp_encode_uint(buf_end, 1);
	buf_end = mp_encode_str(buf_end, "abcd", 4);
	buf_end = mp_encode_uint(buf_end, 6);
	box_tuple_t *tuple1 = box_tuple_new(format, buf, buf_end);
	box_tuple_ref(tuple1);
	buf_end = buf;
	buf_end = mp_encode_array(buf_end, 4);
	buf_end = mp_encode_str(buf_end, "aa", 2);
	buf_end = mp_encode_uint(buf_end, 8);
	buf_end = mp_encode_nil(buf_end);
	buf_end = mp_encode_uint(buf_end, 6);
	box_tuple_t *tuple2 = box_tuple_new(format, buf, buf_end);

	/* Enocode key */
	buf_end = buf;
	buf_end = mp_encode_array(buf_end, 2);
	buf_end = mp_encode_uint(buf_end, 6);
	buf_end = mp_encode_str(buf_end, "aa", 2);

	bool cmp1 = box_tuple_compare(tuple1, tuple2, key_defs[0]) > 0;
	bool cmp2 = box_tuple_compare(tuple1, tuple2, key_defs[1]) < 0;
	bool cmp3 = box_tuple_compare_with_key(tuple1, buf, key_defs[0]) > 0;
	bool cmp4 = box_tuple_compare_with_key(tuple2, buf, key_defs[0]) == 0;
	box_tuple_unref(tuple1);
	lua_pushboolean(L, cmp1 && cmp2 && cmp3 && cmp4);
	box_tuple_format_unref(format);
	box_key_def_delete(key_defs[0]);
	box_key_def_delete(key_defs[1]);
	return 1;
}

static int
check_error(lua_State *L)
{
	box_error_raise(ER_UNSUPPORTED, "test for luaT_error");
	luaT_error(L);
	return 1;
}

static int
test_call(lua_State *L)
{
	assert(luaL_loadbuffer(L, "", 0, "=eval") == 0);
	assert(luaT_call(L, 0, LUA_MULTRET) == 0);
	lua_pushboolean(L, true);
	return 1;
}

static int
cpcall_handler(lua_State *L)
{
	(void) L;
	return 0;
}

static int
test_cpcall(lua_State *L)
{
	assert(luaT_cpcall(L, cpcall_handler, 0) == 0);
	(void)cpcall_handler;
	lua_pushboolean(L, true);
	return 1;
}

static int
test_state(lua_State *L)
{
	lua_State *tarantool_L = luaT_state();
	assert(lua_newthread(tarantool_L) != 0);
	(void)tarantool_L;
	lua_pushboolean(L, true);
	return 1;
}

static int table_tostring(lua_State *L) {
	lua_pushstring(L, "123");
	return 1;
}

static int
test_tostring(lua_State *L)
{
	/* original table */
	lua_createtable(L, 0, 0);
	/* meta-table */
	lua_createtable(L, 0, 0);
	/* pushing __tostring function */
	lua_pushcfunction(L, table_tostring);
	lua_setfield(L, -2, "__tostring");
	/* setting metatable */
	lua_setmetatable(L, -2);
	assert(strcmp(luaT_tolstring(L, -1, NULL), "123") == 0);

	lua_pushnumber(L, 1);
	assert(strcmp(luaT_tolstring(L, -1, NULL), "1") == 0);

	lua_createtable(L, 0, 0);
	assert(strncmp(luaT_tolstring(L, -1, NULL), "table: ", 7) == 0);

	lua_pushboolean(L, true);
	assert(strcmp(luaT_tolstring(L, -1, NULL), "true") == 0);

	lua_pushboolean(L, false);
	assert(strcmp(luaT_tolstring(L, -1, NULL), "false") == 0);

	lua_pushnil(L);
	assert(strcmp(luaT_tolstring(L, -1, NULL), "nil") == 0);

	lua_pushboolean(L, true);
	return 1;
}

static int
test_iscdata(struct lua_State *L)
{
	assert(lua_gettop(L) == 2);

	int exp = lua_toboolean(L, 2);

	/* Basic test. */
	int res = luaL_iscdata(L, 1);
	int ok = res == exp;
	assert(lua_gettop(L) == 2);

	/* Use negative index. */
	res = luaL_iscdata(L, -2);
	ok = ok && res == exp;
	assert(lua_gettop(L) == 2);

	lua_pushboolean(L, ok);
	return 1;
}

/* {{{ test_box_region */

/**
 * Verify basic usage of box region.
 */
static int
test_box_region(struct lua_State *L)
{
	size_t region_svp_0 = box_region_used();

	/* Verify allocation and box_region_used(). */
	size_t size_arr[] = {1, 7, 19, 10 * 1024 * 1024, 1, 18, 1024};
	size_t region_svp_arr[lengthof(size_arr)];
	char *ptr_arr[lengthof(size_arr)];
	for (size_t i = 0; i < lengthof(size_arr); ++i) {
		size_t size = size_arr[i];
		size_t region_svp = box_region_used();
		char *ptr = box_region_alloc(size);

		/* Verify box_region_used() after allocation. */
		assert(box_region_used() - region_svp == size);

		/* Verify that data is accessible. */
		for (char *p = ptr; p < ptr + size; ++p)
			*p = 'x';

		/*
		 * Save data pointer and savepoint to verify
		 * truncation later.
		 */
		ptr_arr[i] = ptr;
		region_svp_arr[i] = region_svp;
	}

	/* Verify truncation. */
	for (ssize_t i = lengthof(region_svp_arr) - 1; i >= 0; --i) {
		box_region_truncate(region_svp_arr[i]);
		assert(box_region_used() == region_svp_arr[i]);

		/*
		 * Verify that all data before this savepoint
		 * still accessible.
		 */
		for (ssize_t j = 0; j < i; ++j) {
			size_t size = size_arr[j];
			char *ptr = ptr_arr[j];
			for (char *p = ptr; p < ptr + size; ++p) {
				assert(*p == 'x' || *p == 'y');
				*p = 'y';
			}
		}
	}
	assert(box_region_used() == region_svp_0);

	/* Verify aligned allocation. */
	size_t a_size_arr[] = {1, 3, 5, 7, 11, 13, 17, 19};
	size_t alignment_arr[] = {1, 2, 4, 8, 16, 32, 64};
	for (size_t s = 0; s < lengthof(a_size_arr); ++s) {
		for (size_t a = 0; a < lengthof(alignment_arr); ++a) {
			size_t size = a_size_arr[s];
			size_t alignment = alignment_arr[a];
			char *ptr = box_region_aligned_alloc(size, alignment);
			assert((uintptr_t) ptr % alignment == 0);

			/* Data is accessible. */
			for (char *p = ptr; p < ptr + size; ++p)
				*p = 'x';
		}
	}

	/* Clean up. */
	box_region_truncate(region_svp_0);

	lua_pushboolean(L, true);
	return 1;
}

/* }}} test_box_region */

/* {{{ test_tuple_encode */

static void
check_tuple_data(char *tuple_data, size_t tuple_size, int retvals)
{
	(void)tuple_data;
	(void)tuple_size;
	(void)retvals;
	assert(tuple_size == 4);
	assert(tuple_data != NULL);
	assert(!strncmp(tuple_data, "\x93\x01\x02\x03", 4));
	assert(retvals == 0);
}

static void
check_encode_error(char *tuple_data, int retvals, const char *exp_err_type,
		   const char *exp_err_msg)
{
	(void)tuple_data;
	(void)retvals;
	(void)exp_err_type;
	(void)exp_err_msg;
	assert(tuple_data == NULL);
	box_error_t *e = box_error_last();
	(void)e;
	assert(strcmp(box_error_type(e), exp_err_type) == 0);
	assert(strcmp(box_error_message(e), exp_err_msg) == 0);
	assert(retvals == 0);
}

/**
 * Encode a Lua table or a tuple into a tuple.
 *
 * Similar to <luaT_tuple_new>() unit test.
 */
static int
test_tuple_encode(struct lua_State *L)
{
	int top;
	char *tuple_data;
	size_t tuple_size;

	size_t region_svp = box_region_used();

	/*
	 * Case: a Lua table on idx == -2 as an input.
	 */

	/* Prepare the Lua stack. */
	luaL_loadstring(L, "return {1, 2, 3}");
	lua_call(L, 0, 1);
	lua_pushnil(L);

	/* Create and check a tuple. */
	top = lua_gettop(L);
	tuple_data = luaT_tuple_encode(L, -2, &tuple_size);
	check_tuple_data(tuple_data, tuple_size, lua_gettop(L) - top);

	/* Clean up. */
	lua_pop(L, 2);
	assert(lua_gettop(L) == 0);

	/*
	 * Case: a tuple on idx == -1 as an input.
	 */

	/* Prepare the Lua stack. */
	luaL_loadstring(L, "return box.tuple.new({1, 2, 3})");
	lua_call(L, 0, 1);

	/* Create and check a tuple. */
	top = lua_gettop(L);
	tuple_data = luaT_tuple_encode(L, -1, &tuple_size);
	check_tuple_data(tuple_data, tuple_size, lua_gettop(L) - top);

	/* Clean up. */
	lua_pop(L, 1);
	assert(lua_gettop(L) == 0);

	/*
	 * Case: a Lua object of an unexpected type.
	 */

	/* Prepare the Lua stack. */
	lua_pushinteger(L, 42);

	/* Try to encode and check for the error. */
	top = lua_gettop(L);
	tuple_data = luaT_tuple_encode(L, -1, &tuple_size);
	check_encode_error(tuple_data, lua_gettop(L) - top, "IllegalParams",
			   "A tuple or a table expected, got number");

	/* Clean up. */
	lua_pop(L, 1);
	assert(lua_gettop(L) == 0);

	/*
	 * Case: unserializable item within a Lua table.
	 *
	 * The function should not raise a Lua error.
	 */
	luaL_loadstring(L, "return {function() end}");
	lua_call(L, 0, 1);

	/* Try to encode and check for the error. */
	top = lua_gettop(L);
	tuple_data = luaT_tuple_encode(L, -1, &tuple_size);
	check_encode_error(tuple_data, lua_gettop(L) - top, "LuajitError",
			   "unsupported Lua type 'function'");

	/* Clean up. */
	lua_pop(L, 1);
	assert(lua_gettop(L) == 0);

	box_region_truncate(region_svp);

	lua_pushboolean(L, 1);
	return 1;
}

/* }}} test_tuple_encode */

/* {{{ test_tuple_new */

/**
 * Create a tuple from a Lua table or another tuple.
 *
 * Just basic test. More cases in the luaT_tuple_new.c unit test.
 */
static int
test_tuple_new(struct lua_State *L)
{
	box_tuple_format_t *default_format = box_tuple_format_default();

	/* Prepare the Lua stack. */
	luaL_loadstring(L, "return {1, 2, 3}");
	lua_call(L, 0, 1);

	/* Create a tuple. */
	int top = lua_gettop(L);
	box_tuple_t *tuple = luaT_tuple_new(L, -1, default_format);

	/* Verify size, data and Lua stack top. */
	size_t region_svp = box_region_used();
	size_t tuple_size = box_tuple_bsize(tuple);
	char *tuple_data = box_region_alloc(tuple_size);
	ssize_t rc = box_tuple_to_buf(tuple, tuple_data, tuple_size);
	(void)rc;
	assert(rc == (ssize_t)tuple_size);
	check_tuple_data(tuple_data, tuple_size, lua_gettop(L) - top);

	/* Clean up. */
	box_region_truncate(region_svp);
	lua_pop(L, 1);
	assert(lua_gettop(L) == 0);

	lua_pushboolean(L, 1);
	return 1;
}

/* }}} test_tuple_new */

LUA_API int
luaopen_module_api(lua_State *L)
{
	(void) consts;
	static const struct luaL_Reg lib[] = {
		{"test_say", test_say },
		{"test_coio_call", test_coio_call },
		{"test_coio_getaddrinfo", test_coio_getaddrinfo },
		{"test_pushcheck_cdata", test_pushcheck_cdata },
		{"test_pushuint64", test_pushuint64 },
		{"test_pushint64", test_pushint64 },
		{"test_checkuint64", test_checkuint64 },
		{"test_checkint64", test_checkint64 },
		{"test_touint64", test_touint64 },
		{"test_toint64", test_toint64 },
		{"test_fiber", test_fiber },
		{"test_cord", test_cord },
		{"pushcdata", test_pushcdata },
		{"checkcdata", test_checkcdata },
		{"test_clock", test_clock },
		{"test_pushtuple", test_pushtuple},
		{"test_key_def_api", test_key_def_api},
		{"check_error", check_error},
		{"test_call", test_call},
		{"test_cpcall", test_cpcall},
		{"test_state", test_state},
		{"test_tostring", test_tostring},
		{"iscdata", test_iscdata},
		{"test_box_region", test_box_region},
		{"test_tuple_encode", test_tuple_encode},
		{"test_tuple_new", test_tuple_new},
		{NULL, NULL}
	};
	luaL_register(L, "module_api", lib);
	return 1;
}
