/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2022, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once
#include <signal.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Set signal handler with the guarantee that it will be delivered
 * to the main thread (before that, it can also be delivered to
 * other threads) and executed only on the main thread.
 */
int
tt_sigaction(int signum, struct sigaction *sa, struct sigaction *osa);

#ifdef __cplusplus
} /* extern "C" */
#endif
