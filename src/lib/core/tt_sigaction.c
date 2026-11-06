/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2022, Tarantool AUTHORS, please see AUTHORS file.
 */
#include <assert.h>
#include <pmatomic.h>
#include <pthread.h>
#include <stdbool.h>
#include "tt_sigaction.h"

#define SIGMAX 32

/** Flag is set if main_thread_id variable is initialized. */
static bool main_thread_initialized;
/** Main thread id, it is set on first tt_sigaction call. */
static pthread_t main_thread_id;

/** User signal handlers registered with tt_sigaction. */
static void (*sighandlers[SIGMAX])(int, siginfo_t *, void *);
/**
 * PID of an unhandled signal sender per signal, -1 if none.
 * The -1 value is selected cause sender PID can be 0 on OS X.
 */
static pid_t signal_sender_pid[SIGMAX];

/**
 * Check that signal has been delivered to the main thread
 * and call signal handler or redirect it if thread is not main.
 */
static void
sighandler_dispatcher(int signum, siginfo_t *info, void *ctx)
{
	/*
	 * No guarantee on which signal sender's PID will be logged in case
	 * of high contention is provided, but some sender PID will totally
	 * be logged (and the first logged one will totally be correct PID
	 * of one of signal senders).
	 */
	if (!pthread_equal(pthread_self(), main_thread_id)) {
		pm_atomic_store(&signal_sender_pid[signum], info->si_pid);
		pthread_kill(main_thread_id, signum);
		return;
	}

	/* Use the redirected PID if provided, use our one otherwise. */
	pid_t redirected_pid = pm_atomic_load(&signal_sender_pid[signum]);
	info->si_pid = redirected_pid != -1 ? redirected_pid : info->si_pid;
	assert(sighandlers[signum] != NULL);
	sighandlers[signum](signum, info, ctx);
	pm_atomic_store(&signal_sender_pid[signum], -1);
}

int
tt_sigaction(int signum, struct sigaction *sa)
{
	assert(signum < SIGMAX);
	assert(sa != NULL);

	/* Memorize id of main thread at the first call. */
	if (!main_thread_initialized) {
		main_thread_id = pthread_self();
		main_thread_initialized = true;
	}

	/* Update the corresponding sighandlers entry. */
	if (sa->sa_handler == SIG_DFL || sa->sa_handler == SIG_IGN) {
		sighandlers[signum] = NULL;
	} else {
		assert(sa->sa_flags & SA_SIGINFO);
		assert(sa->sa_sigaction != NULL);
		sighandlers[signum] = sa->sa_sigaction;
		signal_sender_pid[signum] = -1;
		sa->sa_sigaction = sighandler_dispatcher;
	}
	return sigaction(signum, sa, NULL);
}
