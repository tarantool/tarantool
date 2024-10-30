/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2024, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include "trivia/util.h"

#include <stdbool.h>

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

/** \cond public */

typedef void
(*tnt_tx_func_f)(void *);

/**
 * Schedule the given callback to be executed in TX thread with the provided
 * argument. In order for the messages to be actually sent to TX thread the user
 * must call tnt_tx_flush() in the same thread as the pushes.
 *
 * In TX thread the callbacks are guaranteed to start execution in the same
 * order as the push, but the order of completion is undefined if they are
 * yielding, since they get executed potentially in different fibers.
 *
 * The callbacks are handled by an internal fiber pool running in TX thread.
 * The pool has a limited size. In case the callbacks are yielding and the user
 * wants to execute more of them concurrently than the default size of the fiber
 * pool, then the size can be extended (or reduced) using
 * fiber.tx_user_pool_size() Lua API.
 *
 * If called during Tarantool shutdown, the behaviour is undefined. The external
 * threads must be terminated before that.
 *
 * If called in TX thread, the behaviour is undefined.
 *
 * The function relies on `thread_local` C++ data to have properly working
 * destructors and constructors and won't be suitable for any other runtime.
 */
API_EXPORT void
tnt_tx_push(tnt_tx_func_f func, void *arg);

/**
 * Send all the pending callbacks of this thread to TX thread. Note, that it
 * doesn't guarantee that they are already executed when this function returns.
 * They are only sent to TX thread, not called yet. It is the caller's
 * responsibility to ensure that the messages are not being sent faster than TX
 * thread is handling them. Otherwise the queue in TX thread grows faster than
 * shrinks and could lead to unpredictable latency and even OOM.
 *
 * Note, that push is very cheap while flush is relatively expensive both for
 * this thread and for TX thread. Avoid calling it on each push if possible.
 */
API_EXPORT void
tnt_tx_flush(void);

/** \endcond public */

void
tnt_thread_init(void);

void
tnt_thread_set_tx_user_pool_size(int size);

int
tnt_thread_get_tx_user_pool_size(void);

void
tnt_thread_shutdown(void);

void
tnt_thread_free(void);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */
