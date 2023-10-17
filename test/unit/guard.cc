#include "memory.h"
#include "fiber.h"

#define UNIT_TAP_COMPATIBLE 1
#include "unit.h"

static struct fiber_attr default_attr;

static void
sigsegf_handler(int signo)
{
	ok(true);
	footer();
	exit(check_plan());
}

/*
 * ASAN is disabled for this function, because for the stack-use-after-return
 * detection it could allocate the `block' on a fake stack, rather than placing
 * it on a fiber stack. In that case, a lot more recursive calls will be
 * required to overflow the stack.
 */
static NOINLINE NO_SANITIZE_ADDRESS int
stack_break_f(char *frame_zero)
{
	char *frame_curr = (char *)__builtin_frame_address(0);
	volatile char block[2048];
	/*
	 * Make sum volatile to prevent a compiler from
	 * optimizing away call to this function.
	 */
	volatile char sum = 0;
	memset((void *)block, 0xff, sizeof(block));
	sum += block[(unsigned char) block[4]];

	ptrdiff_t stack_diff = frame_curr - frame_zero;
	if ((size_t)abs(stack_diff) < default_attr.stack_size)
		sum += stack_break_f(frame_zero);
	return sum;
}

static int
main_f(va_list ap)
{
	stack_t stack;
	stack.ss_flags = 0;
	/*
	 * It is said that SIGSTKSZ is not enough for one of llvm sanitizers
	 * (probably asan, because this test fails with segmentation fault if
	 * we use SIGSTKSZ as alternative signal stack size when we use it).
	 * https://github.com/llvm/llvm-project/blame/699231ab3c7dd8f028d868b103481fa901f3c721/compiler-rt/lib/sanitizer_common/sanitizer_posix_libcdep.cpp#L169
	 */
	stack.ss_size = 4 * SIGSTKSZ;
	stack.ss_sp = xmalloc(stack.ss_size);
	if (sigaltstack(&stack, NULL) < 0) {
		free(stack.ss_sp);
		perror("sigaltstack");
		exit(EXIT_FAILURE);
	}
	struct sigaction sa;
	sa.sa_handler = sigsegf_handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_ONSTACK;
	sigaction(SIGSEGV, &sa, NULL);
	sigaction(SIGBUS, &sa, NULL);

	int res = stack_break_f((char *)__builtin_frame_address(0));

	ev_break(loop(), EVBREAK_ALL);
	free(stack.ss_sp);
	return res;
}

int main()
{
	plan(1);
	header();

#ifndef ENABLE_ASAN
	memory_init();
	fiber_init(fiber_cxx_invoke);
	fiber_attr_create(&default_attr);
	struct fiber *fmain = fiber_new_xc("main", main_f);
	fiber_wakeup(fmain);
	ev_run(loop(), 0);
	fiber_free();
	memory_free();
#else
	ok(true);
#endif

	footer();
	return check_plan();
}
