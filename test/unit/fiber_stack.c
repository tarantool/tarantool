#include "memory.h"
#include "fiber.h"
#include "trivia/util.h"
#include "errinj.h"

#define UNIT_TAP_COMPATIBLE 1
#include "unit.h"

static struct fiber_attr default_attr;

/** Total count of allocated fibers in the cord. Including dead ones. */
static int
fiber_count_total(void)
{
	size_t res = mempool_count(&cord()->fiber_mempool);
	assert(res <= INT_MAX);
	return (int)res;
}

static int
noop_f(va_list ap)
{
	return 0;
}

static int
main_f(va_list ap)
{
	struct slab_cache *slabc = &cord()->slabc;
	size_t used_before, used_after;
	struct errinj *inj;
	struct fiber *fiber;
	int fiber_count = fiber_count_total();
	struct fiber_attr *fiber_attr;

	header();
#ifdef NDEBUG
	plan(1);
#else
	plan(11);
#endif

	/*
	 * gh-9026. Stack size crafted to be close to 64k so we should
	 * hit red zone around stack when writing watermark if bug is not
	 * fixed.
	 *
	 * The test is placed at the beginning because stderr is redirected
	 * to /dev/null at the end of the test and ASAN diagnostic will
	 * not be visible if the test will be placed at the end.
	 */
	fiber_attr = fiber_attr_new();
	fiber_attr_setstacksize(fiber_attr, (64 << 10) - 128);
	fiber = fiber_new_ex("gh-9026", fiber_attr, noop_f);
	fiber_set_joinable(fiber, true);
	fiber_start(fiber);
	fiber_join(fiber);
	fiber_attr_delete(fiber_attr);

	/*
	 * Check the default fiber stack size value.
	 */
	fiber_attr = fiber_attr_new();
	ok(default_attr.stack_size == FIBER_STACK_SIZE_DEFAULT,
	   "fiber_attr: the default stack size is %ld, but %d is set via CMake",
	   default_attr.stack_size, FIBER_STACK_SIZE_DEFAULT);

#ifndef NDEBUG
	/*
	 * Set non-default stack size to prevent reusing of an
	 * existing fiber.
	 */
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
	 * Check madvise error on fiber creation.
	 */
	diag_clear(diag_get());
	inj = errinj(ERRINJ_FIBER_MADVISE, ERRINJ_BOOL);
	inj->bparam = true;
	fiber = fiber_new_ex("test_madvise", fiber_attr, noop_f);
	inj->bparam = false;

	ok(fiber_count_total() == fiber_count + 1, "allocated new");
	ok(fiber != NULL, "madvise: non critical error on madvise hint");
	ok(diag_get() != NULL, "madvise: diag is armed after error");

	fiber_wakeup(fiber);
	fiber_sleep(0);
	cord_collect_garbage(cord());
	ok(fiber_count_total() == fiber_count, "fiber is deleted");

	/*
	 * Check if we leak on fiber destruction.
	 * We will print an error and result get
	 * compared by testing engine.
	 */
	fiber_attr_delete(fiber_attr);
	fiber_attr = fiber_attr_new();
	fiber_attr->flags |= FIBER_CUSTOM_STACK;
	fiber_attr->stack_size = 64 << 10;

	diag_clear(diag_get());

	used_before = slabc->allocated.stats.used;

	fiber = fiber_new_ex("test_madvise", fiber_attr, noop_f);
	ok(fiber != NULL, "fiber with custom stack");
	ok(fiber_count_total() == fiber_count + 1, "allocated new");
	fiber_set_joinable(fiber, true);

	inj = errinj(ERRINJ_FIBER_MPROTECT, ERRINJ_INT);
	inj->iparam = PROT_READ | PROT_WRITE;

	/* On fiber_mprotect() fail we are logging number of bytes to be
	 * leaked. However, it depends on system page_size (_SC_PAGESIZE).
	 * On different OS's this parameter may vary. So let's temporary
	 * redirect stderr to dev/null to make this test stable regardless
	 * of OS.
	 */
	freopen("/dev/null", "w", stderr);
	fiber_start(fiber);
	fiber_join(fiber);
	inj->iparam = -1;
	freopen("/dev/stderr", "w", stderr);

	used_after = slabc->allocated.stats.used;
	ok(used_after > used_before, "expected leak detected");

	cord_collect_garbage(cord());
	ok(fiber_count_total() == fiber_count, "fiber is deleted");
#endif /* ifndef NDEBUG */

	fiber_attr_delete(fiber_attr);
	ev_break(loop(), EVBREAK_ALL);

	footer();
	return 0;
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
	return check_plan();
}
