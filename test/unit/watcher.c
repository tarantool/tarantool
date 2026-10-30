#include <stdarg.h>
#include <stddef.h>
#include <string.h>

#include "box/watcher.h"
#include "fiber.h"
#include "lua/utils.h"
#include "lualib.h"
#include "memory.h"
#include "tarantool_ev.h"
#include "trivia/util.h"

#define UNIT_TAP_COMPATIBLE 1
#include "unit.h"

#define TEST_TIMEOUT 5

static int test_result;

struct test_watcher {
	struct watcher base;
	/** Number of times the watcher callback was called. */
	int run_count;
	/** Number of times the watcher destructor was called. */
	int destroy_count;
	/** Arguments passed to the callback. */
	char *key;
	size_t key_len;
	char *data;
	char *data_end;
	/** Call watcher_ack() before returning from the callback. */
	bool do_ack;
	/** Sleep while this flag is set before acknowledging notification. */
	bool do_sleep;
	/** Sleep while this flag is set before returning from callback. */
	bool do_sleep_after_ack;
	/** Sleeping fiber, if 'do_sleep' or 'do_sleep_after_ack' is set. */
	struct fiber *fiber;
};

static void
test_watcher_run_f(struct watcher *base)
{
	struct test_watcher *w = (struct test_watcher *)base;
	w->fiber = fiber();
	size_t key_len;
	const char *key = watcher_key(base, &key_len);
	const char *data_end;
	const char *data = watcher_data(base, &data_end);
	w->run_count++;
	free(w->key);
	w->key = xstrdup(key);
	w->key_len = key_len;
	free(w->data);
	if (data != NULL) {
		w->data = xmalloc(data_end - data);
		w->data_end = w->data + (data_end - data);
		memcpy(w->data, data, data_end - data);
	} else {
		w->data = NULL;
		w->data_end = NULL;
	}
	while (w->do_sleep)
		fiber_sleep(TIMEOUT_INFINITY);
	if (w->do_ack)
		watcher_ack(base);
	while (w->do_sleep_after_ack)
		fiber_sleep(TIMEOUT_INFINITY);
}

static void
test_watcher_destroy_f(struct watcher *base)
{
	struct test_watcher *w = (struct test_watcher *)base;
	w->destroy_count++;
}

static void
test_watcher_create(struct test_watcher *w)
{
	memset(w, 0, sizeof(*w));
}

static void
test_watcher_destroy(struct test_watcher *w)
{
	free(w->key);
	free(w->data);
	TRASH(w);
}

static void
test_watcher_register_with_flags(struct test_watcher *w, const char *key,
				 unsigned flags)
{
	box_register_watcher(key, strlen(key), test_watcher_run_f,
			     test_watcher_destroy_f, flags, &w->base);
}

static void
test_watcher_register(struct test_watcher *w, const char *key)
{
	test_watcher_register_with_flags(w, key, 0);
}

static void
test_watcher_unregister(struct test_watcher *w)
{
	watcher_unregister(&w->base);
}

static void
test_watcher_ack(struct test_watcher *w)
{
	watcher_ack(&w->base);
}

static void
test_watcher_resume_sleeping(struct test_watcher *w)
{
	assert(w->fiber != NULL);
	fiber_wakeup(w->fiber);
	w->fiber = NULL;
	w->do_sleep = false;
}

#define test_watcher_check_destroy_count(w, count)			\
do {									\
	fiber_sleep(0);							\
	is((w)->destroy_count, (count), "watcher destroy count");	\
} while (0)

#define test_watcher_check_run_count(w, count)				\
do {									\
	fiber_sleep(0);							\
	is((w)->run_count, (count), "watcher run count");		\
} while (0)

static bool
test_watcher_key_equal(struct test_watcher *w, const char *key)
{
	return (strlen(key) == w->key_len &&
		strncmp(key, w->key, w->key_len) == 0);
}

static bool
test_value_equal(const char *data, const char *data_end, const char *value)
{
	size_t data_size = data_end - data;
	return ((value == NULL && data == NULL && data_end == NULL) ||
		(value != NULL && strlen((value)) == data_size &&
		 strncmp(value, data, data_size) == 0));
}

static bool
test_watcher_value_equal(struct test_watcher *w, const char *value)
{
	return test_value_equal(w->data, w->data_end, value);
}

