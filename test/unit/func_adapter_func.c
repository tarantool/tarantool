#include "diag.h"
#include "fiber.h"
#include "memory.h"
#include "box/func.h"
#include "box/func_cache.h"
#include "box/lua/call.h"
#include "box/lua/misc.h"
#include "box/port.h"
#include "core/func_adapter.h"
#include "lua/init.h"
#include "lua/error.h"
#include "lua/msgpack.h"
#include "lua/serializer.h"
#include "lua/utils.h"
#include "trivia/util.h"

#define UNIT_TAP_COMPATIBLE 1
#include "unit.h"
#include "lua_test_utils.h"

static struct func *
test_func_new(uint32_t id, const char *name, const char *body)
{
	uint32_t name_len = strlen(name);
	uint32_t body_len = strlen(body);
	struct func_def *def = func_def_new(id, ADMIN, name, name_len,
					    FUNC_LANGUAGE_LUA,
					    body, body_len, NULL, 0, NULL);
	struct func *func = func_new(def);
	func_def_delete(def);
	func_cache_insert(func);
	return func;
}

static void
test_func_delete(struct func *func)
{
	func_cache_delete(func->def->fid);
	func_delete(func);
}

static void
test_func_adapter_func_basic(void)
{
	plan(18);
	header();

	const char *body = "function(a, b, c, d) return d, c, b, a end";
	struct func *func = test_func_new(42, "test_func", body);
	fail_if(func == NULL);

	struct func_adapter *func_adapter =
		func_adapter_func_create(func, FUNC_HOLDER_TRIGGER);
	fail_if(func_adapter == NULL);

	/* Port with arguments that will be used throughout the test. */
	struct port args;
	port_c_create(&args);
	port_c_add_number(&args, 4);
	port_c_add_number(&args, 3);
	port_c_add_number(&args, 2);
	port_c_add_number(&args, 1);

	/* Region should be cleared: port_get_c_entries is used in the test. */
	size_t region_svp = region_used(&fiber()->gc);

	int rc = func_adapter_call(func_adapter, NULL, NULL);
	is(rc, 0, "Call func_adapter without both ports");

	rc = func_adapter_call(func_adapter, &args, NULL);
	is(rc, 0, "Call func_adapter without ret");

	struct port ret;
	rc = func_adapter_call(func_adapter, NULL, &ret);
	is(rc, 0, "Call func_adapter without args");
	const struct port_c_entry *pe;
	int count = 0;
	for (pe = port_get_c_entries(&ret); pe != NULL; pe = pe->next) {
		count++;
		is(pe->type, PORT_C_ENTRY_NULL, "Null is expected as retval");
	}
	is(count, 4, "Expected 4 values");
	port_destroy(&ret);

	rc = func_adapter_call(func_adapter, &args, &ret);
	is(rc, 0, "Call func_adapter with args and ret");
	count = 0;
	for (pe = port_get_c_entries(&ret); pe != NULL; pe = pe->next) {
		count++;
		is(pe->type, PORT_C_ENTRY_NUMBER, "Number expected as retval");
		is(pe->number, count, "Check returned value");
	}
	is(count, 4, "Expected 4 values");
	port_destroy(&ret);

	region_truncate(&fiber()->gc, region_svp);
	port_destroy(&args);
	func_adapter_destroy(func_adapter);
	test_func_delete(func);

	check_plan();
	footer();
}

static void
test_func_adapter_func_is_pinned(void)
{
	plan(FUNC_HOLDER_MAX * 2);
	header();

	const char *body = "function(a, b, c, d) return d, c, b, a end";
	struct func *func = test_func_new(42, "test_func", body);
	fail_if(func == NULL);

	for (int i = 0; i < FUNC_HOLDER_MAX; i++) {
		enum func_holder_type pin_type = i;
		struct func_adapter *func_adapter =
			func_adapter_func_create(func, pin_type);
		fail_if(func_adapter == NULL);

		enum func_holder_type returned_pin_type = FUNC_HOLDER_MAX;
		ok(func_is_pinned(func, &returned_pin_type),
		   "Underlying func must be pinned");
		is(returned_pin_type, pin_type,
		   "Func must be pinned with passed type");

		func_adapter_destroy(func_adapter);
	}

	test_func_delete(func);

	check_plan();
	footer();
}

static int
test_main(void)
{
	plan(2);
	header();
	test_func_adapter_func_basic();
	test_func_adapter_func_is_pinned();
	footer();
	return check_plan();
}

int
main(void)
{
	memory_init();
	fiber_init(fiber_c_invoke);
	port_init();
	func_cache_init();
	lua_State *L = luaT_newteststate();
	tarantool_L = L;
	tarantool_lua_serializer_init(L);
	tarantool_lua_error_init(L);
	tarantool_lua_utils_init(L);
	luaopen_msgpack(L);
	box_lua_call_init(L);
	box_lua_misc_init(L);

	int rc = test_main();

	lua_close(L);
	tarantool_L = NULL;
	func_cache_destroy();
	port_free();
	fiber_free();
	memory_free();
	return rc;
}
