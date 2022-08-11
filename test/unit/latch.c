#include "memory.h"
#include "fiber.h"
#include "latch.h"
#include "unit.h"

const size_t num_fibers = 3;

static int
order_f(va_list ap)
{
	size_t fid = *va_arg(ap, size_t *);
	size_t *check = va_arg(ap, size_t *);
	struct latch *latch = va_arg(ap, struct latch *);
	latch_lock(latch);

	is(fid, *check, "check order");
	++*check;

	latch_unlock(latch);
	return 0;
}

static int
sleep_f(va_list ap)
{
	struct latch *latch = va_arg(ap, struct latch *);
	latch_lock(latch);

	while (!fiber_is_cancelled())
		fiber_sleep(0.001);

	latch_unlock(latch);
	return 0;
}

static void
latch_order_test(bool wakeup_before_unlock)
{
	header();
	size_t check = 0;
	struct latch latch;
	latch_create(&latch);
	latch_lock(&latch);

	struct fiber *fibers[num_fibers];
	for (size_t i = 0; i < num_fibers; i++) {
		fibers[i] = fiber_new("ordered", order_f);
		fail_if(fibers[i] == NULL);
		fiber_set_joinable(fibers[i], true);
		fiber_start(fibers[i], &i, &check, &latch);
	}

	/*
	 * Try to break the order of waiters on the latch.
	 */
	if (wakeup_before_unlock)
		fiber_wakeup(fibers[1]);
	latch_unlock(&latch);
	if (!wakeup_before_unlock)
		fiber_wakeup(fibers[1]);

	for (size_t i = 0; i < num_fibers; i++)
		fiber_join(fibers[i]);
	latch_destroy(&latch);
	footer();
}

static void
latch_timeout_test(void)
{
	header();
	struct latch latch;
	latch_create(&latch);

	struct fiber *fiber = fiber_new("sleeping", sleep_f);
	fail_if(fiber == NULL);
	fiber_set_joinable(fiber, true);
	fiber_start(fiber, &latch);

	int exceeded = latch_lock_timeout(&latch, -0.1);
	is(exceeded, 1, "check timeout");

	fiber_cancel(fiber);
	fiber_join(fiber);
	latch_destroy(&latch);
	footer();
}

static int
main_f(va_list ap)
{
	latch_order_test(true);
	latch_order_test(false);
	latch_timeout_test();

	ev_break(loop(), EVBREAK_ALL);
	return 0;
}

int
main(void)
{
	plan(num_fibers * 2 + 1);
	memory_init();
	fiber_init(fiber_c_invoke);
	struct fiber *f = fiber_new("main", main_f);
	fiber_wakeup(f);
	ev_run(loop(), 0);
	fiber_free();
	memory_free();
	return check_plan();
}
