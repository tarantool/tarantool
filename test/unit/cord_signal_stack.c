#include <signal.h>
#include <stdlib.h>

#include "fiber.h"
#include "memory.h"
#include "trivia/util.h"

#define UNIT_TAP_COMPATIBLE 1
#include "unit.h"

static void
check_signal_stack(const stack_t *previous, const stack_t *replacement,
		   const char *message)
{
	if (previous != NULL)
		fail_unless(sigaltstack(previous, NULL) == 0);

	fiber_init(fiber_c_invoke);
	stack_t current;
	fail_unless(sigaltstack(NULL, &current) == 0);
	ok((current.ss_flags & SS_DISABLE) == 0 &&
	   (previous == NULL || current.ss_sp != previous->ss_sp),
	   "fiber_init installs its own signal stack");
	if (replacement != NULL)
		fail_unless(sigaltstack(replacement, NULL) == 0);

	fiber_free();
	fail_unless(sigaltstack(NULL, &current) == 0);
	const stack_t *expected = replacement != NULL ? replacement : previous;
	if (expected == NULL) {
		ok((current.ss_flags & SS_DISABLE) != 0, "%s", message);
	} else {
		ok((current.ss_flags & SS_DISABLE) == 0 &&
		   current.ss_sp == expected->ss_sp &&
		   current.ss_size == expected->ss_size, "%s", message);
	}
}

static void
test_signal_stack(void)
{
	header();
	plan(6);

	memory_init();
	stack_t initial;
	fail_unless(sigaltstack(NULL, &initial) == 0);
	fail_unless((initial.ss_flags & SS_DISABLE) != 0);
	check_signal_stack(NULL, NULL, "fiber_free disables its signal stack");

	stack_t stack = {.ss_size = SIGSTKSZ};
	stack.ss_sp = xmalloc(stack.ss_size);
	check_signal_stack(&stack, NULL,
			   "fiber_free restores the previous stack");

	stack_t disabled = {.ss_flags = SS_DISABLE, .ss_size = MINSIGSTKSZ};
	check_signal_stack(&disabled, &stack,
			   "fiber_free preserves a replacement stack");
	fail_unless(sigaltstack(&disabled, NULL) == 0);
	free(stack.ss_sp);
	memory_free();

	check_plan();
	footer();
}

int
main(void)
{
	header();
	plan(1);
	test_signal_stack();
	footer();
	return check_plan();
}
