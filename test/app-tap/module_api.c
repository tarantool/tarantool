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
	fiber_join(fiber);
	box_error_t *err = box_error_last();
	lua_pushboolean(L, (int )(err == NULL || box_error_code(err) != 10));
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

LUA_API int
luaopen_module_api(lua_State *L)
{
	(void) consts;
	static const struct luaL_reg lib[] = {
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
		{"test_clock", test_clock },
		{"test_pushtuple", test_pushtuple},
		{NULL, NULL}
	};
	luaL_register(L, "module_api", lib);
	return 1;
}
