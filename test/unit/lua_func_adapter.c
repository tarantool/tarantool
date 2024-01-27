#include "diag.h"
#include "fiber.h"
#include "memory.h"
#include "msgpuck.h"
#include "lua/init.h"
#include "lua/error.h"
#include "lua/utils.h"
#include "lua/msgpack.h"
#include "box/port.h"
#include "box/tuple.h"
#include "box/lua/func_adapter.h"
#include "box/lua/misc.h"
#include "box/lua/tuple.h"
#include "core/func_adapter.h"
#include "core/mp_ctx.h"
#include "core/assoc.h"

#define UNIT_TAP_COMPATIBLE 1
#include "unit.h"
#include "lua_test_utils.h"

#define EPS 0.0001

/**
 * Check if two floating point numbers are equal.
 */
static bool
number_eq(double a, double b)
{
	return fabs(a - b) < EPS;
}

#undef EPS

/**
 * Generates Lua function from string and returns its index in tarantool_L.
 */
static int
generate_function(const char *function)
{
	int rc = luaT_dostring(tarantool_L, tt_sprintf("return %s", function));
	fail_if(rc != 0);
	return lua_gettop(tarantool_L);
}

static void
test_numeric(void)
{
	plan(5);
	header();

	int idx = generate_function(
		"function(a, b, c, d) "
		"return a * b * c * d, a + b + c + d end");
	const double expected[] = { 3 * 5 * 7 * 11, 3 + 5 + 7 + 11};
	struct func_adapter *func = func_adapter_lua_create(tarantool_L, idx);
	uint32_t region_svp = region_used(&fiber()->gc);
	struct port args, ret;
	port_c_create(&args);
	port_c_add_number(&args, 3);
	port_c_add_number(&args, 5);
	port_c_add_number(&args, 7);
	port_c_add_number(&args, 11);
	int rc = func_adapter_call(func, &args, &ret);
	fail_if(rc != 0);

	int i = 0;
	const struct port_c_entry *retval = port_get_c_entries(&ret);
	for (; retval != NULL; retval = retval->next) {
		ok(retval->type == PORT_C_ENTRY_NUMBER, "Expected double");
		double val = retval->number;
		ok(number_eq(expected[i], val),
		   "Returned value must be as expected");
		i++;
	}
	is(i, lengthof(expected), "All values must be returned");
	port_destroy(&args);
	port_destroy(&ret);
	func_adapter_destroy(func);
	region_truncate(&fiber()->gc, region_svp);
	lua_settop(tarantool_L, 0);

	footer();
	check_plan();
}

static void
test_tuple(void)
{
	plan(13);
	header();

	int idx = generate_function(
		"function(a, b, tuple) "
		"return box.internal.tuple.new{a, b}, tuple, "
		"box.internal.tuple.new{b, a}, "
		"box.internal.tuple.new{a + b, a - b} end");
	struct func_adapter *func = func_adapter_lua_create(tarantool_L, idx);
	uint32_t region_svp = region_used(&fiber()->gc);
	struct port args, ret;
	port_c_create(&args);
	port_c_add_number(&args, 42);
	port_c_add_number(&args, 43);
	static const char *tuple_data = "\x92\x06\x03";
	struct tuple *tuple = tuple_new(tuple_format_runtime, tuple_data,
					tuple_data + strlen(tuple_data));
	port_c_add_tuple(&args, tuple);
	int rc = func_adapter_call(func, &args, &ret);
	fail_if(rc != 0);

	const char *expected_tuples[] = {
		"[42, 43]", "[6, 3]", "[43, 42]", "[85, -1]"};
	struct tuple *tuples[4];
	int i = 0;
	const struct port_c_entry *retval = port_get_c_entries(&ret);
	for (; retval != NULL; retval = retval->next) {
		ok(retval->type == PORT_C_ENTRY_TUPLE, "Expected tuple");
		tuples[i] = retval->tuple;
		isnt(tuples[i], NULL, "Returned tuple must not be NULL");
		const char *str = tuple_str(tuples[i]);
		is(strcmp(expected_tuples[i], str), 0, "Expected %s, got %s",
		   expected_tuples[i], str);
		i++;
	}
	is(i, lengthof(tuples), "All values must be returned");
	port_destroy(&args);
	port_destroy(&ret);
	func_adapter_destroy(func);
	region_truncate(&fiber()->gc, region_svp);
	lua_settop(tarantool_L, 0);

	footer();
	check_plan();
}

