#define UNIT_TAP_COMPATIBLE 1
#include "unit.h"

#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <unistd.h>
#include "tt_sigaction.h"
#include "tt_static.h"
#include "clock.h"
#include "pmatomic.h"
#include "trivia/util.h"

#ifdef __linux__
static const bool is_linux = true;
#else
static const bool is_linux = false;
#endif

/*
 * Report a failed handler assertion from signal context. fprintf() and
 * exit() are not async-signal-safe; write a fixed TAP line and leave.
 */
#define HANDLER_FAIL(msg) do {						\
	(void)write(STDOUT_FILENO,					\
		    "TAP version 13\n1..1\nnot ok 1 - " msg "\n",	\
		    sizeof("TAP version 13\n1..1\nnot ok 1 - " msg	\
			   "\n") - 1);					\
	_exit(1);							\
} while (0)

/** Test duration in seconds. */
#define TEST_LEN 1.5
/** Signal sending period in usec. Must be lower than second. */
#define SIGNAL_PERIOD 1e3
/** Number of child threads. */
#define THREADS_NUM 4
/** Sleep interval in microseconds. */
#define USLEEP_INTERVAL 1e5
/** How long handler_wait() waits for the handler to run, in milliseconds. */
#define HANDLER_WAIT_TIMEOUT_MS 5000

static pthread_t main_thread;

static uint32_t false_handle_cnt;

static void *
sleep_thread_f(void *arg)
{
	thread_sleep(TEST_LEN);
	return NULL;
}

static void
timer_handler_f(int signum, siginfo_t *info, void *ctx)
{
	(void)signum;
	(void)info;
	(void)ctx;
	if (!pthread_equal(pthread_self(), main_thread))
		pm_atomic_fetch_add_explicit(&false_handle_cnt, 1,
					     pm_memory_order_relaxed);
}

static int
check_redirection(void)
{
	plan(1);

	/* Create threads. */
	int rc;
	main_thread = pthread_self();
	pthread_t child_threads[THREADS_NUM];
	pthread_attr_t attr;
	rc = pthread_attr_init(&attr);
	fail_if(rc != 0);
	rc = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
	fail_if(rc != 0);
	for (int i = 0; i < THREADS_NUM; ++i) {
		rc = pthread_create(&child_threads[i], &attr,
				    &sleep_thread_f, NULL);
		fail_if(rc != 0);
	}

	/* Set a signal handler. */
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_flags = SA_SIGINFO;
	sa.sa_sigaction = timer_handler_f;
	rc = tt_sigaction(SIGALRM, &sa);
	fail_if(rc != 0);

	/* Start the timer. */
	struct itimerval timer;
	struct timeval resolution = {
		.tv_sec = 0,
		.tv_usec = SIGNAL_PERIOD,
	};
	timer.it_interval = resolution;
	timer.it_value = resolution;
	rc = setitimer(ITIMER_REAL, &timer, NULL);
	fail_if(rc != 0);

	/* Wait for the threads to finish. */
	for (int i = 0; i < THREADS_NUM; ++i) {
		rc = pthread_join(child_threads[i], NULL);
		fail_if(rc != 0);
	}

	/* Stop the timer so that it doesn't interfere with the next tests. */
	memset(&timer, 0, sizeof(timer));
	rc = setitimer(ITIMER_REAL, &timer, NULL);
	fail_if(rc != 0);
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = SIG_IGN;
	rc = tt_sigaction(SIGALRM, &sa);
	fail_if(rc != 0);

	/* Check no user handler called in a non-main thread. */
	uint32_t false_handles = pm_atomic_load_explicit(
		&false_handle_cnt, pm_memory_order_relaxed);
	ok(false_handles == 0,
	   "Child threads haven't executed signal handler");
	return check_plan();
}

/**
 * Handlers write a byte there to tell the main thread that they have run.
 * A pipe is used because write() is async-signal-safe and, unlike POSIX
 * unnamed semaphores, is available on OS X.
 */
static int handler_pipe[2];
static int handler_call_count;
static bool expect_sender_pid_self;

