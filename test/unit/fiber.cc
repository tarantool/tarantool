#include "memory.h"
#include "fiber.h"
#include "unit.h"
#include "trivia/util.h"

static struct fiber_attr default_attr;

static int
noop_f(va_list ap)
{
	return 0;
}

static int
cancel_f(va_list ap)
{
	fiber_set_cancellable(true);
	while (true) {
		fiber_sleep(0.001);
		fiber_testcancel();
	}
	return 0;
}

static int
exception_f(va_list ap)
{
	tnt_raise(OutOfMemory, 42, "allocator", "exception");
	return 0;
}

static int
no_exception_f(va_list ap)
{
	try {
		tnt_raise(OutOfMemory, 42, "allocator", "exception");
	} catch (Exception *e) {
		;
	}
	return 0;
}

static int
cancel_dead_f(va_list ap)
{
	note("cancel dead has started");
	fiber_set_cancellable(true);
	tnt_raise(OutOfMemory, 42, "allocator", "exception");
	return 0;
}

static size_t stack_expand_limit;

static void NOINLINE
stack_expand(void *ptr)
{
	char buf[2048];
	memset(buf, 0x45, 2048);
	ptrdiff_t stack_diff = (buf - (char *)ptr);
	stack_diff = stack_diff >= 0 ? stack_diff : -stack_diff;
	if (stack_diff < (ptrdiff_t)stack_expand_limit)
		stack_expand(ptr);
}

static int
test_stack_f(va_list ap)
{
	char s;
	stack_expand(&s);
	return 0;
}

static void
fiber_join_test()
{
	header();

	struct fiber *fiber = fiber_new_xc("join", noop_f);
	fiber_set_joinable(fiber, true);
	fiber_wakeup(fiber);
	fiber_join(fiber);

	fiber = fiber_new_xc("cancel", cancel_f);
	fiber_set_joinable(fiber, true);
	fiber_wakeup(fiber);
	fiber_sleep(0);
	fiber_cancel(fiber);
	fiber_join(fiber);

	fiber = fiber_new_xc("exception", exception_f);
	fiber_set_joinable(fiber, true);
	fiber_wakeup(fiber);
	try {
		if (fiber_join(fiber) != 0)
			diag_raise();
		fail("exception not raised", "");
	} catch (Exception *e) {
		note("exception propagated");
	}

	fputs("#gh-1238: log uncaught errors\n", stderr);
	fiber = fiber_new_xc("exception", exception_f);
	fiber_wakeup(fiber);

	/*
	 * A fiber which is using exception should not
	 * push them up the stack.
	 */
	fiber = fiber_new_xc("no_exception", no_exception_f);
	fiber_set_joinable(fiber, true);
	fiber_wakeup(fiber);
	fiber_join(fiber);
	/*
	 * Trying to cancel a dead joinable cancellable fiber lead to
	 * a crash, because cancel would try to schedule it.
	 */
	fiber = fiber_new_xc("cancel_dead", cancel_dead_f);
	fiber_set_joinable(fiber, true);
	fiber_wakeup(fiber);
	/** Let the fiber schedule */
	fiber_wakeup(fiber());
	fiber_yield();
	note("by this time the fiber should be dead already");
	fiber_cancel(fiber);
	fiber_join(fiber);

	footer();
}

void
fiber_stack_test()
{
	header();

	struct fiber *fiber;
	struct fiber_attr *fiber_attr;

	/*
	 * Test a fiber with the default stack size.
	 */
	stack_expand_limit = default_attr.stack_size * 3 / 4;
	fiber = fiber_new_xc("test_stack", test_stack_f);
	fiber_wakeup(fiber);
	fiber_sleep(0);
	note("normal-stack fiber not crashed");

	/*
	 * Test a fiber with a custom stack size.
	 */
	fiber_attr = fiber_attr_new();
	fiber_attr_setstacksize(fiber_attr, default_attr.stack_size * 2);
	stack_expand_limit = default_attr.stack_size * 3 / 2;
	fiber = fiber_new_ex("test_stack", fiber_attr, test_stack_f);
	fiber_attr_delete(fiber_attr);
	if (fiber == NULL)
		diag_raise();
	fiber_wakeup(fiber);
	fiber_sleep(0);
	note("big-stack fiber not crashed");

	footer();
}

void
fiber_name_test()
{
	header();
	note("name of a new fiber: %s.\n", fiber_name(fiber()));

	fiber_set_name(fiber(), "Horace");

	note("set new fiber name: %s.\n", fiber_name(fiber()));

	const char *long_name = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
		"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"\
		"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";

	fiber_set_name(fiber(), long_name);

	note("fiber name is truncated: %s.\n", fiber_name(fiber()));
	footer();
}

static int
main_f(va_list ap)
{
	fiber_name_test();
	fiber_join_test();
	fiber_stack_test();
	ev_break(loop(), EVBREAK_ALL);
	return 0;
}

int main()
{
	memory_init();
	fiber_init(fiber_cxx_invoke);
	fiber_attr_create(&default_attr);
	struct fiber *main = fiber_new_xc("main", main_f);
	fiber_wakeup(main);
	ev_run(loop(), 0);
	fiber_free();
	memory_free();
	return 0;
}