#define test_watcher_check_args(w, key, value)				\
do {									\
	ok(test_watcher_key_equal((w), (key)), "watcher key");		\
	ok(test_watcher_value_equal((w), (value)), "watcher data");	\
} while (0)

static void
test_broadcast(const char *key, const char *value)
{
	box_broadcast(key, strlen(key), value,
		      value != NULL ? value + strlen(value) : NULL);
}

#define test_watch_once(key, value)					\
do {									\
	const char *data, *data_end;					\
	data = box_watch_once((key), strlen(key), &data_end);		\
	ok(test_value_equal(data, data_end, value), "value");		\
} while (0)

/**
 * Checks that watchers are invoked with correct arguments on broadcast.
 */
static void
test_basic(void)
{
	header();
	plan(22);

	struct test_watcher w1, w2, w3;
	test_watcher_create(&w1);
	test_watcher_create(&w2);
	test_watcher_create(&w3);

	test_broadcast("foo", "bar");
	test_broadcast("fuzz", "buzz");

	test_watcher_register(&w1, "foo");
	test_watcher_register(&w2, "foo");
	test_watcher_register(&w3, "bar");

	test_watcher_check_run_count(&w1, 1);
	test_watcher_check_args(&w1, "foo", "bar");
	test_watcher_check_run_count(&w2, 1);
	test_watcher_check_args(&w2, "foo", "bar");
	test_watcher_check_run_count(&w3, 1);
	test_watcher_check_args(&w3, "bar", NULL);

	test_broadcast("bar", "baz");
	test_broadcast("fuzz", "fuzz buzz");
	test_watcher_check_run_count(&w3, 2);
	test_watcher_check_args(&w3, "bar", "baz");
	test_watcher_check_run_count(&w1, 1);
	test_watcher_check_run_count(&w2, 1);

	test_watcher_unregister(&w2);
	test_watcher_unregister(&w3);

	test_broadcast("foo", "fuzz");
	test_watcher_check_run_count(&w1, 2);
	test_watcher_check_args(&w1, "foo", "fuzz");
	test_watcher_check_run_count(&w2, 1);
	test_watcher_check_run_count(&w3, 2);

	test_watcher_unregister(&w1);

	test_watcher_check_destroy_count(&w1, 1);
	test_watcher_check_destroy_count(&w2, 1);
	test_watcher_check_destroy_count(&w3, 1);

	test_broadcast("foo", NULL);
	test_broadcast("bar", NULL);
	test_broadcast("fuzz", NULL);

	test_watcher_destroy(&w1);
	test_watcher_destroy(&w2);
	test_watcher_destroy(&w3);

	check_plan();
	footer();
}

/**
 * Checks that an async watcher doesn't block the worker fiber.
 */
static void
test_async(void)
{
	header();
	plan(5);

	struct test_watcher w1, w2;
	test_watcher_create(&w1);
	test_watcher_create(&w2);

	test_watcher_register(&w1, "foo");
	test_watcher_check_run_count(&w1, 1);

	w2.do_sleep = true;
	test_watcher_register_with_flags(&w2, "bar", WATCHER_RUN_ASYNC);
	test_watcher_check_run_count(&w2, 1);

	test_broadcast("foo", NULL);
	test_watcher_check_run_count(&w1, 2);

	test_watcher_resume_sleeping(&w2);
	test_watcher_unregister(&w1);
	test_watcher_unregister(&w2);
	test_watcher_check_destroy_count(&w1, 1);
	test_watcher_check_destroy_count(&w2, 1);
	test_watcher_destroy(&w1);
	test_watcher_destroy(&w2);

	check_plan();
	footer();
}

/**
 * Updates a key while a watcher is running and checks that the watcher is
 * rescheduled.
 */
static void
test_update_running(void)
{
	header();
	plan(3);

	struct test_watcher w;
	test_watcher_create(&w);

	w.do_sleep = true;
	test_watcher_register_with_flags(&w, "foo", WATCHER_RUN_ASYNC);
	test_watcher_check_run_count(&w, 1);

	test_broadcast("foo", NULL);
	test_watcher_resume_sleeping(&w);
	test_watcher_check_run_count(&w, 2);

	test_watcher_unregister(&w);
	test_watcher_check_destroy_count(&w, 1);
	test_watcher_destroy(&w);

	check_plan();
	footer();
}

