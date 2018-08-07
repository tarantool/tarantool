#include "memory.h"
#include "fiber.h"
#include "unit.h"

static void
sigsegf_handler(int signo)
{
	note("signal handler called");
	exit(0);
}

static int __attribute__((noinline))
stack_break_f(char *ptr)
{
	char block[2048];
	/*
	 * Make sum volatile to prevent a compiler from
	 * optimizing away call to this function.
	 */
	volatile char sum = 0;
	memset(block, 0xff, 2048);
	sum += block[block[4]];
	ptrdiff_t stack_diff = ptr > block ? ptr - block : block - ptr;
	if (stack_diff < 65536)
		sum += stack_break_f(ptr);
	return sum;
}

static char stack_buf[SIGSTKSZ];

static int
main_f(va_list ap)
{
	stack_t stack;
	stack.ss_sp = stack_buf;
	stack.ss_size = SIGSTKSZ;
	stack.ss_flags = 0;
	sigaltstack(&stack, NULL);
	struct sigaction sa;
	sa.sa_handler = sigsegf_handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_ONSTACK;
	sigaction(SIGSEGV, &sa, NULL);
	sigaction(SIGBUS, &sa, NULL);

	int res = stack_break_f((char *)&stack);

	ev_break(loop(), EVBREAK_ALL);
	return res;
}

int main()
{
	memory_init();
	fiber_init(fiber_cxx_invoke);
	struct fiber *fmain = fiber_new_xc("main", main_f);
	fiber_wakeup(fmain);
	ev_run(loop(), 0);
	fiber_free();
	memory_free();
	fail("signal handler was not executed", "");
	return 0;
}
