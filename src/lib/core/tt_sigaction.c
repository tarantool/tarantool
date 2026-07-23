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

static void (*sighandlers[SIGMAX])(int, siginfo_t *, void *);
static pid_t signal_sender_pid[SIGMAX];

/**
 * Check that signal has been delivered to the main thread
 * and call signal handler or redirect it if thread is not main.
 */
static void
sighandler_dispatcher(int signum, siginfo_t *info, void *ctx)
{
	/* Sync point - common atomic operation for main and other threads. */
	pid_t uninitialized_sender_pid = 0;
	bool success = pm_atomic_compare_exchange_strong(
			&signal_sender_pid[signum],
			&uninitialized_sender_pid, info->si_pid);
	if (!pthread_equal(pthread_self(), main_thread_id)) {
		/*
		 * Redirect the signal sender PID in case it was uninitialized.
		 *
		 * Effectively this means that, in case if there was a number of
		 * signals received, only the PID of the first signal sender is
		 * redirected to the main cord.
		 */
		if (success)
			pthread_kill(main_thread_id, signum);
		return;
	} else {
		/*
		 * We're in a critical section: no handler will change the
		 * redirected sender ID until we reset it to 0 below.
		 *
		 * If we've succeeed, that means no redirection happened before
		 * and we set the signal_sender_pid[signum] to our si_pid. If we
		 * have failed, the signal_sender_pid[signum] contains the PID
		 * of the original redirected signal sender.
		 *
		 * Either way, the signal_sender_pid[signum] is the PID we need
		 * to pass to the user signal handler.
		 */
		info->si_pid = signal_sender_pid[signum];
		assert(sighandlers[signum] != NULL);
		sighandlers[signum](signum, info, ctx);
		signal_sender_pid[signum] = 0;
	}
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

	void *old_handler = sighandlers[signum];
	if (sa->sa_handler == SIG_DFL || sa->sa_handler == SIG_IGN) {
		sighandlers[signum] = NULL;
	} else {
		assert(sa->sa_flags | SA_SIGINFO);
		assert(sa->sa_sigaction != NULL);
		sighandlers[signum] = sa->sa_sigaction;
		sa->sa_sigaction = sighandler_dispatcher;
	}
	int rc = sigaction(signum, sa, osa);
	if (osa != NULL && old_handler != NULL) {
		if (old_handler == SIG_DFL || old_handler == SIG_IGN) {
			osa->sa_handler = old_handler;
		} else {
			assert(osa->sa_flags & SA_SIGINFO);
			osa->sa_sigaction = old_handler;
		}
	}
	return rc;
}
