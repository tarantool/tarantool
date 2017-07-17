#include "memory.h"
#include "fiber.h"
#include "fiber_cond.h"
#include "unit.h"

static int
fiber_cond_basic_f(va_list ap)
{
	struct fiber_cond *cond = va_arg(ap, struct fiber_cond *);
	int *check = va_arg(ap, int *);

	int rc;

	rc = fiber_cond_wait_timeout(cond, 0.0);
	ok(rc != 0, "timeout");

	rc = fiber_cond_wait(cond);
	is(rc, 0, "signal");

	(*check)++;

	rc = fiber_cond_wait(cond);
	is(rc, 0, "broadcast");

	return 0;
}

static void
fiber_cond_basic()
{
	struct fiber_cond *cond = fiber_cond_new();
	int check = 0;

	struct fiber *f1 = fiber_new("f1", fiber_cond_basic_f);
	assert(f1 != NULL);
	fiber_start(f1, cond, &check);
	fiber_set_joinable(f1, true);

	struct fiber *f2 = fiber_new("f2", fiber_cond_basic_f);
	assert(f2 != NULL);
	fiber_start(f2, cond, &check);
	fiber_set_joinable(f2, true);

	/* check timeout */
	fiber_sleep(0.0);
	fiber_sleep(0.0);

	/* Wake up the first fiber */
	fiber_cond_signal(cond);
	fiber_sleep(0.0);

	/* Wake ip the second fiber */
	fiber_cond_signal(cond);
	fiber_sleep(0.0);

	/* Check that fiber scheduling is fair */
	is(check, 2, "order");

	fiber_cond_broadcast(cond);
	fiber_sleep(0.0);

	fiber_join(f1);
	fiber_join(f2);

	fiber_cond_delete(cond);
}

static int
main_f(va_list ap)
{
	(void) ap;
	fiber_cond_basic();
	ev_break(loop(), EVBREAK_ALL);
	return 0;
}

int
main()
{
	plan(7);
	memory_init();
	fiber_init(fiber_c_invoke);
	struct fiber *f = fiber_new("main", main_f);
	fiber_wakeup(f);
	ev_run(loop(), 0);
	fiber_free();
	memory_free();
	return check_plan();
}
