#include "unit.h"

#include <pthread.h>
#include <string.h>
#include <sys/time.h>
#include "tt_sigaction.h"
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
thread_f(void *arg)
{
	thread_sleep(TEST_LEN);
	return NULL;
}

static void
handler_f(int signum)
{
	(void)signum;
	if (!pthread_equal(pthread_self(), main_thread))
		pm_atomic_fetch_add_explicit(&false_handle_cnt, 1,
					     pm_memory_order_relaxed);
}

int
main(void)
{
	plan(1);

	int rc;
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = handler_f;
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
		rc = pthread_create(&child_threads[i], &attr, &thread_f, NULL);
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
