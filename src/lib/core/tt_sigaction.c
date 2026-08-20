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
	/*
	 * Only update the signal sender PID if it's uninitialized.
	 *
	 * Effectively that means that in case of several incoming signals,
	 * the first handled one wins.
	 *
	 * This CAS is not enough in case there's a number of concurrently
	 * rinning signal handlers. In case a non-main thread successes with
	 * the CAS, it redirects the PID to the main thread using posix_kill,
	 * but the concurrently running main thread could handle the PID
	 * rightaway, so the handler that will be invoked by the phread_kill
	 * will run with PID equal to 0 and errouneously inform the callback
	 * that the signal came from the Tarantool itself. Luckily, the first
	 * signal received is handled correctly and the callback called first
	 * will get the right PID.
	 *
	 * We can't be ideally valid without using 16-byte atomic operations
	 * anyways, so let's be simple and just do a regular CAS. We don't
	 * expect having a lot of signals sent in such a short period of time
	 * that the user gets a wrong PID. Also, the first PID he gets will be
	 * valid, so it's still OK.
	 */
	pid_t uninitialized = 0;
	bool success = pm_atomic_compare_exchange_strong(
			&signal_sender_pid[signum],
			&uninitialized, info->si_pid);

	if (!pthread_equal(pthread_self(), main_thread_id)) {
		/* Successfully updated the sender PID - redirect it. */
		if (success)
			pthread_kill(main_thread_id, signum);
		return;
	}

	/* This is either info->si_pid or a redirected value. */
	info->si_pid = signal_sender_pid[signum];
	assert(sighandlers[signum] != NULL);
	sighandlers[signum](signum, info, ctx);
	signal_sender_pid[signum] = 0; /* Zero means handled. */
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