static void
test_string(void)
{
	plan(7);
	header();

	int idx = generate_function(
		"function(s1, s2) "
		"return s1, s1 .. s2 end");
	struct func_adapter *func = func_adapter_lua_create(tarantool_L, idx);
	uint32_t region_svp = region_used(&fiber()->gc);
	struct port args, ret;
	port_c_create(&args);
	/* Not zero-terminated string. */
	const char s1[] = {'a', 'b', 'c'};
	size_t s1_len = lengthof(s1);
	const char *s2 = "42strstr";
	size_t s2_len = strlen(s2);
	port_c_add_str(&args, s1, s1_len);
	port_c_add_str0(&args, s2);
	int rc = func_adapter_call(func, &args, &ret);
	fail_if(rc != 0);

	const struct port_c_entry *retval = port_get_c_entries(&ret);
	fail_if(retval == NULL);
	is(retval->type, PORT_C_ENTRY_STR, "Expected string");
	const char *ret_str = retval->str.data;
	size_t len = retval->str.size;
	is(len, s1_len, "Length of popped string must match");
	is(strncmp(ret_str, s1, s1_len), 0, "Popped string must match");

	retval = retval->next;
	fail_if(retval == NULL);
	is(retval->type, PORT_C_ENTRY_STR, "Expected string");
	ret_str = retval->str.data;
	len = retval->str.size;
	is(len, s1_len + s2_len, "Len does not match");
	char *buf = tt_static_buf();
	strncpy(buf, s1, s1_len);
	strcpy(buf + s1_len, s2);
	is(strcmp(ret_str, buf), 0, "Expected %s", buf);

	retval = retval->next;
	is(retval, NULL, "No redundant values");

	port_destroy(&args);
	port_destroy(&ret);
	func_adapter_destroy(func);
	region_truncate(&fiber()->gc, region_svp);
	lua_settop(tarantool_L, 0);

	footer();
	check_plan();
}

static void
test_bool(void)
{
	plan(9);
	header();

	int idx = generate_function(
		"function(a, b, c, d) "
		"return a, not b, c, not d end");
	bool arguments[4];
	for (size_t i = 0; i < lengthof(arguments); ++i)
		arguments[i] = rand() % 2 == 0;
	struct func_adapter *func = func_adapter_lua_create(tarantool_L, idx);
	uint32_t region_svp = region_used(&fiber()->gc);
	struct port args, ret;
	port_c_create(&args);
	for (size_t i = 0; i < lengthof(arguments); ++i)
		port_c_add_bool(&args, arguments[i]);
	int rc = func_adapter_call(func, &args, &ret);
	fail_if(rc != 0);

	const struct port_c_entry *retval = port_get_c_entries(&ret);
	for (size_t i = 0; i < lengthof(arguments); ++i) {
		fail_if(retval == NULL);
		ok(retval->type == PORT_C_ENTRY_BOOL, "Expected double");
		bool is_odd = i % 2 == 0;
		bool equal = arguments[i] == retval->boolean;
		is(is_odd, equal, "Only odd elements are equal");
		retval = retval->next;
	}

	is(retval, NULL, "No values left");
	port_destroy(&args);
	port_destroy(&ret);
	func_adapter_destroy(func);
	region_truncate(&fiber()->gc, region_svp);
	lua_settop(tarantool_L, 0);

	footer();
	check_plan();
}

static void
test_null(void)
{
	plan(7);
	header();

	int idx = generate_function(
		"function(a, b, c) return a, box.NULL, nil, c, b end");
	const size_t null_count = 4;
	const double double_val = 42;
	struct func_adapter *func = func_adapter_lua_create(tarantool_L, idx);
	uint32_t region_svp = region_used(&fiber()->gc);
	struct port args, ret;
	port_c_create(&args);
	port_c_add_null(&args);
	port_c_add_number(&args, double_val);
	int rc = func_adapter_call(func, &args, &ret);
	fail_if(rc != 0);

	const struct port_c_entry *retval = port_get_c_entries(&ret);
	for (size_t i = 0; i < null_count; ++i) {
		fail_if(retval == NULL);
		is(retval->type, PORT_C_ENTRY_NULL, "Expected null");
		retval = retval->next;
	}
	is(retval->type, PORT_C_ENTRY_NUMBER, "Expected double");
	is(retval->number, double_val, "Value must match");
	is(retval->next, NULL, "No redundant values");
	port_destroy(&args);
	port_destroy(&ret);
	func_adapter_destroy(func);
	region_truncate(&fiber()->gc, region_svp);
	lua_settop(tarantool_L, 0);

	footer();
	check_plan();
}

