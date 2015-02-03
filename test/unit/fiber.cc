#include "memory.h"
#include "fiber.h"
#include "unit.h"

static void
noop_f(va_list ap)
{
	return;
}

static void
cancel_f(va_list ap)
{
	fiber_set_cancellable(true);
	while (true) {
		fiber_sleep(0.001);
		fiber_testcancel();
	}
}

static void
exception_f(va_list ap)
{
	tnt_raise(OutOfMemory, 42, "allocator", "exception");
}

static void
fiber_join_test()
{
	header();

	struct fiber *fiber= fiber_new("join", noop_f);
	fiber_set_joinable(fiber, true);
	fiber_wakeup(fiber);
	fiber_join(fiber);

	fiber = fiber_new("cancel", cancel_f);
	fiber_set_joinable(fiber, true);
	fiber_wakeup(fiber);
	fiber_sleep(0);
	fiber_cancel(fiber);
	fiber_join(fiber);

	fiber = fiber_new("exception", exception_f);
	fiber_set_joinable(fiber, true);
	fiber_wakeup(fiber);
	try {
		fiber_join(fiber);
		fail("exception not raised", "");
	} catch (Exception *e) {
		note("exception propagated");
	}
	footer();
}

static void
main_f(va_list ap)
{
	fiber_join_test();
	ev_break(loop(), EVBREAK_ALL);
}

int main()
{
	memory_init();
	fiber_init();
	struct fiber *main = fiber_new("main", main_f);
	fiber_wakeup(main);
	ev_run(loop(), 0);
	fiber_free();
	memory_free();
	return 0;
}
