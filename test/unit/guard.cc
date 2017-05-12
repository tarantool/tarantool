#include "memory.h"
#include "fiber.h"
#include "unit.h"

static void
sigsegf_handler(int signo)
{
	note("signal handler called");
	exit(0);
}

static void
stack_break_f(void *ptr)
{
	char block[2048];
	memset(block, 0, 2048);
	if (abs((char *)ptr - block) < 65536)
		stack_break_f(ptr);
}

static int
main_f(va_list ap)
{
	char stack_buf[SIGSTKSZ];
	stack_t stack;
	stack.ss_sp = stack_buf;
	stack.ss_size = SIGSTKSZ;
	stack.ss_flags = SS_ONSTACK;
	sigaltstack(&stack, NULL);
	struct sigaction sa;
	sa.sa_handler = sigsegf_handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_ONSTACK;
	sigaction(SIGSEGV, &sa, NULL);

	stack_break_f(&stack);

	ev_break(loop(), EVBREAK_ALL);
	return 0;
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
