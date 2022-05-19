/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2022, Tarantool AUTHORS, please see AUTHORS file.
 */
#include <signal.h>
#include <string.h>
#include <say.h>
#include <pthread.h>
#include "clock.h"
#include "clock_lowres.h"

#if defined(__APPLE__) || !defined(NDEBUG)
/**
 * A thread that initialized this module. Only owner thread
 * is allowed to handle SIGALRM and use methods from this module.
 */
static pthread_t owner;

/** Check if current thread is owner. */
bool
clock_lowres_thread_is_owner(void)
{
	return pthread_equal(owner, pthread_self());
}
#endif

/** Resolution of clock (clock update period). */
static const struct timeval resolution = {
	.tv_sec = 0,
	.tv_usec = 10 * 1e3,
};

double
clock_lowres_resolution(void)
{
	return (double)resolution.tv_sec + resolution.tv_usec / 1e6;
}

/** Monotonic time, updated once every resolution of time unit. */
double clock_lowres_monotonic_clock_value = 0.0;

/** A tick of clock_lowres, SIGALRM handler. */
static void
clock_lowres_tick(int signum)
{
#ifdef __APPLE__
	/**
	 * We cannot guarantee that the signal is delivered
	 * only to owner thread on MacOS. So use this workaround.
	 * https://github.com/tarantool/tarantool/issues/7206
	 */
	if (!clock_lowres_thread_is_owner()) {
		pthread_kill(owner, signum);
		return;
	}
#else
	(void)signum;
	assert(clock_lowres_thread_is_owner());
#endif
	clock_lowres_monotonic_clock_value = clock_monotonic();
}

void
clock_lowres_signal_init(void)
{
#if defined(__APPLE__) || !defined(NDEBUG)
	owner = pthread_self();
#endif
	clock_lowres_monotonic_clock_value = clock_monotonic();
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = clock_lowres_tick;
	sa.sa_flags = SA_RESTART;
	if (sigaction(SIGALRM, &sa, NULL) == -1)
		panic_syserror("cannot set low resolution clock timer signal");

	struct itimerval timer;
	timer.it_interval = resolution;
	timer.it_value = resolution;
	if (setitimer(ITIMER_REAL, &timer, NULL) == -1)
		panic_syserror("cannot set low resolution clock timer");
}

void
clock_lowres_signal_reset(void)
{
	struct itimerval timer;
	memset(&timer, 0, sizeof(timer));
	if (setitimer(ITIMER_REAL, &timer, NULL) == -1)
		say_syserror("cannot reset low resolution clock timer");

	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = SIG_DFL;
	if (sigaction(SIGALRM, &sa, NULL) == -1)
		say_syserror("cannot reset low resolution clock timer signal");
}