static void
expect_sender_pid_handler_f(int signum, siginfo_t *info, void *ctx)
{
	(void)signum;
	(void)ctx;
	if (signum != SIGUSR1 || info->si_signo != signum)
		HANDLER_FAIL("unexpected signal in the handler");
	if ((info->si_pid == getpid()) != expect_sender_pid_self &&
	    (is_linux || info->si_pid != 0))
		HANDLER_FAIL("unexpected sender PID in the handler");
	handler_call_count++;
	/* Only inform about one call so the pipe contents are predictable. */
	if (handler_call_count == 1 &&
	    write(handler_pipe[1], "", 1) != 1)
		HANDLER_FAIL("handler pipe write failed");
}

/* The number of signals to send from a killer thread. */
static int killer_thread_kill_count;

/* Send signals using POSIX's sh (compatible with macOS and FreeBSD). */
static bool killer_thread_kill_externally;

/* Sends SIGUSR1 signals to its own process. */
static void *
killer_thread_f(void *arg)
{
	/* Ignore all signals in the killer thread. */
	sigset_t set;
	sigfillset(&set);
	pthread_sigmask(SIG_SETMASK, &set, NULL);

	/* Send the requested number of SIGUSR1 signals. */
	if (killer_thread_kill_externally) {
		int rc = system(tt_sprintf("i=1; while [ $i -le %d ]; do"
					   " kill -s USR1 %ld 2> /dev/null;"
					   " i=$((i + 1)); done",
					   killer_thread_kill_count,
					   (long)getpid()));
		fail_unless(rc == 0);
	} else {
		for (int i = 0; i < killer_thread_kill_count; i++)
			kill(getpid(), SIGUSR1);
	}
	return NULL;
}

/**
 * Wait for a handler call. The read can be interrupted by a signal; the
 * wait is bounded so that a signal that never arrives fails the test with
 * the step it was lost at instead of hanging the runner. The failure is a
 * TAP document, so test-run prints it to the job log.
 */
static void
handler_wait(const char *step)
{
	char c;
	struct pollfd pfd = {
		.fd = handler_pipe[0],
		.events = POLLIN,
	};
	int rc;
	do {
		rc = poll(&pfd, 1, HANDLER_WAIT_TIMEOUT_MS);
	} while (rc < 0 && errno == EINTR);
	if (rc == 0) {
		char msg[160];
		int len = snprintf(msg, sizeof(msg),
				   "TAP version 13\n1..1\n"
				   "not ok 1 - no handler call in %d ms "
				   "at %s\n",
				   HANDLER_WAIT_TIMEOUT_MS, step);
		if (len > 0)
			(void)write(STDOUT_FILENO, msg, (size_t)len);
		_exit(1);
	}
	fail_unless(rc == 1);
	do {
		rc = read(handler_pipe[0], &c, 1);
	} while (rc < 0 && errno == EINTR);
	fail_unless(rc == 1);
}

