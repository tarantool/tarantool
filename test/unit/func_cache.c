#include "func_cache.h"
#include "func.h"
#include "trivia/util.h"

#define UNIT_TAP_COMPATIBLE 1
#include "unit.h"

int status;

struct func *
test_func_new(uint32_t id, const char *name)
{
	uint32_t name_len = strlen(name);
	struct func_def *def = func_def_new(id, ADMIN, name, name_len,
					    FUNC_LANGUAGE_LUA,
					    NULL, 0, NULL, 0, NULL);
	struct func *f = xmalloc(sizeof(*f));
	f->def = def;
	rlist_create(&f->func_cache_pin_list);
	return f;
}

static void
test_func_delete(struct func *f)
{
	func_def_delete(f->def);
	free(f);
}

/**
 * Test that pin/is_pinned/unpin works fine with one func and one holder.
 */
static void
func_cache_pin_test_one_holder(void)
{
	header();
	plan(7);

	func_cache_init();
	struct func *f1 = test_func_new(1, "func1");
	enum func_holder_type type = FUNC_HOLDER_MAX;
	struct func_cache_holder h1;

	func_cache_insert(f1);
	ok(!func_is_pinned(f1, &type), "ok");
	func_pin(f1, &h1, 1);
	ok(func_is_pinned(f1, &type), "ok");
	ok(type == 1, "ok");
	func_unpin(&h1);
	ok(!func_is_pinned(f1, &type), "ok");
	func_pin(f1, &h1, 1);
	ok(func_is_pinned(f1, &type), "ok");
	ok(type == 1, "ok");
	func_unpin(&h1);
	ok(!func_is_pinned(f1, &type), "ok");
	func_cache_delete(f1->def->fid);

	test_func_delete(f1);
	func_cache_destroy();

	footer();
	status |= check_plan();
}

/**
 * Test several holders that pin/unpins one func in FIFO order.
 */
static void
func_cache_pin_test_fifo(void)
{
	header();
	plan(8);

	func_cache_init();
	struct func *f1 = test_func_new(1, "func1");
	enum func_holder_type type = FUNC_HOLDER_MAX;
	struct func_cache_holder h1, h2;

	func_cache_insert(f1);
	ok(!func_is_pinned(f1, &type), "ok");
	func_pin(f1, &h1, 1);
	ok(func_is_pinned(f1, &type), "ok");
	ok(type == 1, "ok");
	func_pin(f1, &h2, 2);
	ok(func_is_pinned(f1, &type), "ok");
	ok(type == 1 || type == 2, "ok");
	func_unpin(&h1);
	ok(func_is_pinned(f1, &type), "ok");
	ok(type == 2, "ok");
	func_unpin(&h2);
	ok(!func_is_pinned(f1, &type), "ok");
	func_cache_delete(f1->def->fid);

	test_func_delete(f1);
	func_cache_destroy();

	footer();
	status |= check_plan();
}

/**
 * Test several holders that pin/unpins one func in LIFO order.
 */
static void
func_cache_pin_test_lifo(void)
{
	header();
	plan(8);

	func_cache_init();
	struct func *f1 = test_func_new(1, "func1");
	enum func_holder_type type = FUNC_HOLDER_MAX;
	struct func_cache_holder h1, h2;

	func_cache_insert(f1);
	ok(!func_is_pinned(f1, &type), "ok");
	func_pin(f1, &h1, 1);
	ok(func_is_pinned(f1, &type), "ok");
	ok(type == 1, "ok");
	func_pin(f1, &h2, 2);
	ok(func_is_pinned(f1, &type), "ok");
	ok(type == 1 || type == 2, "ok");
	func_unpin(&h2);
	ok(func_is_pinned(f1, &type), "ok");
	ok(type == 1, "ok");
	func_unpin(&h1);
	ok(!func_is_pinned(f1, &type), "ok");
	func_cache_delete(f1->def->fid);

	test_func_delete(f1);
	func_cache_destroy();

	footer();
	status |= check_plan();
}

/**
 * Test several holders with several funcs.
 */
static void
func_cache_pin_test_several(void)
{
	header();
	plan(18);

	func_cache_init();
	struct func *f1 = test_func_new(1, "func1");
	struct func *f2 = test_func_new(2, "func2");
	enum func_holder_type type = FUNC_HOLDER_MAX;
	struct func_cache_holder h1, h2, h3;

	func_cache_insert(f1);
	ok(!func_is_pinned(f1, &type), "ok");
	func_pin(f1, &h1, 1);
	ok(func_is_pinned(f1, &type), "ok");
	ok(type == 1, "ok");

	func_cache_insert(f2);
	ok(func_is_pinned(f1, &type), "ok");
	ok(!func_is_pinned(f2, &type), "ok");

	func_pin(f1, &h2, 2);
	ok(func_is_pinned(f1, &type), "ok");
	ok(!func_is_pinned(f2, &type), "ok");

	func_pin(f2, &h3, 3);
	ok(func_is_pinned(f1, &type), "ok");
	ok(type == 1 || type == 2, "ok");
	ok(func_is_pinned(f2, &type), "ok");
	ok(type == 3, "ok");

	func_unpin(&h1);
	ok(func_is_pinned(f1, &type), "ok");
	ok(type == 2, "ok");
	ok(func_is_pinned(f2, &type), "ok");
	ok(type == 3, "ok");

	func_unpin(&h3);
	ok(func_is_pinned(f1, &type), "ok");
	ok(!func_is_pinned(f2, &type), "ok");
	func_cache_delete(f2->def->fid);

	func_unpin(&h2);
	ok(!func_is_pinned(f1, &type), "ok");
	func_cache_delete(f1->def->fid);

	test_func_delete(f1);
	test_func_delete(f2);
	func_cache_destroy();

	footer();
	status |= check_plan();
}

int
main(void)
{
	plan(4);
	header();
	func_cache_pin_test_one_holder();
	func_cache_pin_test_fifo();
	func_cache_pin_test_lifo();
	func_cache_pin_test_several();
	status |= check_plan();
	footer();
	return status;
}