/**
 * Unregisters a running watcher and checks that it isn't invoked again.
 */
static void
test_unregister_running(void)
{
	header();
	plan(4);

	struct test_watcher w;
	test_watcher_create(&w);

	w.do_sleep = true;
	test_watcher_register_with_flags(&w, "foo", WATCHER_RUN_ASYNC);
	test_watcher_check_run_count(&w, 1);

	test_watcher_unregister(&w);
	test_watcher_check_destroy_count(&w, 0);
	test_broadcast("foo", NULL);
	test_watcher_resume_sleeping(&w);
	test_watcher_check_destroy_count(&w, 1);
	test_watcher_check_run_count(&w, 1);

	test_watcher_destroy(&w);

	check_plan();
	footer();
}

/**
 * Checks that a WATCHER_EXPLICIT_ACK watcher isn't invoked until it
 * acknowledges the last notification.
 */
static void
test_ack(void)
{
	header();
	plan(15);

	struct test_watcher w1, w2, w3;
	test_watcher_create(&w1);
	test_watcher_create(&w2);
	test_watcher_create(&w3);

	test_watcher_register_with_flags(
		&w1, "foo", WATCHER_EXPLICIT_ACK);
	test_watcher_register_with_flags(
		&w2, "foo", WATCHER_EXPLICIT_ACK | WATCHER_RUN_ASYNC);
	test_watcher_register(&w3, "foo");

	/* Ack before receiving a notification is nop. */
	test_watcher_ack(&w1);

	test_watcher_check_run_count(&w1, 1);
	test_watcher_check_run_count(&w2, 1);
	test_watcher_check_run_count(&w3, 1);

	test_broadcast("foo", NULL);
	test_watcher_check_run_count(&w1, 1);
	test_watcher_check_run_count(&w2, 1);
	test_watcher_check_run_count(&w3, 2);

	test_watcher_ack(&w1);
	/* Ack without WATCHER_EXPLICIT_ACK is nop. */
	test_watcher_ack(&w3);

	test_watcher_check_run_count(&w1, 2);
	test_watcher_check_run_count(&w2, 1);
	test_watcher_check_run_count(&w3, 2);

	test_watcher_ack(&w2);
	/* Second ack is nop. */
	test_watcher_ack(&w2);

	test_watcher_check_run_count(&w1, 2);
	test_watcher_check_run_count(&w2, 2);
	test_watcher_check_run_count(&w3, 2);

	test_watcher_unregister(&w1);
	test_watcher_unregister(&w2);
	test_watcher_unregister(&w3);
	test_watcher_check_destroy_count(&w1, 1);
	test_watcher_check_destroy_count(&w2, 1);
	test_watcher_check_destroy_count(&w3, 1);
	test_watcher_destroy(&w1);
	test_watcher_destroy(&w2);
	test_watcher_destroy(&w3);

	check_plan();
	footer();
}

/**
 * Checks that calling watcher_ack() from the callback after the watcher was
 * unregistered works fine.
 */
static void
test_ack_unregistered(void)
{
	header();
	plan(4);

	struct test_watcher w;
	test_watcher_create(&w);

	w.do_ack = true;
	w.do_sleep = true;
	test_watcher_register_with_flags(
		&w, "foo", WATCHER_EXPLICIT_ACK | WATCHER_RUN_ASYNC);
	test_watcher_check_run_count(&w, 1);

	test_watcher_unregister(&w);
	test_watcher_check_destroy_count(&w, 0);
	test_broadcast("foo", NULL);
	test_watcher_resume_sleeping(&w);
	test_watcher_check_destroy_count(&w, 1);
	test_watcher_check_run_count(&w, 1);

	test_watcher_destroy(&w);

	check_plan();
	footer();
}

/**
 * Checks that the destructor is called once in case a watcher is unregistered
 * while more than instance of the watcher callback is running.
 */
