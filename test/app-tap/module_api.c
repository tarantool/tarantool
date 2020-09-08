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
	return BOX_KEY_PART_DEF_IS_NULLABLE;
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
		{"iscallable", test_iscallable},
		{"iscdata", test_iscdata},
		{"test_box_region", test_box_region},
		{"test_tuple_encode", test_tuple_encode},
		{"test_tuple_new", test_tuple_new},
		{"test_key_def_new_v2", test_key_def_new_v2},
		{"test_key_def_dump_parts", test_key_def_dump_parts},
		{"test_key_def_validate_tuple", test_key_def_validate_tuple},
		{NULL, NULL}
	};
	luaL_register(L, "module_api", lib);
	return 1;
}
