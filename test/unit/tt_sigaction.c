#define UNIT_TAP_COMPATIBLE 1
#include "unit.h"

#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <string.h>
#include <sys/time.h>
#include "tt_sigaction.h"
#include "tt_static.h"
#include "clock.h"
#include "pmatomic.h"
#include "trivia/util.h"

/** Test duration in seconds. */
#define TEST_LEN 1.5
/** Signal sending period in usec. Must be lower than second. */
#define SIGNAL_PERIOD 1e3
/** Number of child threads. */
#define THREADS_NUM 4
/** Sleep interval in microseconds. */
#define USLEEP_INTERVAL 1e5

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

	int rc;
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_flags = SA_SIGINFO;
	sa.sa_sigaction = timer_handler_f;
	rc = tt_sigaction(SIGALRM, &sa, NULL);
	fail_if(rc != 0);

	struct itimerval timer;
	struct timeval resolution = {
		.tv_sec = 0,
		.tv_usec = SIGNAL_PERIOD,
	};
	timer.it_interval = resolution;
	timer.it_value = resolution;
	rc = setitimer(ITIMER_REAL, &timer, NULL);
	fail_if(rc != 0);

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

	for (int i = 0; i < THREADS_NUM; ++i) {
		rc = pthread_join(child_threads[i], NULL);
		fail_if(rc != 0);
	}

	uint32_t false_handles = pm_atomic_load_explicit(
		&false_handle_cnt, pm_memory_order_relaxed);
	ok(false_handles == 0,
	   "Child threads haven't executed signal handler");
	return check_plan();
}

static int handler_call_count;
static bool expect_sender_pid_self;

static void
expect_sender_pid_handler_f(int signum, siginfo_t *info, void *ctx)
{
	(void)signum;
	(void)ctx;
	fail_unless(signum == SIGUSR1);
	fail_unless(info->si_signo == signum);
	fail_unless(info->si_pid != 0);
	fail_unless((info->si_pid == getpid()) == expect_sender_pid_self);
	handler_call_count++;
}

/* The amount of signals to send for a killer thread. */
static int killer_thread_kill_count;

/* Send signals using bash. */
static bool killer_thread_kill_externally;

/* Sends SIGUSR1 signals to own process. */
static void *
killer_thread_f(void *arg)
{
	/* Ignore all signals in the killer thread. */
	sigset_t set;
	sigfillset(&set);
	pthread_sigmask(SIG_SETMASK, &set, NULL);

	/* Send the amount of SIGUSR1s requested. */
	if (killer_thread_kill_externally) {
		system(tt_sprintf("bash -c 'for i in {1..%d};"
				  " do kill -USR1 %ld 2> /dev/null; done'",
				  killer_thread_kill_count, (long)getpid()));
	} else {
		for (int i = 0; i < killer_thread_kill_count; i++)
			kill(getpid(), SIGUSR1);
	}
	return NULL;
}

static int
check_sender_pid_singlethread()
{
	plan(5);
	header();
	pthread_t killer_thread;

	/* Set the signal handler. */
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_flags = SA_SIGINFO;
	sa.sa_sigaction = expect_sender_pid_handler_f;
	fail_unless(tt_sigaction(SIGUSR1, &sa, NULL) == 0);

	/* Test raising a signal. */
	handler_call_count = 0;
	expect_sender_pid_self = true;
	raise(SIGUSR1);
	is(handler_call_count, 1, "raise()");

	/* Test killing oneself. */
	handler_call_count = 0;
	expect_sender_pid_self = true;
	kill(getpid(), SIGUSR1);
	is(handler_call_count, 1, "kill() by self");

	/* Test killing by a child process. */
	handler_call_count = 0;
	expect_sender_pid_self = false;
	system(tt_sprintf("bash -c 'kill -USR1 %ld'", (long)getpid()));
	is(handler_call_count, 1, "kill() by child");

	/*
	 * Test multiple signals from oneself. The amount of callbacks called
	 * is expected to be be less as some signals are merged on the system
	 * level.
	 */
	handler_call_count = 0;
	expect_sender_pid_self = true;
	killer_thread_kill_count = 1024;
	killer_thread_kill_externally = false;
	pthread_create(&killer_thread, NULL, killer_thread_f, NULL);
	fail_unless(pthread_join(killer_thread, NULL) == 0);
	ok(handler_call_count < killer_thread_kill_count,
	   "multiple signals (internal)");

	/*
	 * Test multiple signals from a child process. The amount of callbacks
	 * called can be less as some signals are probably merged on the system
	 * level.
	 */
	handler_call_count = 0;
	expect_sender_pid_self = false;
	killer_thread_kill_count = 1024;
	killer_thread_kill_externally = true;
	pthread_create(&killer_thread, NULL, killer_thread_f, NULL);
	fail_unless(pthread_join(killer_thread, NULL) == 0);
	ok(handler_call_count <= killer_thread_kill_count,
	   "multiple signals (external)");

	/* Unset the signal handler. */
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = SIG_DFL;
	fail_unless(tt_sigaction(SIGUSR1, &sa, NULL) == 0);

	footer();
	return check_plan();
}