static int
check_sender_pid_singlethread(void)
{
	plan(5);
	header();
	pthread_t killer_thread;
	fail_unless(pipe(handler_pipe) == 0);

	/*
	 * Darwin's system() saves and restores the signal mask with the
	 * process-wide sigprocmask, which XNU applies to every thread. The
	 * killer thread below blocks all signals, so the restore in the
	 * external case would leave that mask on the main thread. Remember
	 * the mask to restore it.
	 */
	sigset_t saved_mask;
	fail_unless(pthread_sigmask(SIG_BLOCK, NULL, &saved_mask) == 0);

	/* Set the signal handler. */
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_flags = SA_SIGINFO;
	sa.sa_sigaction = expect_sender_pid_handler_f;
	fail_unless(tt_sigaction(SIGUSR1, &sa) == 0);

	/* Test raising a signal. */
	handler_call_count = 0;
	expect_sender_pid_self = true;
	raise(SIGUSR1);
	handler_wait("'raise()'");
	is(handler_call_count, 1, "raise()");

	/* Test killing oneself. */
	handler_call_count = 0;
	expect_sender_pid_self = true;
	kill(getpid(), SIGUSR1);
	handler_wait("'kill() by self'");
	is(handler_call_count, 1, "kill() by self");

	/* Test killing by a child process. */
	handler_call_count = 0;
	expect_sender_pid_self = false;
	int rc = system(tt_sprintf("kill -s USR1 %ld", (long)getpid()));
	fail_unless(rc == 0);
	handler_wait("'kill() by child'");
	is(handler_call_count, 1, "kill() by child");

	/*
	 * Test multiple signals from oneself. The number of
	 * callbacks can be fewer because some signals may be
	 * merged at the system level.
	 */
	handler_call_count = 0;
	expect_sender_pid_self = true;
	killer_thread_kill_count = 1024;
	killer_thread_kill_externally = false;
	pthread_create(&killer_thread, NULL, killer_thread_f, NULL);
	fail_unless(pthread_join(killer_thread, NULL) == 0);
	handler_wait("'multiple signals (internal)'");
	ok(handler_call_count > 0 &&
	   handler_call_count <= killer_thread_kill_count,
	   "multiple signals (internal)");

	/*
	 * Test multiple signals from a child process. The number
	 * of callbacks can be fewer because some signals may be
	 * merged at the system level.
	 */
	handler_call_count = 0;
	expect_sender_pid_self = false;
	killer_thread_kill_count = 1024;
	killer_thread_kill_externally = true;
	pthread_create(&killer_thread, NULL, killer_thread_f, NULL);
	fail_unless(pthread_join(killer_thread, NULL) == 0);
	fail_unless(pthread_sigmask(SIG_SETMASK, &saved_mask, NULL) == 0);
	handler_wait("'multiple signals (external)'");
	ok(handler_call_count > 0 &&
	   handler_call_count <= killer_thread_kill_count,
	   "multiple signals (external)");

	/* Unset the signal handler. */
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = SIG_DFL;
	fail_unless(tt_sigaction(SIGUSR1, &sa) == 0);
	close(handler_pipe[0]);
	close(handler_pipe[1]);

	footer();
	return check_plan();
}

static int matched_pid_count;

static void
multithread_handler_f(int signum, siginfo_t *info, void *ctx)
{
	(void)signum;
	(void)ctx;
	if (signum != SIGUSR1 || info->si_signo != signum)
		HANDLER_FAIL("unexpected signal in the handler");
	if ((info->si_pid == getpid()) == expect_sender_pid_self ||
	    /* Handle possible coalescing on OS X and *BSD. */
	    (!is_linux && info->si_pid == 0))
		matched_pid_count++;
	handler_call_count++;
	if (handler_call_count == 1) {
		/* The first received PID must be valid. */
		if (matched_pid_count == 0)
			HANDLER_FAIL("unexpected sender PID in the handler");
		/* See the write in `expect_sender_pid_handler_f'. */
		if (write(handler_pipe[1], "", 1) != 1)
			HANDLER_FAIL("handler pipe write failed");
	}
}

static void *
infinite_sleep_thread_f(void *arg)
{
	(void)arg;
	for (;;)
		thread_sleep(0.1);
	return NULL;
}

static int
check_sender_pid_multithread(void)
{
	plan(1);
	header();
	fail_unless(pipe(handler_pipe) == 0);

	/* Set the signal handler. */
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_flags = SA_SIGINFO;
	sa.sa_sigaction = multithread_handler_f;
	fail_unless(tt_sigaction(SIGUSR1, &sa) == 0);

	/* Start threads. */
	pthread_t child_threads[THREADS_NUM];
	for (int i = 0; i < THREADS_NUM; ++i) {
		fail_unless(pthread_create(&child_threads[i], NULL,
					   infinite_sleep_thread_f, NULL) == 0);
	}

	/* Test killing threads. */
	int total_handler_call_count = 0;
	expect_sender_pid_self = true;
	for (int i = 0; i < THREADS_NUM; ++i) {
		handler_call_count = 0;
		matched_pid_count = 0;
		fail_unless(pthread_kill(child_threads[i], SIGUSR1) == 0);
		handler_wait("'pthread_kill() each thread'");
		total_handler_call_count += handler_call_count;
	}
	is(total_handler_call_count, THREADS_NUM, "pthread_kill() each thread");

	/* Unset the signal handler. */
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = SIG_DFL;
	fail_unless(tt_sigaction(SIGUSR1, &sa) == 0);
	close(handler_pipe[0]);
	close(handler_pipe[1]);

	footer();
	return check_plan();
}

