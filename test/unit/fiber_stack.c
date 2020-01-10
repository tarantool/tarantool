#include "memory.h"
#include "fiber.h"
#include "unit.h"
#include "trivia/util.h"
#include "errinj.h"

static struct fiber_attr default_attr;

static int
noop_f(va_list ap)
{
	return 0;
}

static int
main_f(va_list ap)
{
	struct errinj *inj;
	struct fiber *fiber;

	header();
	plan(4);

	/*
	 * Set non-default stack size to prevent reusing of an
	 * existing fiber.
	 */
	struct fiber_attr *fiber_attr = fiber_attr_new();
	fiber_attr_setstacksize(fiber_attr, default_attr.stack_size * 2);

	/*
	 * Clear the fiber's diagnostics area to check that failed
	 * fiber_new() sets an error.
	 */
	diag_clear(diag_get());

	/*
	 * Check guard page setup via mprotect. We can't test the fiber
	 * destroy path since it clears fiber's diag.
	 */
	inj = errinj(ERRINJ_FIBER_MPROTECT, ERRINJ_INT);
	inj->iparam = PROT_NONE;
	fiber = fiber_new_ex("test_mprotect", fiber_attr, noop_f);
	inj->iparam = -1;

	ok(fiber == NULL, "mprotect: failed to setup fiber guard page");
	ok(diag_get() != NULL, "mprotect: diag is armed after error");

	/*
	 * Check madvise. We can't test the fiber destroy
	 * path since it is cleaning error.
	 */
	diag_clear(diag_get());
	inj = errinj(ERRINJ_FIBER_MADVISE, ERRINJ_BOOL);
	inj->bparam = true;
	fiber = fiber_new_ex("test_madvise", fiber_attr, noop_f);
	inj->bparam = false;

	ok(fiber != NULL, "madvise: non critical error on madvise hint");
	ok(diag_get() != NULL, "madvise: diag is armed after error");

	footer();

	ev_break(loop(), EVBREAK_ALL);
	return check_plan();
}

int main()
{
	memory_init();
	fiber_init(fiber_c_invoke);
	fiber_attr_create(&default_attr);
	struct fiber *f = fiber_new("main", main_f);
	fiber_wakeup(f);
	ev_run(loop(), 0);
	fiber_free();
	memory_free();
	return 0;
}
