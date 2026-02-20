/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2022, Tarantool AUTHORS, please see AUTHORS file.
 */
#include <signal.h>
#include <string.h>
#include <sys/time.h>

#include "clock.h"
#include "clock_lowres.h"
#include "say.h"

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
	(void)signum;
	clock_lowres_monotonic_clock_value = clock_monotonic();
}

void
clock_lowres_signal_init(void)
{
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
