#include <signal.h>
#include <tarantool_ev.h>

#include "core/tt_static.h"
#include "fiber.h"
#include "memory.h"

#define UNIT_TAP_COMPATIBLE 1
#include "unit.h"

static int test_result = 1;

static struct ev_signal ev_sig;

static bool ev_sig_handler_expect_pid_self;
static int ev_sig_handler_call_count;

static void
ev_sig_handler(struct ev_loop *loop, struct ev_signal *w, int revents)
{
	(void)loop;
	(void)revents;

	note("Processing %s sent by PID %ld\n",
	     strsignal(w->signum), (long)w->sender_pid);

	fail_unless(w == &ev_sig);
	fail_unless(w->signum == SIGUSR1);
	fail_unless((w->sender_pid == getpid()) ==
		    ev_sig_handler_expect_pid_self);
	ev_sig_handler_call_count++;
}

static int
main_f(va_list ap)
{
	plan(6);
	header();

	/* Set the signal handler. */
	ev_signal_init(&ev_sig, ev_sig_handler, SIGUSR1);
	ev_signal_start(loop(), &ev_sig);

	/* Test raising a signal. */
	raise(SIGUSR1);
	ev_sig_handler_call_count = 0;
	ev_sig_handler_expect_pid_self = true;
	fiber_sleep(0);
	is(ev_sig_handler_call_count, 1, "raise()");

	/* Test killing oneself. */
	kill(getpid(), SIGUSR1);
	ev_sig_handler_call_count = 0;
	ev_sig_handler_expect_pid_self = true;
	fiber_sleep(0);
	is(ev_sig_handler_call_count, 1, "kill() by self");

	/* Test killing by child process. */
	system(tt_sprintf("bash -c 'kill -USR1 %ld'", (long)getpid()));
	ev_sig_handler_call_count = 0;
	ev_sig_handler_expect_pid_self = false;
	fiber_sleep(0);
	is(ev_sig_handler_call_count, 1, "kill() by child");

	/* Test multiple signals = one callback call. */
	raise(SIGUSR1);
	raise(SIGUSR1);
	ev_sig_handler_call_count = 0;
	ev_sig_handler_expect_pid_self = true;
	fiber_sleep(0);
	fiber_sleep(0);
	is(ev_sig_handler_call_count, 1, "multiple signals one callback");

	/* Test the sender_pid is PID of the last sender (oneself). */
	system(tt_sprintf("bash -c 'kill -USR1 %ld'", (long)getpid()));
	raise(SIGUSR1);
	ev_sig_handler_call_count = 0;
	ev_sig_handler_expect_pid_self = true;
	fiber_sleep(0);
	fiber_sleep(0);
	is(ev_sig_handler_call_count, 1, "self is last");

	/* Test the sender_pid is PID of the last sender (child). */
	raise(SIGUSR1);
	system(tt_sprintf("bash -c 'kill -USR1 %ld'", (long)getpid()));
	ev_sig_handler_call_count = 0;
	ev_sig_handler_expect_pid_self = false;
	fiber_sleep(0);
	fiber_sleep(0);
	is(ev_sig_handler_call_count, 1, "child is last");

	/* Unset the signal handler. */
	ev_signal_stop(loop(), &ev_sig);

	footer();
	test_result = check_plan();
	return 0;
}

int
main(void)
{
	memory_init();
	fiber_init(fiber_cxx_invoke);

	struct fiber *main = fiber_new_xc("main", main_f);
	fiber_wakeup(main);
	ev_run(loop(), 0);

	fiber_free();
	memory_free();
	return test_result;
}
