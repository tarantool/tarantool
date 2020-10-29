#include <stdbool.h>
#include <string.h>
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
test_box_ibuf(lua_State *L)
{
	struct slab_cache *slabc = cord_slab_cache();
	assert(slabc != NULL);
	box_ibuf_t ibuf;

	ibuf_create(&ibuf, slabc, 16320);
	assert(ibuf_used(&ibuf) == 0);
	void *ptr = box_ibuf_reserve(&ibuf, 65536);
	(void)ptr;
	assert(ptr != NULL);
	char **rpos;
	char **wpos;
	box_ibuf_read_range(&ibuf, &rpos, &wpos);

	ptr = ibuf_alloc(&ibuf, 10);
	assert(ptr != NULL);

	assert(ibuf_used(&ibuf) == 10);
	assert((*wpos - *rpos) == 10);

	/* let be a little bit paranoid and double check */
	box_ibuf_read_range(&ibuf, &rpos, &wpos);
	assert((*wpos - *rpos) == 10);

	ptr = ibuf_alloc(&ibuf, 10000);
	assert(ptr);
	assert(ibuf_used(&ibuf) == 10010);
	assert((*wpos - *rpos) == 10010);

	size_t unused = ibuf_unused(&ibuf);
	(void)unused;
	char **end;
	box_ibuf_write_range(&ibuf, &wpos, &end);
	assert((*end - *wpos) == (ptrdiff_t)unused);

	ibuf_reset(&ibuf);
	assert(ibuf_used(&ibuf) == 0);
	assert(*rpos == *wpos);

	lua_pushboolean(L, 1);
	return 1;
}

