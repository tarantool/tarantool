#include <errno.h>
#include <stdint.h>

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

static void
test_custom_stack_size(size_t min_stack_size)
{
	header();
	plan(10);

	struct fiber_attr attr;
	fiber_attr_create(&attr);
	int rc = fiber_attr_setstacksize(&attr, min_stack_size - 1);
	ok(rc == -1 && errno == EINVAL, "reject a stack below the minimum");
	is(fiber_attr_getstacksize(&attr), FIBER_STACK_SIZE_DEFAULT,
	   "rejection preserves the stack size");
	diag_clear(diag_get());
	rc = fiber_attr_setstacksize(&attr, min_stack_size);
	is(rc, 0, "accept the minimum stack size");
	fail_unless(rc == 0);
	struct fiber *fiber = fiber_new_ex("min_stack", &attr, noop_f);
	size_t usable_min = 8 << 10;
	ok(fiber->stack_size >= usable_min,
	   "the minimum stack has at least 8 KiB of usable space");
	fail_unless(fiber->stack_size >= usable_min);
	fiber_set_joinable(fiber, true);
	fiber_start(fiber);
	is(fiber_join(fiber), 0, "the fiber with the minimum stack size runs");

	size_t legacy_min = 16 << 10;
	is(fiber_attr_setstacksize(&attr, legacy_min),
	   min_stack_size == legacy_min ? 0 : -1,
	   "accept 16 KiB only when it fits the guard and usable stack");
	diag_clear(diag_get());

	size_t odd_size = MAX(min_stack_size, 64 << 10) + 1;
	rc = fiber_attr_setstacksize(&attr, odd_size);
	is(rc, 0, "accept an odd stack size");
	fail_unless(rc == 0);
	is(fiber_attr_getstacksize(&attr), odd_size,
	   "the getter returns the requested size");
	fiber = fiber_new_ex("custom_stack", &attr, noop_f);
	uintptr_t stack_start = (uintptr_t)fiber->stack;
	uintptr_t stack_end = stack_start + fiber->stack_size;
	bool aligned = stack_start % 16 == 0 && stack_end % 16 == 0;
	ok(aligned, "both stack bounds are aligned to 16 bytes");
	fail_unless(aligned);
	fiber_set_joinable(fiber, true);
	fiber_start(fiber);
	is(fiber_join(fiber), 0, "the fiber with an odd stack size runs");

	footer();
	check_plan();
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
	size_t page_size = small_getpagesize();
	size_t min_stack_size = MAX(16 << 10, 2 * page_size + (8 << 10));

	header();
#ifdef NDEBUG
	plan(2);
#else
	plan(11);
#endif

	/*
	 * gh-9026. Stack size crafted to be close to 64k so we should
	 * hit red zone around stack when writing watermark if bug is not
	 * fixed.
	 */
	size_t watermark_size = (64 << 10) - 128;

	/*
	 * On Linux AArch64 with 64 KiB pages, the minimum stack size is
	 * 2 * 64 KiB + 8 KiB = 136 KiB, so this request is too small.
	 */
	if (watermark_size >= min_stack_size) {
		fiber_attr = fiber_attr_new();
		int rc = fiber_attr_setstacksize(fiber_attr, watermark_size);
		fail_unless(rc == 0);
		fiber = fiber_new_ex("gh-9026", fiber_attr, noop_f);
		fiber_set_joinable(fiber, true);
		fiber_start(fiber);
		fiber_join(fiber);
		fiber_attr_delete(fiber_attr);
	}
	test_custom_stack_size(min_stack_size);

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

	ok(fiber != NULL, "mprotect: fiber guard page setup failure ignored");
	fiber_set_joinable(fiber, true);
	fiber_start(fiber);
	fiber_join(fiber);

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
	size_t stack_size = MAX(min_stack_size, 64 << 10);
	fail_unless(fiber_attr_setstacksize(fiber_attr, stack_size) == 0);

	diag_clear(diag_get());

	used_before = slab_cache_used(slabc);

	fiber = fiber_new_ex("test_madvise", fiber_attr, noop_f);
	ok(fiber != NULL, "fiber with custom stack");
	ok(fiber_count_total() == fiber_count + 1, "allocated new");
	fiber_set_joinable(fiber, true);

	inj = errinj(ERRINJ_FIBER_MPROTECT, ERRINJ_INT);
	inj->iparam = PROT_READ | PROT_WRITE;

	fiber_start(fiber);
	fiber_join(fiber);
	inj->iparam = -1;

	used_after = slab_cache_used(slabc);
#ifndef ENABLE_ASAN
	ok(used_after > used_before, "expected leak detected");
#else
	ok(used_after == used_before, "no leak under ASAN");
#endif

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