sem_t sem;

static void
sem_handler_f(int signum, siginfo_t *info, void *ctx)
{
	(void)signum;
	(void)ctx;
	fail_unless(signum == SIGUSR1);
	fail_unless(info->si_signo == signum);
	fail_unless(info->si_pid != 0);
	fail_unless((info->si_pid == getpid()) == expect_sender_pid_self);
	handler_call_count++;
	sem_post(&sem);
}

static void *
testcancel_thread_f(void *arg)
{
	/* Wait for pthread_cancel. */
	for (;;) {
		thread_sleep(0.1);
		pthread_testcancel();
	}
	return NULL;
}

static int
check_sender_pid_muiltithread()
{
	plan(3);
	header();
	pthread_t killer_thread;

	/* Test threads. */
	sem_init(&sem, 0, 0);
	pthread_t child_threads[THREADS_NUM];
	for (int i = 0; i < THREADS_NUM; ++i) {
		fail_unless(pthread_create(&child_threads[i], NULL,
					   testcancel_thread_f, NULL) == 0);
	}

	/* Set the signal handler. */
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_flags = SA_SIGINFO;
	sa.sa_sigaction = sem_handler_f;
	fail_unless(tt_sigaction(SIGUSR1, &sa, NULL) == 0);

	/* Test killing threads. */
	handler_call_count = 0;
	expect_sender_pid_self = true;
	for (int i = 0; i < THREADS_NUM; ++i) {
		fail_unless(pthread_kill(child_threads[i], SIGUSR1) == 0);
		errno = 0;
		while (sem_wait(&sem) != 0 && errno == EINTR)
			errno = 0;
		fail_unless(errno == 0);
	}
	is(handler_call_count, THREADS_NUM, "pthread_kill() each thread");

	/*
	 * Test multiple signals from oneself. The amount of callbacks called
	 * is expected to be be less as some signals are merged on the system
	 * and the tt_sigaction level (during redirection).
	 */
	handler_call_count = 0;
	expect_sender_pid_self = true;
	killer_thread_kill_count = 1024;
	killer_thread_kill_externally = false;
	pthread_create(&killer_thread, NULL, killer_thread_f, NULL);
	fail_unless(pthread_join(killer_thread, NULL) == 0);
	ok(handler_call_count < killer_thread_kill_count,
	   "multiple signals (internal)");

	/*
	 * Test multiple signals from a child process. The amount of callbacks
	 * called can be less as some signals are probably merged on the system
	 * and the tt_sigaction level (during redirection).
	 */
	handler_call_count = 0;
	expect_sender_pid_self = false;
	killer_thread_kill_count = 1024;
	killer_thread_kill_externally = true;
	pthread_create(&killer_thread, NULL, killer_thread_f, NULL);
	fail_unless(pthread_join(killer_thread, NULL) == 0);
	ok(handler_call_count <= killer_thread_kill_count,
	   "multiple signals (external)");

	/* Unset the signal handler. */
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = SIG_DFL;
	fail_unless(tt_sigaction(SIGUSR1, &sa, NULL) == 0);

	/* Wait for the test threads. */
	for (int i = 0; i < THREADS_NUM; ++i) {
		fail_unless(pthread_cancel(child_threads[i]) == 0);
		fail_unless(pthread_join(child_threads[i], NULL) == 0);
	}

	footer();
	return check_plan();
}

int
main(void)
{
	plan(3);

	check_redirection();
	check_sender_pid_singlethread();
	check_sender_pid_muiltithread();

	return check_plan();
}