static void
test_parallel(void)
{
	header();
	plan(6);

	struct test_watcher w;
	test_watcher_create(&w);
	w.do_ack = true;
	test_watcher_register_with_flags(
		&w, "foo", WATCHER_EXPLICIT_ACK | WATCHER_RUN_ASYNC);
	test_watcher_check_run_count(&w, 1);

	w.do_sleep_after_ack = true;
	test_broadcast("foo", "v1");
	test_watcher_check_run_count(&w, 2);
	struct fiber *f1 = w.fiber;
	ok(f1 != NULL, "callback is running");

	test_broadcast("foo", "v2");
	test_watcher_check_run_count(&w, 3);
	struct fiber *f2 = w.fiber;
	ok(f2 != NULL && f2 != f1, "callback is running");

	test_watcher_unregister(&w);
	w.do_sleep_after_ack = false;
	fiber_wakeup(f1);
	fiber_wakeup(f2);

	test_watcher_check_destroy_count(&w, 1);
	test_watcher_destroy(&w);
	test_broadcast("foo", NULL);

	check_plan();
	footer();
}

/**
 * Checks that all functions are callable and work as usual after
 * box_watcher_shutdown() except no notification are delivired.
 *
 * Checks that box_watcher_free() properly unregisters all watchers.
 */
static void
test_free(void)
{
	header();
	plan(17);

	test_broadcast("foo", "bar");
	test_broadcast("fuzz", "buzz");

	struct test_watcher w1, w2, w3;
	test_watcher_create(&w1);
	test_watcher_create(&w2);
	test_watcher_create(&w3);
	w3.do_ack = true;
	w3.do_sleep = true;

	test_watcher_register(&w1, "foo");
	test_watcher_register(&w2, "bar");
	test_watcher_register_with_flags(
		&w3, "bar", WATCHER_EXPLICIT_ACK | WATCHER_RUN_ASYNC);

	test_watcher_check_run_count(&w1, 1);
	test_watcher_check_run_count(&w2, 1);
	test_watcher_check_run_count(&w3, 1);

	box_watcher_shutdown();

	struct test_watcher w4, w5;
	test_watcher_create(&w4);
	test_watcher_create(&w5);

	test_watcher_register(&w4, "foo");
	test_watcher_register_with_flags(&w5, "foo", WATCHER_RUN_ASYNC);
	test_watcher_check_run_count(&w4, 0);
	test_watcher_check_run_count(&w5, 0);
	test_watch_once("foo", "bar");

	test_broadcast("foo", NULL);
	test_watcher_check_run_count(&w1, 1);
	test_watcher_check_run_count(&w2, 1);
	test_watcher_check_run_count(&w3, 1);
	test_watcher_check_run_count(&w4, 0);
	test_watcher_check_run_count(&w5, 0);
	test_watch_once("foo", NULL);

	test_watcher_resume_sleeping(&w3);
	box_watcher_free();

	test_watcher_check_destroy_count(&w1, 1);
	test_watcher_check_destroy_count(&w2, 1);
	test_watcher_check_destroy_count(&w3, 1);
	test_watcher_check_destroy_count(&w4, 1);
	test_watcher_check_destroy_count(&w5, 1);

	test_watcher_destroy(&w1);
	test_watcher_destroy(&w2);
	test_watcher_destroy(&w3);
	test_watcher_destroy(&w4);
	test_watcher_destroy(&w5);

	check_plan();
	footer();
}

static void
test_value(void)
{
	header();
	plan(8);

	test_watch_once("foo", NULL);
	test_watch_once("fuzz", NULL);

	test_broadcast("foo", "bar");
	test_broadcast("fuzz", "buzz");

	test_watch_once("foo", "bar");
	test_watch_once("fuzz", "buzz");

	test_broadcast("foo", NULL);

	test_watch_once("foo", NULL);
	test_watch_once("fuzz", "buzz");

	test_broadcast("fuzz", NULL);

	test_watch_once("foo", NULL);
	test_watch_once("fuzz", NULL);

	check_plan();
	footer();
}

static int
main_f(va_list ap)
{
	header();
	plan(9);
	box_watcher_init();
	test_basic();
	test_async();
	test_update_running();
	test_unregister_running();
	test_ack();
	test_ack_unregistered();
	test_parallel();
	test_value();
	test_free(); /* must be last */
	test_result = check_plan();
	footer();
	ev_break(loop(), EVBREAK_ALL);
	return 0;
}

int
main()
{
	struct lua_State *L = luaL_newstate();
	luaL_openlibs(L);
	tarantool_L = L;

	memory_init();
	fiber_init(fiber_c_invoke);
	struct fiber *f = fiber_new("main", main_f);
	fiber_wakeup(f);
	ev_run(loop(), 0);
	fiber_free();
	memory_free();

	lua_close(L);
	tarantool_L = NULL;
	return test_result;
}