static pid_t expected_redirected_sender_pid;

static void
redirected_sender_handler_f(int signum, siginfo_t *info, void *ctx)
{
	(void)ctx;
	if (signum != SIGUSR1 || info->si_signo != signum)
		HANDLER_FAIL("unexpected signal in the handler");
	if (info->si_pid != expected_redirected_sender_pid &&
	    (is_linux || info->si_pid != 0))
		HANDLER_FAIL("unexpected redirected sender PID");
	handler_call_count++;
	if (handler_call_count == 1 &&
	    write(handler_pipe[1], "", 1) != 1)
		HANDLER_FAIL("handler pipe write failed");
}

/*
 * A process-directed SIGUSR1 from a separate process: the main thread
 * blocks the signal, so it is delivered to a child thread, whose dispatcher
 * forwards the sender PID to the main thread. The handler runs on main only
 * after the test unblocks the signal, and it receives the sender's PID, not
 * getpid().
 */
static int
check_sender_pid_redirected_from_process(void)
{
	plan(2);
	header();
	fail_unless(pipe(handler_pipe) == 0);

	/* Set the signal handler. */
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_flags = SA_SIGINFO;
	sa.sa_sigaction = redirected_sender_handler_f;
	fail_unless(tt_sigaction(SIGUSR1, &sa) == 0);

	/* Create the child threads first: they inherit the unblocked mask. */
	pthread_t child_threads[THREADS_NUM];
	for (int i = 0; i < THREADS_NUM; ++i) {
		fail_unless(pthread_create(&child_threads[i], NULL,
					   infinite_sleep_thread_f, NULL) == 0);
	}

	/*
	 * Main blocks the signal, so the process-directed signal goes to a
	 * child thread.
	 */
	sigset_t set;
	sigemptyset(&set);
	sigaddset(&set, SIGUSR1);
	fail_unless(pthread_sigmask(SIG_BLOCK, &set, NULL) == 0);

	/* Send the signal from a separate process. */
	pid_t sender = fork();
	fail_unless(sender >= 0);
	if (sender == 0) {
		/* The signal sender: report failure as a non-zero exit. */
		_exit(kill(getppid(), SIGUSR1) == 0 ? 0 : 1);
	}
	expected_redirected_sender_pid = sender;
	int status;
	pid_t rc;
	do {
		rc = waitpid(sender, &status, 0);
	} while (rc == -1 && errno == EINTR);
	fail_unless(rc == sender);
	fail_unless(WIFEXITED(status) && WEXITSTATUS(status) == 0);

	/* The redirect is pending in main; unblock it to run the handler. */
	handler_call_count = 0;
	fail_unless(pthread_sigmask(SIG_UNBLOCK, &set, NULL) == 0);
	handler_wait("'redirected signal from a process'");
	is(handler_call_count, 1, "redirected signal from a process");

	/*
	 * A signal delivered directly to the main thread after the
	 * redirected one must use its own sender, not the stale slot.
	 */
	expected_redirected_sender_pid = getpid();
	handler_call_count = 0;
	raise(SIGUSR1);
	handler_wait("'direct signal after a redirect'");
	is(handler_call_count, 1, "direct signal after a redirect");

	/* Unset the signal handler. */
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = SIG_DFL;
	fail_unless(tt_sigaction(SIGUSR1, &sa) == 0);
	close(handler_pipe[0]);
	close(handler_pipe[1]);

	footer();
	return check_plan();
}

int
main(void)
{
	plan(4);

	check_redirection();
	check_sender_pid_singlethread();
	check_sender_pid_multithread();
	check_sender_pid_redirected_from_process();

	return check_plan();
}
