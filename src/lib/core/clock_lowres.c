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

/**
 * Granularity in seconds.
 */

static const struct timeval LOW_RES_GRANULARITY = {
	.tv_sec = 0,
	.tv_usec = 10,
};

double low_res_monotonic_clock = 0.0;

pthread_t owner;

bool
clock_lowres_thread_is_owner(void)
{
	return pthread_self() == owner;
}

/** A tick of low_res_clock, SIGALRM handler. */
static void
clock_monotonic_lowres_tick(int signum)
{
	(void)signum;
	assert(clock_lowres_thread_is_owner());
	if (!clock_lowres_thread_is_owner()) {
		say_error("Clock lowres tick handled by another thread");
		abort();
	}
	low_res_monotonic_clock = clock_monotonic();
}

void
clock_lowres_signal_init(void)
{
	owner = pthread_self();
	low_res_monotonic_clock = clock_monotonic();
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = clock_monotonic_lowres_tick;
	sa.sa_flags = SA_RESTART;
	if (sigaction(SIGALRM, &sa, NULL) == -1)
		panic_syserror("cannot set low resolution clock timer signal");

	struct itimerval timer;
	timer.it_interval = LOW_RES_GRANULARITY;
	timer.it_value = LOW_RES_GRANULARITY;
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
	if (sigaction(SIGALRM, &sa, 0) == -1)
		say_syserror("cannot reset low resolution clock timer signal");
}
