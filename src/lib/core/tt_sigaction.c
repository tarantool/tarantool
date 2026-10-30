/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2022, Tarantool AUTHORS, please see AUTHORS file.
 */
#include <assert.h>
#include <pthread.h>
#include <stdbool.h>
#include "tt_sigaction.h"

#define SIGMAX 32

/** Flag is set if main_thread_id variable is initialized. */
static bool main_thread_initialized;
/** Main thread id, it is set on first tt_sigaction call. */
static pthread_t main_thread_id;

static void (*sighandlers[SIGMAX])(int signum);

/**
 * Check that signal has been delivered to the main thread
 * and call signal handler or redirect it if thread is not main.
 */
static void
sighandler_dispatcher(int signum)
{
	if (!pthread_equal(pthread_self(), main_thread_id)) {
		pthread_kill(main_thread_id, signum);
		return;
	}
	assert(sighandlers[signum] != NULL);
	sighandlers[signum](signum);
}

int
tt_sigaction(int signum, struct sigaction *sa, struct sigaction *osa)
{
	assert(signum < SIGMAX);
	assert(sa != NULL);

	/* Memorize id of main thread at the first call. */
	if (!main_thread_initialized) {
		main_thread_id = pthread_self();
		main_thread_initialized = true;
	}

	void (*old_handler)(int) = sighandlers[signum];
	if (sa->sa_handler == SIG_DFL || sa->sa_handler == SIG_IGN) {
		sighandlers[signum] = NULL;
	} else {
		sighandlers[signum] = sa->sa_handler;
		sa->sa_handler = sighandler_dispatcher;
	}
	int rc = sigaction(signum, sa, osa);
	if (osa != NULL && old_handler != NULL)
		osa->sa_handler = old_handler;
	return rc;
}
