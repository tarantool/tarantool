#include <unistd.h>

#include "memory.h"
#include "fiber.h"
#include "unit.h"
#include "trivia/util.h"

static size_t stack_expand_limit;
static struct fiber_attr default_attr;

static unsigned long page_size;
#define PAGE_4K 4096

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
wait_cancel_f(va_list ap)
{
	while (!fiber_is_cancelled())
		fiber_yield();
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

static void NOINLINE
stack_expand(unsigned long *ret, unsigned long nr_calls)
{
	char volatile fill[PAGE_4K];
	char volatile *p;

	memset((void *)fill, (unsigned char)nr_calls, sizeof(fill));
	p = fill;
	p[PAGE_4K / 2] = (unsigned char)nr_calls;

	if (nr_calls != 0) {
		stack_expand(ret, nr_calls-1);
	} else {
		*ret = (unsigned long)&fill[0];
	}
}

static int
test_stack_f(va_list ap)
{
	unsigned long ret = 0;
	unsigned long ret_addr = (unsigned long)&ret;

	/*
	 * We can't just dirty the stack in precise
	 * way without using assembly. Thus lets do
	 * the following trick:
	 *  - assume 8K will be enough to carry all
	 *    arguments passed for all calls, still
	 *    we might need to adjust this value
	 */
	stack_expand(&ret, (stack_expand_limit - 2 * page_size) / page_size);
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
	fiber_reschedule();
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
	struct slab_cache *slabc = &cord()->slabc;

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
	int fiber_count = fiber_count_total();
	size_t used1 = slabc->allocated.stats.used;
	fiber_attr = fiber_attr_new();
	fiber_attr_setstacksize(fiber_attr, default_attr.stack_size * 2);
	stack_expand_limit = default_attr.stack_size * 3 / 2;
	fiber = fiber_new_ex("test_stack", fiber_attr, test_stack_f);
	fail_unless(fiber_count + 1 == fiber_count_total());
	fiber_attr_delete(fiber_attr);
	if (fiber == NULL)
		diag_raise();
	fiber_wakeup(fiber);
	fiber_sleep(0);
	cord_collect_garbage(cord());
	fail_unless(fiber_count == fiber_count_total());
	size_t used2 = slabc->allocated.stats.used;
	fail_unless(used2 == used1);
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

	char long_name[FIBER_NAME_MAX + 30];
	memset(long_name, 'a', sizeof(long_name));
	long_name[sizeof(long_name) - 1] = 0;

	fiber_set_name(fiber(), long_name);

	note("fiber name is truncated: %s.\n", fiber_name(fiber()));
	footer();
}

static void
fiber_wakeup_self_test()
{
	header();

	struct fiber *f = fiber();

	fiber_wakeup(f);
	double duration = 0.001;
	uint64_t t1 = fiber_clock64();
	fiber_sleep(duration);
	uint64_t t2 = fiber_clock64();
	/*
	 * It was a real sleep, not 0 duration. Wakeup is nop on the running
	 * fiber.
	 */
	assert(t2 - t1 >= duration);

	/*
	 * Wakeup + start of a new fiber. This is different from yield but
	 * works without crashes too.
	 */
	struct fiber *newf = fiber_new_xc("nop", noop_f);
	fiber_wakeup(f);
	fiber_start(newf);

	footer();
}

static void
fiber_dead_while_in_cache_test(void)
{
	header();

	struct fiber *f = fiber_new_xc("nop", noop_f);
	int fiber_count = fiber_count_total();
	fiber_start(f);
	/* The fiber remains in the cache of recycled fibers. */
	fail_unless(fiber_count == fiber_count_total());
	fail_unless(fiber_is_dead(f));

	footer();
}

static void
fiber_flags_respect_test(void)
{
	header();

	/* Make sure the cache has at least one fiber. */
	struct fiber *f = fiber_new_xc("nop", noop_f);
	fiber_start(f);

	/* Fibers taken from the cache need to respect the passed flags. */
	struct fiber_attr attr;
	fiber_attr_create(&attr);
	uint32_t flags = FIBER_IS_JOINABLE | FIBER_IS_CANCELLABLE;
	attr.flags |= flags;
	f = fiber_new_ex("wait_cancel", &attr, wait_cancel_f);
	fail_unless((f->flags & flags) == flags);
	fiber_wakeup(f);
	fiber_cancel(f);
	fiber_join(f);

	footer();
}

static int
main_f(va_list ap)
{
	fiber_name_test();
	fiber_join_test();
	fiber_stack_test();
	fiber_wakeup_self_test();
	fiber_dead_while_in_cache_test();
	fiber_flags_respect_test();
	ev_break(loop(), EVBREAK_ALL);
	return 0;
}

int main()
{
	page_size = sysconf(_SC_PAGESIZE);

	/* Page should be at least 4K */
	assert(page_size >= PAGE_4K);

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