static int
test_toibuf(lua_State *L)
{
	box_ibuf_t *buf;
	buf = luaT_toibuf(L, -1);
	lua_pushboolean(L, buf != NULL);
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

/* {{{ key_def api */

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

/* }}} key_def api */

/* {{{ key_def api v2 */

/*
 * More functions around key_def were exposed to the module API
 * in order to implement external tuple.keydef and tuple.merger
 * modules (gh-5273, gh-5384).
 */

/**
 * Verify that two zero terminated strings are either both NULL
 * or have equal values.
 */
static void
string_check_equal(const char *a, const char *b)
{
	(void)a;
	(void)b;
	if (a == NULL) {
		assert(b == NULL);
	} else {
		assert(b != NULL);
		assert(strlen(a) == strlen(b));
		assert(strcmp(a, b) == 0);
	}
}

/**
 * Verify type and message of an error in the diagnostics area.
 *
 * Accepts a prefix of an actual type or a message. Pass an empty
 * string if you need to verify only type or only message.
 */
static void
check_diag(const char *exp_err_type, const char *exp_err_msg)
{
	(void)exp_err_type;
	(void)exp_err_msg;
	box_error_t *e = box_error_last();
	(void)e;
	assert(strcmp(box_error_type(e), exp_err_type) == 0);
	assert(strcmp(box_error_message(e), exp_err_msg) == 0);
}

/**
 * Create a tuple on runtime arena.
 *
 * Release this tuple using <box_tuple_unref>().
 */
static box_tuple_t *
new_runtime_tuple(const char *tuple_data, size_t tuple_size)
{
	box_tuple_format_t *fmt = box_tuple_format_default();
	const char *tuple_end = tuple_data + tuple_size;
	box_tuple_t *tuple = box_tuple_new(fmt, tuple_data, tuple_end);
	assert(tuple != NULL);
	box_tuple_ref(tuple);
	return tuple;
}

/**
 * Where padding bytes starts.
 */
static size_t
key_part_padding_offset(void)
{
	if (sizeof(void *) * CHAR_BIT == 64)
		return 32;
	if (sizeof(void *) * CHAR_BIT == 32)
		return 20;
	assert(false);
}

/**
 * Mask of all defined flags.
 */
static uint32_t
key_part_def_known_flags(void)
{
	return BOX_KEY_PART_DEF_IS_NULLABLE | BOX_KEY_PART_DEF_EXCLUDE_NULL;
}

/**
 * Default flags value.
 *
 * All unknown bits are set to zero.
 */
static uint32_t
key_part_def_default_flags(void)
{
	return 0;
}

/**
 * Set all <box_key_part_def_t> fields to nondefault values.
 *
 * It also set all padding bytes and unknown flags to non-zero
 * values.
 */
static void
key_part_def_set_nondefault(box_key_part_def_t *part)
{
	size_t padding_offset = key_part_padding_offset();
	uint32_t default_flags = key_part_def_default_flags();

	/*
	 * Give correct non-default values for known fields and
	 * flags. Set unknown flags to non-zero values.
	 */
	part->fieldno = 1;
	part->flags = ~default_flags;
	part->field_type = "string";
	part->collation = "unicode_ci";
	part->path = "foo";

	/* Fill padding with non-zero bytes. */
	char *padding = ((char *) part) + padding_offset;
	size_t padding_size = sizeof(box_key_part_def_t) - padding_offset;
	memset(padding, 0xff, padding_size);
}

/**
 * Verify that all known fields and flags are set to default
 * values.
 */
static void
key_part_def_check_default(box_key_part_def_t *part)
{
	(void)part;
	uint32_t known_flags = key_part_def_known_flags();
	uint32_t default_flags = key_part_def_default_flags();
	(void)known_flags;
	(void)default_flags;

	assert(part->fieldno == 0);
	assert((part->flags & known_flags) == default_flags);
	assert(part->field_type == NULL);
	assert(part->collation == NULL);
	assert(part->path == NULL);
}

/**
 * Verify that all padding bytes and unknown flags are set to
 * zeros.
 */
static void
key_part_def_check_zeros(const box_key_part_def_t *part)
{
	size_t padding_offset = key_part_padding_offset();
	uint32_t unknown_flags = ~key_part_def_known_flags();
	(void)unknown_flags;

	char *padding = ((char *) part) + padding_offset;
	char *padding_end = ((char *) part) + sizeof(box_key_part_def_t);
	for (char *p = padding; p < padding_end; ++p) {
		(void)p;
		assert(*p == 0);
	}

	assert((part->flags & unknown_flags) == 0);
}

/**
 * Check that two key part definitions are equal.
 *
 * It compares only known fields and flags, but ignores padding
 * bytes and unknown flags.
 */
static void
key_part_def_check_equal(const box_key_part_def_t *a,
			 const box_key_part_def_t *b)
{
	uint32_t known_flags = key_part_def_known_flags();
	(void)known_flags;

	assert(a->fieldno == b->fieldno);
	assert((a->flags & known_flags) == (b->flags & known_flags));
	string_check_equal(a->field_type, b->field_type);
	string_check_equal(a->collation, b->collation);
	string_check_equal(a->path, b->path);
}

/**
 * Check <box_key_def_merge>() result against expected one.
 *
 * Allocates temporary values on the box region (a caller should
 * release them).
 */
static void
key_def_check_merge(box_key_part_def_t *a, uint32_t part_count_a,
		    box_key_part_def_t *b, uint32_t part_count_b,
		    box_key_part_def_t *exp, uint32_t part_count_exp)
{
	box_key_def_t *key_def_a = box_key_def_new_v2(a, part_count_a);
	assert(key_def_a != NULL);
	box_key_def_t *key_def_b = box_key_def_new_v2(b, part_count_b);
	assert(key_def_b != NULL);

	box_key_def_t *key_def_res = box_key_def_merge(key_def_a, key_def_b);
	uint32_t part_count_res;
	box_key_part_def_t *res = box_key_def_dump_parts(key_def_res,
							 &part_count_res);
	assert(res != NULL);

	assert(part_count_res == part_count_exp);
	for (uint32_t i = 0; i < part_count_exp; ++i) {
		key_part_def_check_equal(&res[i], &exp[i]);
	}

	box_key_def_delete(key_def_res);
	box_key_def_delete(key_def_b);
	box_key_def_delete(key_def_a);
}

/**
 * Basic <box_key_part_def_create>() and <box_key_def_new_v2>()
 * test.
 */
static int
test_key_def_new_v2(struct lua_State *L)
{
	/* Verify <box_key_part_def_t> binary layout. */
	assert(BOX_KEY_PART_DEF_T_SIZE == 64);
	assert(sizeof(box_key_part_def_t) == BOX_KEY_PART_DEF_T_SIZE);
	assert(offsetof(box_key_part_def_t, fieldno) == 0);
	assert(offsetof(box_key_part_def_t, flags) == 4);
	assert(offsetof(box_key_part_def_t, field_type) == 8);
	if (sizeof(void *) * CHAR_BIT == 64) {
		assert(offsetof(box_key_part_def_t, collation) == 16);
		assert(offsetof(box_key_part_def_t, path) == 24);
	} else if (sizeof(void *) * CHAR_BIT == 32) {
		assert(offsetof(box_key_part_def_t, collation) == 12);
		assert(offsetof(box_key_part_def_t, path) == 16);
	} else {
		assert(false);
	}

	/*
	 * Fill key part definition with nondefault values.
	 * Fill padding and unknown flags with non-zero values.
	 */
	box_key_part_def_t part;
	key_part_def_set_nondefault(&part);

	/*
	 * Verify that all known fields are set to default values and
	 * all unknown fields and flags are set to zeros.
	 */
	box_key_part_def_create(&part);
	key_part_def_check_default(&part);
	key_part_def_check_zeros(&part);

	box_key_def_t *key_def;

	/* Should not accept zero part count. */
	key_def = box_key_def_new_v2(NULL, 0);
	assert(key_def == NULL);
	check_diag("IllegalParams", "At least one key part is required");

	/* Should not accept NULL as a <field_type>. */
	key_def = box_key_def_new_v2(&part, 1);
	assert(key_def == NULL);
	check_diag("IllegalParams", "Field type is mandatory");

	/* Success case. */
	part.field_type = "unsigned";
	key_def = box_key_def_new_v2(&part, 1);
	assert(key_def != NULL);

	/*
	 * Prepare tuples to do some comparisons.
	 *
	 * [1, 2, 3] and [3, 2, 1].
	 */
	box_tuple_t *tuple_1 = new_runtime_tuple("\x93\x01\x02\x03", 4);
	box_tuple_t *tuple_2 = new_runtime_tuple("\x93\x03\x02\x01", 4);

	/*
	 * Verify that key_def actually can be used in functions
	 * that accepts it.
	 *
	 * Do several comparisons. Far away from being an
	 * exhaustive comparator test.
	 */
	int rc;
	(void)rc;
	rc = box_tuple_compare(tuple_1, tuple_1, key_def);
	assert(rc == 0);
	rc = box_tuple_compare(tuple_2, tuple_2, key_def);
	assert(rc == 0);
	rc = box_tuple_compare(tuple_1, tuple_2, key_def);
	assert(rc < 0);
	rc = box_tuple_compare(tuple_2, tuple_1, key_def);
	assert(rc > 0);

	/* The same idea, but perform comparisons against keys. */
	rc = box_tuple_compare_with_key(tuple_1, "\x91\x00", key_def);
	assert(rc > 0);
	rc = box_tuple_compare_with_key(tuple_1, "\x91\x01", key_def);
	assert(rc == 0);
	rc = box_tuple_compare_with_key(tuple_1, "\x91\x02", key_def);
	assert(rc < 0);

	/* Clean up. */
	box_tuple_unref(tuple_1);
	box_tuple_unref(tuple_2);
	box_key_def_delete(key_def);

	lua_pushboolean(L, 1);
	return 1;
}

/**
 * Basic <box_key_def_dump_parts>() test.
 */
static int
test_key_def_dump_parts(struct lua_State *L)
{
	size_t region_svp = box_region_used();
	box_key_def_t *key_def = NULL;
	box_key_part_def_t *dump = NULL;
	uint32_t dump_part_count = 0;

	/*
	 * Create a key_def with a single key part with all fields
	 * and flags set to non-default values.
	 */
	box_key_part_def_t part;
	key_part_def_set_nondefault(&part);
	key_def = box_key_def_new_v2(&part, 1);
	assert(key_def != NULL);

	/*
	 * Verify that the same values are dumped, but unknown
	 * fields and flags are set to zeros.
	 */
	dump = box_key_def_dump_parts(key_def, &dump_part_count);
	assert(dump != NULL);
	assert(dump_part_count == 1);
	key_part_def_check_equal(&part, &dump[0]);
	key_part_def_check_zeros(&dump[0]);

	/* We can pass NULL as <part_count_ptr>. */
	dump = box_key_def_dump_parts(key_def, NULL);
	assert(dump != NULL);

	/* Clean up. */
	box_key_def_delete(key_def);

	/* Create a key_def from two key part definitions. */
	box_key_part_def_t parts[2];
	box_key_part_def_create(&parts[0]);
	box_key_part_def_create(&parts[1]);
	parts[0].fieldno = 19;
	parts[0].field_type = "unsigned";
	parts[0].path = "foo";
	parts[1].fieldno = 7;
	parts[1].field_type = "string";
	parts[1].collation = "unicode";
	parts[1].flags |= BOX_KEY_PART_DEF_IS_NULLABLE;
	key_def = box_key_def_new_v2(parts, 2);
	assert(key_def != NULL);

	/* Verify how it'll be dumped. */
	dump = box_key_def_dump_parts(key_def, &dump_part_count);
	assert(dump != NULL);
	assert(dump_part_count == 2);
	key_part_def_check_equal(&parts[0], &dump[0]);
	key_part_def_check_equal(&parts[1], &dump[1]);

	/* Clean up. */
	box_key_def_delete(key_def);

	/* Can we again create a key_def from the dumped parts? */
	key_def = box_key_def_new_v2(dump, dump_part_count);
	assert(key_def != NULL);

	/* Verify this dump based key_def. */
	dump = box_key_def_dump_parts(key_def, &dump_part_count);
	assert(dump != NULL);
	assert(dump_part_count == 2);
	key_part_def_check_equal(&parts[0], &dump[0]);
	key_part_def_check_equal(&parts[1], &dump[1]);

	/* Clean up. */
	box_key_def_delete(key_def);

	/*
	 * 'none' collation is the same as lack of a collation
	 * from key_def point of view. In the dump it is present
	 * as NULL.
	 */
	parts[1].collation = "none";
	key_def = box_key_def_new_v2(parts, 2);
	assert(key_def != NULL);
	dump = box_key_def_dump_parts(key_def, &dump_part_count);
	assert(dump != NULL);
	assert(dump_part_count == 2);
	/* Set to NULL just for ease verification. */
	parts[1].collation = NULL;
	key_part_def_check_equal(&parts[0], &dump[0]);
	key_part_def_check_equal(&parts[1], &dump[1]);

	/* Clean up. */
	box_key_def_delete(key_def);
	box_region_truncate(region_svp);

	lua_pushboolean(L, 1);
	return 1;
}

/**
 * Basic <box_key_def_validate_tuple>() test.
 */
static int
test_key_def_validate_tuple(struct lua_State *L)
{
	/*
	 * Create a key_def.
	 *
	 *  |              tuple
	 *  |            [x, x, x]
	 *  | key_def     ^     ^
	 *  |    |        |     |
	 *  |   (0) <-----+---- string (optional)
	 *  |    |        |
	 *  |   (1) <---- unsigned
	 */
	box_key_part_def_t parts[2];
	box_key_part_def_create(&parts[0]);
	box_key_part_def_create(&parts[1]);
	parts[0].fieldno = 2;
	parts[0].field_type = "string";
	parts[0].flags |= BOX_KEY_PART_DEF_IS_NULLABLE;
	parts[1].fieldno = 0;
	parts[1].field_type = "unsigned";
	box_key_def_t *key_def = box_key_def_new_v2(parts, 2);
	assert(key_def != NULL);

	/*
	 * Create tuples to validate them against given key_def.
	 *
	 *  | # | tuple         | Is valid? |
	 *  | - | ------------- | --------- |
	 *  | 0 | [1, 2, "moo"] | valid     |
	 *  | 1 | [1, 2, null]  | valid     |
	 *  | 2 | [1, 2]        | valid     |
	 *  | 3 | [1]           | valid     |
	 *  | 4 | []            | invalid   |
	 *  | 5 | [1, 2, 3]     | invalid   |
	 *  | 6 | ["moo"]       | invalid   |
	 *  | 7 | [-1]          | invalid   |
	 */
	box_tuple_t *tuples[] = {
		/* [0] = */ new_runtime_tuple("\x93\x01\x02\xa3moo", 7),
		/* [1] = */ new_runtime_tuple("\x93\x01\x02\xc0", 4),
		/* [2] = */ new_runtime_tuple("\x92\x01\x02", 3),
		/* [3] = */ new_runtime_tuple("\x91\x01", 2),
		/* [4] = */ new_runtime_tuple("\x90", 1),
		/* [5] = */ new_runtime_tuple("\x93\x01\x02\x03", 4),
		/* [6] = */ new_runtime_tuple("\x91\xa3moo", 5),
		/* [7] = */ new_runtime_tuple("\x91\xff", 2),
	};
	int expected_results[] = {
		/* [0] = */ 0,
		/* [1] = */ 0,
		/* [2] = */ 0,
		/* [3] = */ 0,
		/* [4] = */ -1,
		/* [5] = */ -1,
		/* [6] = */ -1,
		/* [7] = */ -1,
	};
	uint32_t expected_error_codes[] = {
		/* [0] = */ box_error_code_MAX,
		/* [1] = */ box_error_code_MAX,
		/* [2] = */ box_error_code_MAX,
		/* [3] = */ box_error_code_MAX,
		/* [4] = */ ER_FIELD_MISSING,
		/* [5] = */ ER_KEY_PART_TYPE,
		/* [6] = */ ER_KEY_PART_TYPE,
		/* [7] = */ ER_KEY_PART_TYPE,
	};

	for (size_t i = 0; i < lengthof(tuples); ++i) {
		int rc = box_key_def_validate_tuple(key_def, tuples[i]);
		assert(rc == expected_results[i]);
		(void)rc;
		(void)expected_results;

		if (expected_error_codes[i] != box_error_code_MAX) {
			assert(rc != 0);
			box_error_t *e = box_error_last();
			(void)e;
			assert(box_error_code(e) == expected_error_codes[i]);
		}
	}

	/* Clean up. */
	for (size_t i = 0; i < lengthof(tuples); ++i)
		box_tuple_unref(tuples[i]);
	box_key_def_delete(key_def);

	lua_pushboolean(L, 1);
	return 1;
}

/**
 * Basic <box_key_def_merge>() test.
 */
static int
test_key_def_merge(struct lua_State *L)
{
	/*
	 * What is the idea of <box_key_def_merge>()?
	 *
	 * (In my humble understanding.)
	 *
	 * For any given kd1 and kd2, kd3 = merge(kd1, kd2) should
	 * impose the same order of tuples as if they would be
	 * ordered by kd1, but all kd1-equal tuples would be
	 * ordered by kd2.
	 *
	 * We could just add all key parts of kd2 to kd1 parts.
	 * However in some cases we can skip some of kd2 parts
	 * (the simplest case: when they are equal). That is what
	 * <box_key_def_merge>() doing in fact.
	 *
	 * Should we provide a guarantee that first len(kd1) parts
	 * of kd3 = merge(kd1, kd2) will be the same as in kd1? Or
	 * those key parts can be strengthen with turning off
	 * nullability, picking up more restrictive field type or
	 * choosing of a more restrictive collation if such
	 * restrictions are defined by kd2?
	 *
	 * The tuples ordering property is guaranteed by the
	 * implementation. In particular, it leans on the fact
	 * that a comparator for a more general type imposes the
	 * same ordering on a more restrictive type as if when a
	 * type-specific comparator is be used. E.g. an order of
	 * any two given unsigned integers is the same when we
	 * comparing them as unsigned integers, as integers, as
	 * numbers or as scalars (note: we don't have comparators
	 * for 'any' type).
	 *
	 * However <box_key_def_t> provides not only comparator
	 * functions, but also validation and key extraction ones.
	 *
	 * Let's consider validation. It looks logical to expect
	 * that the following invariant is guaranteed: for any
	 * given kd1 and kd2, kd3 = merge(kd1, kd2) should accept
	 * only those tuples that both kd1 and kd2 accept (kd
	 * accepts a tuple when it is valid against kd). This is
	 * not so now.
	 *
	 * If the function would impose this guarantee, it must
	 * pay attention to field types compatibility (and which
	 * ones are more restrictive than others) and nullability.
	 * Not sure whether a collation may restrict a set of
	 * possible values (in theory it may be so; at least not
	 * any byte sequence forms a valid UTF-8 string).
	 *
	 * It also looks logical to expect that, when sets of
	 * tuples that are accepted by kd1 and that are accepted
	 * by kd2 have the empty intersection, the merge function
	 * will give an error. It is not so now too.
	 *
	 * If the function would impose this guarantee, it must
	 * handle the case, when the same field is marked with
	 * incompatible types and both key part definitions are
	 * non-nullable. Not sure that it is the only point that
	 * must be taken into account here.
	 *
	 * Now let's consider key extraction from a tuple. For
	 * given kd1 and kd2, a change of the merge algorithm may
	 * change parts count in kd3 = merge(kd1, kd2) and so
	 * parts count in a key extracted by it. It is hard to
	 * say, which guarantees we should provide here. So,
	 * maybe, if we'll touch the merge algorithm, we should
	 * leave the old function as is and expose _v2() function.
	 *
	 * On the other hand, having two implementations of the
	 * merge function with different guarantees, where only
	 * the older one will be used internally is somewhat
	 * strange and may lead to sudden inconsistencies.
	 *
	 * If we'll look at the <box_key_def_merge>() from the
	 * practical point of view, the only known usage of this
	 * function is to provide a comparator that gives exactly
	 * same order as a secondary index in tarantool (when it
	 * is not unique, secondary key parts are merged with the
	 * primary ones). So, it seems, if something should be
	 * changed, it should be changed in sync with internals.
	 *
	 * To sum up: current behaviour is the controversial topic
	 * and we may want to reconsider it in some way in a
	 * future. So let's look to some of the test cases below
	 * as on examples of current behaviour: not as on a
	 * commitment that it'll be the same forever (while the
	 * main property regarding tuples ordering is hold).
	 */

	size_t region_svp = box_region_used();

	/*
	 * Causion: Don't initialize <box_key_part_def_t> directly
	 * in a real world code. Use <box_key_part_def_create>().
	 *
	 * The testing code is updated in sync with tarantool, so
	 * it may lean on the knowledge about particular set of
	 * fields and flags.
	 *
	 * In contrast a module should be able to be built against
	 * an older version of tarantool and correctly run on a
	 * newer one. It also should be able to build against the
	 * newer tarantool version without code changes.
	 *
	 * The <box_key_part_def_t> structure may be updated in a
	 * future version of tarantool. The only permitted updates
	 * are adding new fields or flags, or update of a default
	 * value of a field or a flag. Let's show how it may break
	 * non-conventional code:
	 *
	 * 1. Case: a new field is added.
	 *
	 *    As result, if brace initializer is used,
	 *    -Wmissing-field-initializers (part of -Wextra)
	 *    warning may be produced when building a module
	 *    against the new tarantool version. Usage of -Werror
	 *    for the Debug build is usual, so it may break
	 *    compilation.
	 *
	 * 2. Case: a new field or flag is added with non-zero
	 *    default value or a default value of some field or
	 *    flag is changed.
	 *
	 *    As result a module will initialize the new / changed
	 *    fields or flags with values that are not default for
	 *    given tarantool version, but may assume that
	 *    everything that is not set explicitly is default.
	 */

	/* Non-conventional prerequisite: no new fields. */
	size_t padding_offset = key_part_padding_offset();
	size_t path_field_end = offsetof(box_key_part_def_t, path) +
		sizeof(const char *);
	assert(padding_offset == path_field_end);
	(void)padding_offset;
	(void)path_field_end;

	/* Non-conventional prerequisite: list of known flags. */
	uint32_t known_flags = key_part_def_known_flags();
	assert(known_flags == (BOX_KEY_PART_DEF_IS_NULLABLE |
			       BOX_KEY_PART_DEF_EXCLUDE_NULL));
	(void)known_flags;

	/* Non-conventional prerequisite: certain defaults. */
	box_key_part_def_t tmp;
	box_key_part_def_create(&tmp);
	assert((tmp.flags & BOX_KEY_PART_DEF_IS_NULLABLE) == 0);
	assert((tmp.flags & BOX_KEY_PART_DEF_EXCLUDE_NULL) == 0);
	assert(tmp.collation == NULL);
	assert(tmp.path == NULL);

	/*
	 * The extra parentheses are necessary to initialize
	 * <box_key_part_def_t>, because it is a union around an
	 * anonymous structure and padding, not a structure.
	 */

	/* Case 1: all <fieldno> are different. */
	box_key_part_def_t a_1[] = {
		{{3, 0, "unsigned", NULL, NULL}},
		{{1, 0, "unsigned", NULL, NULL}},
	};
	box_key_part_def_t b_1[] = {
		{{0, 0, "unsigned", NULL, NULL}},
		{{2, 0, "unsigned", NULL, NULL}},
	};
	box_key_part_def_t exp_1[] = {
		{{3, 0, "unsigned", NULL, NULL}},
		{{1, 0, "unsigned", NULL, NULL}},
		{{0, 0, "unsigned", NULL, NULL}},
		{{2, 0, "unsigned", NULL, NULL}},
	};
	key_def_check_merge(a_1, lengthof(a_1), b_1, lengthof(b_1),
			    exp_1, lengthof(exp_1));

	/* Case 2: two key parts are the same. */
	box_key_part_def_t a_2[] = {
		{{3, 0, "unsigned", NULL, NULL}},
		{{1, 0, "unsigned", NULL, NULL}}, /* clash */
	};
	box_key_part_def_t b_2[] = {
		{{1, 0, "unsigned", NULL, NULL}}, /* clash */
		{{2, 0, "unsigned", NULL, NULL}},
	};
	box_key_part_def_t exp_2[] = {
		{{3, 0, "unsigned", NULL, NULL}},
		{{1, 0, "unsigned", NULL, NULL}}, /* coalesced */
		{{2, 0, "unsigned", NULL, NULL}},
	};
	key_def_check_merge(a_2, lengthof(a_2), b_2, lengthof(b_2),
			    exp_2, lengthof(exp_2));

	/*
	 * Case 3: more general field type + more restrictive one.
	 *
	 * Interpretation: when <a> and <b> have key parts that
	 * are point to the same field (considering <fieldno> and
	 * JSON paths) and collations are not present or don't
	 * impose any restrictions, the key part from <b> is
	 * omitted without any care to <field_type> and <flags>.
	 */
	box_key_part_def_t a_3[] = {
		{{3, 0, "unsigned", NULL, NULL}},
		{{1, 0, "number",   NULL, NULL}}, /* clash */
	};
	box_key_part_def_t b_3[] = {
		{{1, 0, "unsigned", NULL, NULL}}, /* clash */
		{{2, 0, "unsigned", NULL, NULL}},
	};
	box_key_part_def_t exp_3[] = {
		{{3, 0, "unsigned", NULL, NULL}},
		{{1, 0, "number",   NULL, NULL}}, /* coalesced */
		{{2, 0, "unsigned", NULL, NULL}},
	};
	key_def_check_merge(a_3, lengthof(a_3), b_3, lengthof(b_3),
			    exp_3, lengthof(exp_3));

	/*
	 * Case 4: more restrictive field type + more general one.
	 *
	 * Interpretation: the same as for the case 3.
	 */
	box_key_part_def_t a_4[] = {
		{{3, 0, "unsigned", NULL, NULL}},
		{{1, 0, "unsigned", NULL, NULL}}, /* clash */
	};
	box_key_part_def_t b_4[] = {
		{{1, 0, "number",   NULL, NULL}}, /* clash */
		{{2, 0, "unsigned", NULL, NULL}},
	};
	box_key_part_def_t exp_4[] = {
		{{3, 0, "unsigned", NULL, NULL}},
		{{1, 0, "unsigned", NULL, NULL}}, /* coalesced */
		{{2, 0, "unsigned", NULL, NULL}},
	};
	key_def_check_merge(a_4, lengthof(a_4), b_4, lengthof(b_4),
			    exp_4, lengthof(exp_4));

	/*
	 * Case 5: incompatible field types.
	 *
	 * Interpretation: the same as for the case 3.
	 */
	box_key_part_def_t a_5[] = {
		{{3, 0, "unsigned", NULL, NULL}},
		{{1, 0, "unsigned", NULL, NULL}}, /* clash */
	};
	box_key_part_def_t b_5[] = {
		{{1, 0, "string",   NULL, NULL}}, /* clash */
		{{2, 0, "unsigned", NULL, NULL}},
	};
	box_key_part_def_t exp_5[] = {
		{{3, 0, "unsigned", NULL, NULL}},
		{{1, 0, "unsigned", NULL, NULL}}, /* coalesced */
		{{2, 0, "unsigned", NULL, NULL}},
	};
	key_def_check_merge(a_5, lengthof(a_5), b_5, lengthof(b_5),
			    exp_5, lengthof(exp_5));

	/*
	 * Case 6: nullable + non-nullable.
	 *
	 * Interpretation: the same as for the case 3.
	 */
	box_key_part_def_t a_6[] = {
		{{3, 0, "unsigned", NULL, NULL}},
		{{1, 1, "unsigned", NULL, NULL}}, /* clash */
	};
	box_key_part_def_t b_6[] = {
		{{1, 0, "unsigned", NULL, NULL}}, /* clash */
		{{2, 0, "unsigned", NULL, NULL}},
	};
	box_key_part_def_t exp_6[] = {
		{{3, 0, "unsigned", NULL, NULL}},
		{{1, 1, "unsigned", NULL, NULL}}, /* coalesced */
		{{2, 0, "unsigned", NULL, NULL}},
	};
	key_def_check_merge(a_6, lengthof(a_6), b_6, lengthof(b_6),
			    exp_6, lengthof(exp_6));

	/*
	 * Case 7: non-nullable + nullable.
	 *
	 * Interpretation: the same as for the case 3.
	 */
	box_key_part_def_t a_7[] = {
		{{3, 0, "unsigned", NULL, NULL}},
		{{1, 0, "unsigned", NULL, NULL}}, /* clash */
	};
	box_key_part_def_t b_7[] = {
		{{1, 1, "unsigned", NULL, NULL}}, /* clash */
		{{2, 0, "unsigned", NULL, NULL}},
	};
	box_key_part_def_t exp_7[] = {
		{{3, 0, "unsigned", NULL, NULL}},
		{{1, 0, "unsigned", NULL, NULL}}, /* coalesced */
		{{2, 0, "unsigned", NULL, NULL}},
	};
	key_def_check_merge(a_7, lengthof(a_7), b_7, lengthof(b_7),
			    exp_7, lengthof(exp_7));

	/*
	 * Case 8: the same ICU collations.
	 *
	 * Interpretation: when <a> and <b> have key parts that
	 * are point to the same field (considering <fieldno> and
	 * JSON paths), the key part from <b> is omitted, when one
	 * of the following conditions is true:
	 *
	 * 1. <a> and <b> have the same collation (or both lacks
	 *    it).
	 * 2. <a> has no collation.
	 * 3. <a> has a non-ICU collation (those are 'none' and
	 *    'binary' now).
	 * 4. <a> has an ICU collation with UCOL_DEFAULT strength
	 *    (but I don't know what does it mean in practice and
	 *    unable to interpret).
	 *
	 * Comments around <coll_can_merge>() point the general
	 * idea: don't coalesce when <b>'s collation may impose
	 * a strict order on keys equal in terms of the <a>'s
	 * collation. (And I guess 'more strict' was meant by the
	 * word 'strict'.)
	 *
	 * The general rule is to don't coalesce when doubt. But
	 * under the conditions above we're sure that the order
	 * imposed by <a>'s collation is already strict and hence
	 * we don't need <b>'s collation at all.
	 *
	 * Beware! Tarantool-1.10 does not take collations into
	 * account at all when decide whether to coalesce a key
	 * part or not. See gh-3537.
	 *
	 * Aside of this, tarantool-1.10 only have 'unicode' and
	 * 'unicode_ci' collations.
	 */
	box_key_part_def_t a_8[] = {
		{{3, 0, "unsigned", NULL,      NULL}},
		{{1, 0, "string",   "unicode", NULL}}, /* clash */
	};
	box_key_part_def_t b_8[] = {
		{{1, 0, "string",   "unicode", NULL}}, /* clash */
		{{2, 0, "unsigned", NULL,      NULL}},
	};
	box_key_part_def_t exp_8[] = {
		{{3, 0, "unsigned", NULL,      NULL}},
		{{1, 0, "string",   "unicode", NULL}}, /* coalesced */
		{{2, 0, "unsigned", NULL,      NULL}},
	};
	key_def_check_merge(a_8, lengthof(a_8), b_8, lengthof(b_8),
			    exp_8, lengthof(exp_8));

	/*
	 * Case 9: no collation + ICU collation.
	 *
	 * Interpretation: see the case 8.
	 */
	box_key_part_def_t a_9[] = {
		{{3, 0, "unsigned", NULL,      NULL}},
		{{1, 0, "string",   NULL,      NULL}}, /* clash */
	};
	box_key_part_def_t b_9[] = {
		{{1, 0, "string",   "unicode", NULL}}, /* clash */
		{{2, 0, "unsigned", NULL,      NULL}},
	};
	box_key_part_def_t exp_9[] = {
		{{3, 0, "unsigned", NULL,      NULL}},
		{{1, 0, "string",   NULL,      NULL}}, /* coalesced */
		{{2, 0, "unsigned", NULL,      NULL}},
	};
	key_def_check_merge(a_9, lengthof(a_9), b_9, lengthof(b_9),
			    exp_9, lengthof(exp_9));

	/*
	 * Case 10: ICU collation + no collation.
	 *
	 * Interpretation: see the case 8.
	 */
	box_key_part_def_t a_10[] = {
		{{3, 0, "unsigned", NULL,      NULL}},
		{{1, 0, "string",   "unicode", NULL}}, /* clash */
	};
	box_key_part_def_t b_10[] = {
		{{1, 0, "string",   NULL,      NULL}}, /* clash */
		{{2, 0, "unsigned", NULL,      NULL}},
	};
	box_key_part_def_t exp_10[] = {
		{{3, 0, "unsigned", NULL,      NULL}},
		{{1, 0, "string",   "unicode", NULL}}, /* from <a> */
		{{1, 0, "string",   NULL,      NULL}}, /* from <b> */
		{{2, 0, "unsigned", NULL,      NULL}},
	};
	key_def_check_merge(a_10, lengthof(a_10), b_10, lengthof(b_10),
			    exp_10, lengthof(exp_10));

	/*
	 * Case 11: less strong ICU collation + more strong one,
	 * but with the same locale.
	 *
	 * 'Less strong' means 'have smaller strength' here.
	 *
	 * Interpretation: see the case 8.
	 */
	box_key_part_def_t a_11[] = {
		{{3, 0, "unsigned", NULL,         NULL}},
		{{1, 0, "string",   "unicode_ci", NULL}}, /* clash */
	};
	box_key_part_def_t b_11[] = {
		{{1, 0, "string",   "unicode",    NULL}}, /* clash */
		{{2, 0, "unsigned", NULL,         NULL}},
	};
	box_key_part_def_t exp_11[] = {
		{{3, 0, "unsigned", NULL,         NULL}},
		{{1, 0, "string",   "unicode_ci", NULL}}, /* from <a> */
		{{1, 0, "string",   "unicode",    NULL}}, /* from <b> */
		{{2, 0, "unsigned", NULL,         NULL}},
	};
	key_def_check_merge(a_11, lengthof(a_11), b_11, lengthof(b_11),
			    exp_11, lengthof(exp_11));

	/*
	 * Case 12: more strong ICU collation + less strong one,
	 * but with the same locale.
	 *
	 * 'More strong' means 'have bigger strength' here.
	 *
	 * Interpretation: see the case 8.
	 */
	box_key_part_def_t a_12[] = {
		{{3, 0, "unsigned", NULL,         NULL}},
		{{1, 0, "string",   "unicode",    NULL}}, /* clash */
	};
	box_key_part_def_t b_12[] = {
		{{1, 0, "string",   "unicode_ci", NULL}}, /* clash */
		{{2, 0, "unsigned", NULL,         NULL}},
	};
	box_key_part_def_t exp_12[] = {
		{{3, 0, "unsigned", NULL,         NULL}},
		{{1, 0, "string",   "unicode",    NULL}}, /* from <a> */
		{{1, 0, "string",   "unicode_ci", NULL}}, /* from <b> */
		{{2, 0, "unsigned", NULL,         NULL}},
	};
	key_def_check_merge(a_12, lengthof(a_12), b_12, lengthof(b_12),
			    exp_12, lengthof(exp_12));

	/*
	 * Case 13: ICU collations with different locales.
	 *
	 * Interpretation: see the case 8.
	 */
	box_key_part_def_t a_13[] = {
		{{3, 0, "unsigned", NULL,            NULL}},
		{{1, 0, "string",   "unicode_am_s3", NULL}}, /* clash */
	};
	box_key_part_def_t b_13[] = {
		{{1, 0, "string",   "unicode_fi_s3", NULL}}, /* clash */
		{{2, 0, "unsigned", NULL,            NULL}},
	};
	box_key_part_def_t exp_13[] = {
		{{3, 0, "unsigned", NULL,            NULL}},
		{{1, 0, "string",   "unicode_am_s3", NULL}}, /* from <a> */
		{{1, 0, "string",   "unicode_fi_s3", NULL}}, /* from <b> */
		{{2, 0, "unsigned", NULL,            NULL}},
	};
	key_def_check_merge(a_13, lengthof(a_13), b_13, lengthof(b_13),
			    exp_13, lengthof(exp_13));

	/*
	 * Case 14: 'none' collation + ICU collation.
	 *
	 * Interpretation: see the case 8.
	 *
	 * Note: 'none' collation is the same as lack of a
	 * collation from key_def point of view. So after
	 * dump to key parts it becomes NULL.
	 */
	box_key_part_def_t a_14[] = {
		{{3, 0, "unsigned", NULL,      NULL}},
		{{1, 0, "string",   "none",    NULL}}, /* clash */
	};
	box_key_part_def_t b_14[] = {
		{{1, 0, "string",   "unicode", NULL}}, /* clash */
		{{2, 0, "unsigned", NULL,      NULL}},
	};
	box_key_part_def_t exp_14[] = {
		{{3, 0, "unsigned", NULL,      NULL}},
		{{1, 0, "string",   NULL,      NULL}}, /* coalesced */
		{{2, 0, "unsigned", NULL,      NULL}},
	};
	key_def_check_merge(a_14, lengthof(a_14), b_14, lengthof(b_14),
			    exp_14, lengthof(exp_14));

	/*
	 * Case 15: ICU collation + 'none' collation.
	 *
	 * Interpretation: see the case 8.
	 *
	 * Note: 'none' collation is the same as lack of a
	 * collation from key_def point of view. So after
	 * dump to key parts it becomes NULL.
	 */
	box_key_part_def_t a_15[] = {
		{{3, 0, "unsigned", NULL,      NULL}},
		{{1, 0, "string",   "unicode", NULL}}, /* clash */
	};
	box_key_part_def_t b_15[] = {
		{{1, 0, "string",   "none",    NULL}}, /* clash */
		{{2, 0, "unsigned", NULL,      NULL}},
	};
	box_key_part_def_t exp_15[] = {
		{{3, 0, "unsigned", NULL,      NULL}},
		{{1, 0, "string",   "unicode", NULL}}, /* from <a> */
		{{1, 0, "string",   NULL,      NULL}}, /* from <b> */
		{{2, 0, "unsigned", NULL,      NULL}},
	};
	key_def_check_merge(a_15, lengthof(a_15), b_15, lengthof(b_15),
			    exp_15, lengthof(exp_15));

	/*
	 * Case 16: 'binary' collation + ICU collation.
	 *
	 * Interpretation: see the case 8.
	 */
	box_key_part_def_t a_16[] = {
		{{3, 0, "unsigned", NULL,      NULL}},
		{{1, 0, "string",   "binary",  NULL}}, /* clash */
	};
	box_key_part_def_t b_16[] = {
		{{1, 0, "string",   "unicode", NULL}}, /* clash */
		{{2, 0, "unsigned", NULL,      NULL}},
	};
	box_key_part_def_t exp_16[] = {
		{{3, 0, "unsigned", NULL,      NULL}},
		{{1, 0, "string",   "binary",  NULL}}, /* coalesced */
		{{2, 0, "unsigned", NULL,      NULL}},
	};
	key_def_check_merge(a_16, lengthof(a_16), b_16, lengthof(b_16),
			    exp_16, lengthof(exp_16));

	/*
	 * Case 17: ICU collation + 'binary' collation.
	 *
	 * Interpretation: see the case 8.
	 */
	box_key_part_def_t a_17[] = {
		{{3, 0, "unsigned", NULL,      NULL}},
		{{1, 0, "string",   "unicode", NULL}}, /* clash */
	};
	box_key_part_def_t b_17[] = {
		{{1, 0, "string",   "binary",  NULL}}, /* clash */
		{{2, 0, "unsigned", NULL,      NULL}},
	};
	box_key_part_def_t exp_17[] = {
		{{3, 0, "unsigned", NULL,      NULL}},
		{{1, 0, "string",   "unicode", NULL}}, /* from <a> */
		{{1, 0, "string",   "binary",  NULL}}, /* from <b> */
		{{2, 0, "unsigned", NULL,      NULL}},
	};
	key_def_check_merge(a_17, lengthof(a_17), b_17, lengthof(b_17),
			    exp_17, lengthof(exp_17));

	/*
	 * Case 18: the same JSON paths.
	 *
	 * Interpretation: <fieldno> and <path> are considered as
	 * a 'pointer' to a field. JSON path are compared by its
	 * meaning, not just byte-to-byte. See also the case 3.
	 */
	box_key_part_def_t a_18[] = {
		{{0, 0, "unsigned", NULL, "moo"}},
	};
	box_key_part_def_t b_18[] = {
		{{0, 0, "unsigned", NULL, "moo"}},
	};
	box_key_part_def_t exp_18[] = {
		{{0, 0, "unsigned", NULL, "moo"}}, /* coalesced */
	};
	key_def_check_merge(a_18, lengthof(a_18), b_18, lengthof(b_18),
			    exp_18, lengthof(exp_18));

	/*
	 * Case 19: the same JSON paths, but different <fieldno>.
	 *
	 * Interpretation: see the case 18.
	 */
	box_key_part_def_t a_19[] = {
		{{0, 0, "unsigned", NULL, "moo"}},
	};
	box_key_part_def_t b_19[] = {
		{{1, 0, "unsigned", NULL, "moo"}},
	};
	box_key_part_def_t exp_19[] = {
		{{0, 0, "unsigned", NULL, "moo"}},
		{{1, 0, "unsigned", NULL, "moo"}},
	};
	key_def_check_merge(a_19, lengthof(a_19), b_19, lengthof(b_19),
			    exp_19, lengthof(exp_19));

	/*
	 * Case 20: equivalent JSON paths.
	 *
	 * Interpretation: see the case 18. A key part from <b>
	 * is omitted in the case, so the JSON path from <a> is
	 * present in the result.
	 */
	box_key_part_def_t a_20[] = {
		{{0, 0, "unsigned", NULL, ".moo"}},
	};
	box_key_part_def_t b_20[] = {
		{{0, 0, "unsigned", NULL, "moo" }},
	};
	box_key_part_def_t exp_20[] = {
		{{0, 0, "unsigned", NULL, ".moo"}}, /* coalesced */
	};
	key_def_check_merge(a_20, lengthof(a_20), b_20, lengthof(b_20),
			    exp_20, lengthof(exp_20));

	/*
	 * Case 21: no JSON path + JSON path.
	 *
	 * Interpretation: see the case 18.
	 */
	box_key_part_def_t a_21[] = {
		{{0, 0, "unsigned", NULL, NULL }},
	};
	box_key_part_def_t b_21[] = {
		{{0, 0, "unsigned", NULL, "moo"}},
	};
	box_key_part_def_t exp_21[] = {
		{{0, 0, "unsigned", NULL, NULL }},
		{{0, 0, "unsigned", NULL, "moo"}},
	};
	key_def_check_merge(a_21, lengthof(a_21), b_21, lengthof(b_21),
			    exp_21, lengthof(exp_21));

	/*
	 * Case 22: JSON path + no JSON path.
	 *
	 * Interpretation: see the case 18.
	 */
	box_key_part_def_t a_22[] = {
		{{0, 0, "unsigned", NULL, "moo"}},
	};
	box_key_part_def_t b_22[] = {
		{{0, 0, "unsigned", NULL, NULL }},
	};
	box_key_part_def_t exp_22[] = {
		{{0, 0, "unsigned", NULL, "moo"}},
		{{0, 0, "unsigned", NULL, NULL }},
	};
	key_def_check_merge(a_22, lengthof(a_22), b_22, lengthof(b_22),
			    exp_22, lengthof(exp_22));

	/*
	 * Case 23: different JSON paths.
	 *
	 * Interpretation: see the case 18.
	 */
	box_key_part_def_t a_23[] = {
		{{0, 0, "unsigned", NULL, "foo"}},
	};
	box_key_part_def_t b_23[] = {
		{{0, 0, "unsigned", NULL, "bar"}},
	};
	box_key_part_def_t exp_23[] = {
		{{0, 0, "unsigned", NULL, "foo"}},
		{{0, 0, "unsigned", NULL, "bar"}},
	};
	key_def_check_merge(a_23, lengthof(a_23), b_23, lengthof(b_23),
			    exp_23, lengthof(exp_23));

	/*
	 * Case 24: a shorter JSON path + a longer JSON path, but
	 * with the same prefix.
	 *
	 * Interpretation: see the case 18. Those JSON paths are
	 * not equivalent.
	 */
	box_key_part_def_t a_24[] = {
		{{0, 0, "unsigned", NULL, "foo"    }},
	};
	box_key_part_def_t b_24[] = {
		{{0, 0, "unsigned", NULL, "foo.bar"}},
	};
	box_key_part_def_t exp_24[] = {
		{{0, 0, "unsigned", NULL, "foo"    }},
		{{0, 0, "unsigned", NULL, "foo.bar"}},
	};
	key_def_check_merge(a_24, lengthof(a_24), b_24, lengthof(b_24),
			    exp_24, lengthof(exp_24));

	/*
	 * Case 25: a longer JSON path + a shorter JSON path, but
	 * with the same prefix.
	 *
	 * Interpretation: see the case 18. Those JSON paths are
	 * not equivalent.
	 */
	box_key_part_def_t a_25[] = {
		{{0, 0, "unsigned", NULL, "foo.bar"}},
	};
	box_key_part_def_t b_25[] = {
		{{0, 0, "unsigned", NULL, "foo"    }},
	};
	box_key_part_def_t exp_25[] = {
		{{0, 0, "unsigned", NULL, "foo.bar"}},
		{{0, 0, "unsigned", NULL, "foo"    }},
	};
	key_def_check_merge(a_25, lengthof(a_25), b_25, lengthof(b_25),
			    exp_25, lengthof(exp_25));

	/*
	 * Case 26: exclude_null=true + exclude_null=false.
	 *
	 * Interpretation: the same as for the case 3.
	 */
	box_key_part_def_t a_26[] = {
		{{3, 0, "unsigned", NULL, NULL}},
		{{1, 2, "unsigned", NULL, NULL}}, /* clash */
	};
	box_key_part_def_t b_26[] = {
		{{1, 0, "unsigned", NULL, NULL}}, /* clash */
		{{2, 0, "unsigned", NULL, NULL}},
	};
	box_key_part_def_t exp_26[] = {
		{{3, 0, "unsigned", NULL, NULL}},
		{{1, 2, "unsigned", NULL, NULL}}, /* coalesced */
		{{2, 0, "unsigned", NULL, NULL}},
	};
	key_def_check_merge(a_26, lengthof(a_26), b_26, lengthof(b_26),
			    exp_26, lengthof(exp_26));

	/*
	 * Case 27: exclude_null=false + exclude_null=true.
	 *
	 * Interpretation: the same as for the case 3.
	 */
	box_key_part_def_t a_27[] = {
		{{3, 0, "unsigned", NULL, NULL}},
		{{1, 0, "unsigned", NULL, NULL}}, /* clash */
	};
	box_key_part_def_t b_27[] = {
		{{1, 2, "unsigned", NULL, NULL}}, /* clash */
		{{2, 0, "unsigned", NULL, NULL}},
	};
	box_key_part_def_t exp_27[] = {
		{{3, 0, "unsigned", NULL, NULL}},
		{{1, 0, "unsigned", NULL, NULL}}, /* coalesced */
		{{2, 0, "unsigned", NULL, NULL}},
	};
	key_def_check_merge(a_27, lengthof(a_27), b_27, lengthof(b_27),
			    exp_27, lengthof(exp_27));

	/* Clean up. */
	box_region_truncate(region_svp);

	lua_pushboolean(L, 1);
	return 1;
}

/**
 * Basic <box_key_def_extract_key>() test.
 */
static int
test_key_def_extract_key(struct lua_State *L)
{
	size_t region_svp = box_region_used();

	/*
	 * Create a key_def.
	 *
	 *  |              tuple
	 *  |            [x, x, x]
	 *  | key_def     ^     ^
	 *  |    |        |     |
	 *  |   (0) <-----+---- string (optional)
	 *  |    |        |
	 *  |   (1) <---- unsigned
	 */
	box_key_part_def_t parts[2];
	box_key_part_def_create(&parts[0]);
	box_key_part_def_create(&parts[1]);
	parts[0].fieldno = 2;
	parts[0].field_type = "string";
	parts[0].flags |= BOX_KEY_PART_DEF_IS_NULLABLE;
	parts[1].fieldno = 0;
	parts[1].field_type = "unsigned";
	box_key_def_t *key_def = box_key_def_new_v2(parts, 2);
	assert(key_def != NULL);

	/*
	 * Create tuples to extract keys from them.
	 *
	 *  | # | tuple         | key        |
	 *  | - | ------------- | ---------- |
	 *  | 0 | [1, 2, "moo"] | ["moo", 1] |
	 *  | 1 | [1, 2, null]  | [null, 1]  |
	 *  | 2 | [1, 2]        | [null, 1]  |
	 *  | 3 | [1]           | [null, 1]  |
	 */
	box_tuple_t *tuples[] = {
		/* [0] = */ new_runtime_tuple("\x93\x01\x02\xa3moo", 7),
		/* [1] = */ new_runtime_tuple("\x93\x01\x02\xc0", 4),
		/* [2] = */ new_runtime_tuple("\x92\x01\x02", 3),
		/* [3] = */ new_runtime_tuple("\x91\x01", 2),
	};
	struct {
		const char *key;
		uint32_t key_size;
	} expected_keys_1[] = {
		/* [0] = */ {"\x92\xa3moo\x01", 6},
		/* [1] = */ {"\x92\xc0\x01", 3},
		/* [2] = */ {"\x92\xc0\x01", 3},
		/* [3] = */ {"\x92\xc0\x01", 3},
	};

	for (size_t i = 0; i < lengthof(tuples); ++i) {
		uint32_t key_size = 0;
		char *key = box_key_def_extract_key(key_def, tuples[i], -1,
						    &key_size);
		assert(key != NULL);
		uint32_t exp_key_size = expected_keys_1[i].key_size;
		const char *exp_key = expected_keys_1[i].key;
		assert(key_size == exp_key_size);
		assert(memcmp(key, exp_key, exp_key_size) == 0);
		(void)exp_key_size;
		(void)exp_key;
		(void)key_size;
		(void)key;
	}

	/* Clean up. */
	for (size_t i = 0; i < lengthof(tuples); ++i)
		box_tuple_unref(tuples[i]);
	box_key_def_delete(key_def);

	/*
	 * Create a key_def with multikey JSON path.
	 *
	 *  |             tuple
	 *  |           [[x, x, x], x, x]
	 *  | key_def     ^  ^  ^
	 *  |    |        0  1  2
	 *  |    |        |  |  |
	 *  |    |        |--+--+
	 *  |    |        |
	 *  |   (0) <---- unsigned
	 */
	box_key_part_def_t part;
	box_key_part_def_create(&part);
	part.fieldno = 0;
	part.field_type = "unsigned";
	part.path = "[*]";
	key_def = box_key_def_new_v2(&part, 1);
	assert(key_def != NULL);

	/* [[7, 2, 1], 5, 4] */
	box_tuple_t *tuple =
		new_runtime_tuple("\x93\x93\x07\x02\x01\x05\x04", 7);

	struct {
		const char *key;
		uint32_t key_size;
	} expected_keys_2[] = {
		/* [0] = */ {"\x91\x07", 2},
		/* [1] = */ {"\x91\x02", 2},
		/* [2] = */ {"\x91\x01", 2},
	};

	for (int i = 0; i < (int)lengthof(expected_keys_2); ++i) {
		uint32_t key_size = 0;
		char *key = box_key_def_extract_key(key_def, tuple, i,
						    &key_size);
		assert(key != NULL);
		uint32_t exp_key_size = expected_keys_2[i].key_size;
		const char *exp_key = expected_keys_2[i].key;
		assert(key_size == exp_key_size);
		assert(memcmp(key, exp_key, exp_key_size) == 0);
		(void)exp_key_size;
		(void)exp_key;
		(void)key_size;
		(void)key;
	}

	/* Clean up. */
	box_tuple_unref(tuple);
	box_key_def_delete(key_def);
	box_region_truncate(region_svp);

	lua_pushboolean(L, 1);
	return 1;
}

/**
 * Basic <box_key_def_validate_key>() and
 * <box_key_def_validate_full_key>() test.
 */
static int
test_key_def_validate_key(struct lua_State *L)
{
	/*
	 * Create a key_def.
	 *
	 *  |              tuple
	 *  |            [x, x, x]
	 *  | key_def     ^     ^
	 *  |    |        |     |
	 *  |   (0) <-----+---- unsigned
	 *  |    |        |
	 *  |   (1) <---- unsigned (optional)
	 */
	box_key_part_def_t parts[2];
	box_key_part_def_create(&parts[0]);
	box_key_part_def_create(&parts[1]);
	parts[0].fieldno = 2;
	parts[0].field_type = "unsigned";
	parts[1].fieldno = 0;
	parts[1].field_type = "unsigned";
	parts[1].flags |= BOX_KEY_PART_DEF_IS_NULLABLE;
	box_key_def_t *key_def = box_key_def_new_v2(parts, 2);
	assert(key_def != NULL);

	/*
	 * Create keys to validate them against given key_def.
	 *
	 *  | # | key            | Is valid? | Is valid? |
	 *  |   |                | (partial) |   (full)  |
	 *  | - | -------------- | --------- | --------- |
	 *  | 0 | [1, 1]         | valid     | valid     |
	 *  | 1 | [1, null]      | valid     | valid     |
	 *  | 2 | [1]            | valid     | invalid   |
	 *  | 3 | []             | valid     | invalid   |
	 *  | 4 | [null]         | invalid   | invalid   |
	 *  | 5 | [1, 2, 3]      | invalid   | invalid   |
	 *  | 6 | [1, -1]        | invalid   | invalid   |
	 */
	struct {
		const char *data;
		uint32_t size;
	} keys[] = {
		/* [0] = */ {"\x92\x01\x01",     3},
		/* [1] = */ {"\x92\x01\xc0",     3},
		/* [2] = */ {"\x91\x01",         2},
		/* [3] = */ {"\x90",             1},
		/* [4] = */ {"\x91\xc0",         2},
		/* [5] = */ {"\x93\x01\x02\x03", 4},
		/* [6] = */ {"\x92\x01\xff",     3},
	};
	int expected_results[][2] = {
		/* [0] = */ {0,  0 },
		/* [1] = */ {0,  0 },
		/* [2] = */ {0,  -1},
		/* [3] = */ {0,  -1},
		/* [4] = */ {-1, -1},
		/* [5] = */ {-1, -1},
		/* [6] = */ {-1, -1},
	};
	uint32_t expected_error_codes[][2] = {
		/* [0] = */ {box_error_code_MAX, box_error_code_MAX},
		/* [1] = */ {box_error_code_MAX, box_error_code_MAX},
		/* [2] = */ {box_error_code_MAX, ER_EXACT_MATCH    },
		/* [3] = */ {box_error_code_MAX, ER_EXACT_MATCH    },
		/* [4] = */ {ER_KEY_PART_TYPE,   ER_EXACT_MATCH    },
		/* [5] = */ {ER_KEY_PART_COUNT,  ER_EXACT_MATCH    },
		/* [6] = */ {ER_KEY_PART_TYPE,   ER_KEY_PART_TYPE  },
	};

	typedef int (*key_def_validate_key_f)(const box_key_def_t *key_def,
					      const char *key,
					      uint32_t *key_size_ptr);
	key_def_validate_key_f funcs[] = {
		box_key_def_validate_key,
		box_key_def_validate_full_key,
	};

	for (size_t i = 0; i < lengthof(keys); ++i) {
		for (size_t f = 0; f < lengthof(funcs); ++f) {
			int exp_res = expected_results[i][f];
			uint32_t exp_err_code = expected_error_codes[i][f];
			const char *key = keys[i].data;
			uint32_t key_size = 0;
			int rc = funcs[f](key_def, key, &key_size);
			assert(rc == exp_res);
			(void)rc;
			(void)exp_res;

			if (exp_err_code == box_error_code_MAX) {
				/* Verify key_size. */
				assert(key_size != 0);
				assert(key_size == keys[i].size);

				/*
				 * Verify that no NULL pointer
				 * dereference occurs when NULL
				 * is passed as key_size_ptr.
				 */
				box_key_def_validate_key(key_def, key, NULL);
			} else {
				assert(rc != 0);
				box_error_t *e = box_error_last();
				(void)e;
				assert(box_error_code(e) == exp_err_code);
			}
		}
	}

	/* Clean up. */
	box_key_def_delete(key_def);

	lua_pushboolean(L, 1);
	return 1;
}

static int
test_key_def_dup(lua_State *L)
{
	box_key_def_t *key_def = NULL;
	box_key_part_def_t part;
	box_key_part_def_t *dump = NULL;
	uint32_t dump_part_count = 0;

	key_part_def_set_nondefault(&part);
	key_def = box_key_def_new_v2(&part, 1);
	assert(key_def != NULL);
	box_key_def_t *key_def_dup = box_key_def_dup(key_def);
	assert(key_def_dup != NULL);

	dump = box_key_def_dump_parts(key_def_dup, &dump_part_count);
	assert(dump != NULL);
	assert(dump_part_count == 1);

	key_part_def_check_equal(&part, &dump[0]);
	key_part_def_check_zeros(&dump[0]);

	box_key_def_delete(key_def_dup);
	box_key_def_delete(key_def);

	lua_pushboolean(L, 1);
	return 1;
}

/* }}} key_def api v2 */

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
test_iscallable(lua_State *L)
{
	int exp = lua_toboolean(L, 2);
	int res = luaL_iscallable(L, 1);
	lua_pushboolean(L, res == exp);
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

/*
 * Check that argument is a tuple of any format, without
 * its verification
 */
static int
test_tuple_validate_default(lua_State *L)
{
	int valid = 0;
	box_tuple_t *tuple = luaT_istuple(L, -1);

	if (tuple != NULL) {
		box_tuple_format_t *format = box_tuple_format_default();
		valid = box_tuple_validate(tuple, format) == 0;
	}
	lua_pushboolean(L, valid);

	return 1;
}

/*
 * Validate tuple with format of single boolean field
 */
static int
test_tuple_validate_formatted(lua_State *L)
{
	int valid = 0;
	box_tuple_t *tuple = luaT_istuple(L, -1);

	if (tuple != NULL) {
		uint32_t fields[] = { 0 };
		uint32_t types[] = { FIELD_TYPE_BOOLEAN };
		box_key_def_t *key_defs[] = {
			box_key_def_new(fields, types, 1)
		};
		assert(key_defs[0] != NULL);
		struct tuple_format *format =
			box_tuple_format_new(key_defs, 1);
		assert(format);

		valid = box_tuple_validate(tuple, format) == 0;
		box_tuple_format_unref(format);
		box_key_def_delete(key_defs[0]);
	}
	lua_pushboolean(L, valid);

	return 1;
}

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
		{"toibuf", test_toibuf},
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
		{"iscallable", test_iscallable},
		{"iscdata", test_iscdata},
		{"test_box_region", test_box_region},
		{"test_tuple_encode", test_tuple_encode},
		{"test_tuple_new", test_tuple_new},
		{"test_key_def_new_v2", test_key_def_new_v2},
		{"test_key_def_dump_parts", test_key_def_dump_parts},
		{"test_key_def_validate_tuple", test_key_def_validate_tuple},
		{"test_key_def_merge", test_key_def_merge},
		{"test_key_def_extract_key", test_key_def_extract_key},
		{"test_key_def_validate_key", test_key_def_validate_key},
		{"test_box_ibuf", test_box_ibuf},
		{"tuple_validate_def", test_tuple_validate_default},
		{"tuple_validate_fmt", test_tuple_validate_formatted},
		{"test_key_def_dup", test_key_def_dup},
		{NULL, NULL}
	};
	luaL_register(L, "module_api", lib);
	return 1;
}
