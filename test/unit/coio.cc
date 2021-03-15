#include "memory.h"
#include "fiber.h"
#include "coio.h"
#include "coio_task.h"
#include "fio.h"
#include "unit.h"
#include "unit.h"

int
touch_f(va_list ap)
{
	FILE *f = va_arg(ap, FILE *);
	const char *c = "c";
	while (true) {
		int rc = fwrite(c, strlen(c), 1, f);
		fail_unless(rc == 1);
		fflush(f);
		fiber_sleep(0.01);
		if (fiber_is_cancelled())
			return -1;
	}
	return 0;
}

static void
stat_notify_test(FILE *f, const char *filename)
{
	header();

	struct fiber *touch = fiber_new_xc("touch", touch_f);
	fiber_start(touch, f);
	ev_stat stat;
	note("filename: %s", filename);
	coio_stat_init(&stat, filename);
	coio_stat_stat_timeout(&stat, TIMEOUT_INFINITY);
	fail_unless(stat.prev.st_size < stat.attr.st_size);
	fiber_cancel(touch);

	footer();
}

static void
stat_timeout_test(const char *filename)
{
	header();

	ev_stat stat;
	coio_stat_init(&stat, filename);
	coio_stat_stat_timeout(&stat, 0.01);

	footer();
}

static ssize_t
coio_test_wakeup(va_list ap)
{
	usleep(1000);
	return 0;
}

static int
test_call_f(va_list ap)
{
	header();
	int res = coio_call(coio_test_wakeup);
	note("call done with res %i", res);
	footer();
	return res;
}

static void
test_getaddrinfo(void)
{
	header();
	plan(3);
	const char *host = "127.0.0.1";
	const char *port = "3333";
	struct addrinfo *i;
	/* NULL hints should work. It is a standard. */
	int rc = coio_getaddrinfo(host, port, NULL, &i, 1);
	is(rc, 0, "getaddrinfo");
	freeaddrinfo(i);

	/*
	 * gh-4138: Check getaddrinfo() retval and diagnostics
	 * area.
	 */
	rc = coio_getaddrinfo("non_exists_hostname", port, NULL, &i,
			      15768000000);
	isnt(rc, 0, "getaddrinfo retval");
	const char *errmsg = diag_get()->last->errmsg;
	bool is_match_with_exp = strstr(errmsg, "getaddrinfo") == errmsg;
	is(is_match_with_exp, true, "getaddrinfo error message");

	/*
	 * gh-4209: 0 timeout should not be a special value and
	 * detach a task. Before a fix it led to segfault
	 * sometimes. The cycle below runs getaddrinfo multiple
	 * times to increase segfault probability.
	 */
	for (int j = 0; j < 5; ++j) {
		if (coio_getaddrinfo(host, port, NULL, &i, 0) == 0 && i != NULL)
			freeaddrinfo(i);
		/*
		 * Skip one event loop to check, that the coio
		 * task destructor will not free the memory second
		 * time.
		 */
		fiber_sleep(0);
	}

	check_plan();
	footer();
}

static int
main_f(va_list ap)
{
	const char *filename = "1.out";
	FILE *f = fopen(filename, "w+");
	stat_timeout_test(filename);
	stat_notify_test(f, filename);
	fclose(f);
	(void) remove(filename);

	coio_enable();
	struct fiber *call_fiber = fiber_new_xc("coio_call wakeup", test_call_f);
	fiber_set_joinable(call_fiber, true);
	fiber_start(call_fiber);
	fiber_wakeup(call_fiber);
	fiber_cancel(call_fiber);
	fiber_join(call_fiber);

	test_getaddrinfo();

	ev_break(loop(), EVBREAK_ALL);
	return 0;
}

int main()
{
	memory_init();
	fiber_init(fiber_cxx_invoke);
	struct fiber *test = fiber_new_xc("coio_stat", main_f);
	fiber_wakeup(test);
	ev_run(loop(), 0);
	fiber_free();
	memory_free();
	return 0;
}