static void
test_mp_object(void)
{
	plan(7);
	header();

	#define MP_BUF_LEN 64
	char mp_buf[MP_BUF_LEN];
	char *mp = mp_buf;
	mp = mp_encode_map(mp, 2);
	mp = mp_encode_str0(mp, "key");
	mp = mp_encode_str0(mp, "value");
	mp = mp_encode_uint(mp, 42);
	mp = mp_encode_uint(mp, 64);
	fail_unless(mp < mp_buf + MP_BUF_LEN);
	#undef MP_BUF_LEN

	int idx = generate_function(
		"function(a) "
		"  local mp = require('msgpack') "
		"  assert(mp.is_object(a)) "
		"  return a.key, a[42] "
		"end");

	struct func_adapter *func = func_adapter_lua_create(tarantool_L, idx);
	uint32_t region_svp = region_used(&fiber()->gc);
	struct port args, ret;
	port_c_create(&args);
	port_c_add_mp_object(&args, mp_buf, mp, NULL);
	int rc = func_adapter_call(func, &args, &ret);
	is(rc, 0, "Function must return successfully");

	const struct port_c_entry *retval = port_get_c_entries(&ret);
	fail_if(retval == NULL);
	is(retval->type, PORT_C_ENTRY_STR, "A string must be returned");
	const char *str = retval->str.data;
	size_t str_len = retval->str.size;
	is(str_len, strlen("value"), "Returned value must be as expected");
	is(strncmp(str, "value", str_len), 0,
	   "Returned value must be as expected");

	retval = retval->next;
	fail_if(retval == NULL);
	is(retval->type, PORT_C_ENTRY_NUMBER, "A double must be returned");
	ok(number_eq(64, retval->number),
	   "Returned value must be as expected");
	is(retval->next, NULL, "No values left");
	port_destroy(&args);
	port_destroy(&ret);
	func_adapter_destroy(func);
	region_truncate(&fiber()->gc, region_svp);

	footer();
	check_plan();
}

static void
test_error(void)
{
	plan(2);
	header();

	const char *functions[] = {
		"function() error('lua error') end",
		"function() box.error('tnt error') end"
	};

	for (size_t i = 0; i < lengthof(functions); ++i) {
		int idx = generate_function(functions[i]);
		struct func_adapter *func = func_adapter_lua_create(tarantool_L,
								    idx);
		int rc = func_adapter_call(func, NULL, NULL);
		is(rc, -1, "Call must fail");
		func_adapter_destroy(func);
		lua_settop(tarantool_L, 0);
	}

	footer();
	check_plan();
}

static void
test_get_func(void)
{
	plan(1);
	header();

	struct lua_State *L = tarantool_L;
	int idx = generate_function("function(a) return a end");
	struct func_adapter *func = func_adapter_lua_create(L, idx);

	func_adapter_lua_get_func(func, L);
	is(lua_equal(L, -1, idx), 1, "Actual function must be returned");

	func_adapter_destroy(func);

	lua_settop(L, 0);

	footer();
	check_plan();
}

static void
test_callable(void)
{
	plan(4);
	header();

	const int table_value = 42;
	const int argument = 19;
	struct lua_State *L = tarantool_L;
	lua_createtable(L, 1, 0);
	lua_pushinteger(L, table_value);
	lua_rawseti(L, -2, 1);
	lua_createtable(L, 0, 1);
	generate_function("function(self, a) return self[1] - a end");
	lua_setfield(L, -2, "__call");
	lua_setmetatable(L, -2);
	int idx = lua_gettop(L);

	struct func_adapter *func = func_adapter_lua_create(L, idx);
	uint32_t region_svp = region_used(&fiber()->gc);
	struct port args, ret;
	port_c_create(&args);
	port_c_add_number(&args, argument);
	int rc = func_adapter_call(func, &args, &ret);
	ok(rc == 0, "Callable table must be called successfully");

	const struct port_c_entry *retval = port_get_c_entries(&ret);
	fail_if(retval == NULL);
	is(retval->type, PORT_C_ENTRY_NUMBER, "Expected double");
	ok(number_eq(retval->number, table_value - argument),
	   "Returned value must be as expected");
	port_destroy(&args);
	port_destroy(&ret);
	func_adapter_lua_get_func(func, L);
	is(lua_equal(L, -1, idx), 1, "Actual table must be returned");
	func_adapter_destroy(func);
	region_truncate(&fiber()->gc, region_svp);
	lua_settop(L, 0);

	footer();
	check_plan();
}

/**
 * Iterator for the test.
 */
struct test_iterator {
	/**
	 * Iterator next.
	 */
	port_c_iterator_next_f next;
	/**
	 * Current value, is incremented after every yield.
	 */
	double current;
	/**
	 * Maximum value, after which iterator yields nothing.
	 */
	double limit;
};

/**
 * Yields 3 sequentially growing values, stops when the iterator is exhausted.
 */
static int
test_iterator_next(struct port_c_iterator *it, struct port *out, bool *is_eof)
{
	struct test_iterator *test_it = (struct test_iterator *)it;
	if (test_it->current > test_it->limit) {
		*is_eof = true;
		return 0;
	}
	*is_eof = false;
	port_c_create(out);
	for (int i = 0; i < 3; ++i) {
		if (test_it->current > test_it->limit)
			break;
		port_c_add_number(out, test_it->current++);
	}
	return 0;
}

/** The data is actually the iterator. */
static void
test_iterator_create(void *data, struct port_c_iterator *it)
{
	struct test_iterator *test_it = (struct test_iterator *)it;
	struct test_iterator *src_it = (struct test_iterator *)data;
	*test_it = *src_it;
}

static void
test_iterator(void)
{
	plan(3 * 2 + 1);
	header();

	struct test_iterator it = {
		.next = test_iterator_next, .current = 1.0, .limit = 20.0,
	};

	int idx = generate_function(
		"function(iter) "
		"  local res1 = 0 "
		"  local res2 = 0 "
		"  local res3 = 0 "
		"  for v1, v2, v3 in iter() do "
		"    if v1 ~= nil then res1 = res1 + v1 end"
		"    if v2 ~= nil then res2 = res2 + v2 end"
		"    if v3 ~= nil then res3 = res3 + v3 end"
		"  end "
		"  return res1, res2, res3 "
		"end");

	double results[3] = {0.0, 0.0, 0.0};
	int i = 0;
	for (int v = it.current; v <= it.limit; ++v, i = (i + 1) % 3)
		results[i] += v;

	struct func_adapter *func = func_adapter_lua_create(tarantool_L, idx);
	uint32_t region_svp = region_used(&fiber()->gc);
	struct port args, ret;
	port_c_create(&args);
	port_c_add_iterable(&args, &it, test_iterator_create);
	int rc = func_adapter_call(func, &args, &ret);
	fail_if(rc != 0);

	const struct port_c_entry *retval = port_get_c_entries(&ret);
	for (size_t i = 0; i < 3; ++i) {
		fail_if(retval == NULL);
		is(retval->type, PORT_C_ENTRY_NUMBER, "Expected double");
		ok(number_eq(retval->number, results[i]),
		   "Function result must match expected one");
		retval = retval->next;
	}
	is(retval, NULL, "No values left");
	port_destroy(&args);
	port_destroy(&ret);
	func_adapter_destroy(func);
	region_truncate(&fiber()->gc, region_svp);

	footer();
	check_plan();
}

/**
 * Message of error returned by iterator next.
 */
static const char *iterator_next_errmsg = "My error in iterator next";

/**
 * Iterator next that returns an error.
 */
static int
test_iterator_next_error(struct port_c_iterator *it, struct port *out,
			 bool *is_eof)
{
	diag_set(ClientError, ER_PROC_C, iterator_next_errmsg);
	return -1;
}

/**
 * Check if errors in iterator_next are handled correctly.
 */
static void
test_iterator_error(void)
{
	plan(2);
	header();

	struct test_iterator it = {
		.next = test_iterator_next_error, .current = 0, .limit = 10,
	};

	int idx = generate_function(
		"function(iter) "
		"  local res = 0 "
		"  for i in iter() do res = res + i end "
		"  return res "
		"end");

	struct func_adapter *func = func_adapter_lua_create(tarantool_L, idx);
	struct port args;
	port_c_create(&args);
	port_c_add_iterable(&args, &it, test_iterator_create);
	int rc = func_adapter_call(func, &args, NULL);
	fail_unless(rc != 0);
	struct error *e = diag_last_error(diag_get());
	is(e->cause, NULL, "Thrown error has no cause");
	is(strcmp(e->errmsg, iterator_next_errmsg), 0,
	   "Expected errmsg: %s, got: %s", iterator_next_errmsg, e->errmsg);
	port_destroy(&args);
	func_adapter_destroy(func);

	footer();
	check_plan();
}

static void
test_translation(void)
{
	plan(5);
	header();

	const uint32_t keys[] = {21, 42};
	const char *names[] = {"foo", "bar"};
	struct mh_strnu32_t *mp_key_translation = mh_strnu32_new();

	for (size_t i = 0; i < nelem(keys); i++) {
		size_t len = strlen(names[i]);
		struct mh_strnu32_node_t translation = {
			.str = names[i],
			.len = len,
			.hash = lua_hash(names[i], len),
			.val = keys[i]
		};
		mh_strnu32_put(mp_key_translation, &translation, NULL, NULL);
	}

	struct mp_ctx mp_ctx;
	mp_ctx_create_default(&mp_ctx, mp_key_translation);

	#define MP_BUF_LEN 64
	char mp_buf[MP_BUF_LEN];
	char *mp = mp_buf;
	mp = mp_encode_map(mp, 2);
	mp = mp_encode_uint(mp, keys[1]);
	mp = mp_encode_uint(mp, 64);
	mp = mp_encode_uint(mp, keys[0]);
	mp = mp_encode_uint(mp, 32);
	fail_unless(mp < mp_buf + MP_BUF_LEN);
	#undef MP_BUF_LEN

	int idx = generate_function(
		"function(a) "
		"  local mp = require('msgpack') "
		"  assert(mp.is_object(a)) "
		"  return a.foo, a.bar "
		"end");

	struct func_adapter *func = func_adapter_lua_create(tarantool_L, idx);
	struct port args, ret;
	uint32_t region_svp = region_used(&fiber()->gc);
	port_c_create(&args);
	port_c_add_mp_object(&args, mp_buf, mp, &mp_ctx);
	int rc = func_adapter_call(func, &args, &ret);
	is(rc, 0, "Function must return successfully");

	const struct port_c_entry *retval = port_get_c_entries(&ret);
	fail_if(retval == NULL);
	is(retval->type, PORT_C_ENTRY_NUMBER, "A double must be returned");
	double val = retval->number;

	retval = retval->next;
	fail_if(retval == NULL);
	is(retval->type, PORT_C_ENTRY_NUMBER, "A double must be returned");
	val = retval->number;
	ok(number_eq(64, val), "Returned value must be as expected");

	is(retval->next, NULL, "No values left");
	port_destroy(&args);
	port_destroy(&ret);
	mh_strnu32_delete(mp_key_translation);
	mp_ctx_destroy(&mp_ctx);
	region_truncate(&fiber()->gc, region_svp);

	footer();
	check_plan();
}

static int
test_lua_func_adapter(void)
{
	plan(12);
	header();

	test_numeric();
	test_tuple();
	test_string();
	test_bool();
	test_null();
	test_mp_object();
	test_error();
	test_get_func();
	test_callable();
	test_iterator();
	test_iterator_error();
	test_translation();

	footer();
	return check_plan();
}

int
main(void)
{
	memory_init();
	fiber_init(fiber_c_invoke);
	tuple_init(NULL);
	port_init();

	lua_State *L = luaT_newteststate();
	tarantool_L = L;

	tarantool_lua_error_init(L);
	tarantool_lua_error_init(L);
	tarantool_lua_utils_init(L);
	luaopen_msgpack(L);
	box_lua_tuple_init(L);
	box_lua_misc_init(L);
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

	int rc = test_lua_func_adapter();

	lua_close(L);
	tarantool_L = NULL;
	port_free();
	tuple_free();
	fiber_free();
	memory_free();
	return rc;
}
