#pragma once
/*
 * Copyright 2010-2021, Tarantool AUTHORS, please see AUTHORS file.
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 *
 * 1. Redistributions of source code must retain the above
 *    copyright notice, this list of conditions and the
 *    following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDER> ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * <COPYRIGHT HOLDER> OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include <trivia/util.h>

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

/** \cond public */

/**
 * Function, which registers or deletes on_shutdown handler.
 * @param[in] arg on_shutdown function's argument.
 * @param[in] new_handler New on_shutdown handler, in
 *            case this argument is NULL, function finds
 *            and destroys old on_shutdown handler.
 * @param[in] old_handler Old on_shutdown handler.
 * @retval return 0 if success otherwise return -1 and sets
 *                  errno. There are three cases when
 *                  function fails:
 *                  - both old_handler and new_handler are equal to
 *                    zero (sets errno to EINVAL).
 *                  - old_handler != NULL, but there is no trigger
 *                    with such function (sets errno to EINVAL).
 *                  - malloc for some internal struct memory allocation
 *                    return NULL (errno sets by malloc to ENOMEM).
 */
API_EXPORT int
box_on_shutdown(void *arg, int (*new_handler)(void *),
		int (*old_handler)(void *));

/** \endcond public */

/**
 * Runs triggers from box_on_shutdown_trigger_list and on_shutdown event in
 * separate fibers. Waits for their completion for on_shutdown_trigger_timeout
 * seconds. When time is over, the function immediately stops without waiting
 * for other triggers completion and returns a TimedOut error.
 * NB: the function removes all elements from box_on_shutdown_trigger_list.
 */
int
on_shutdown_run_triggers(void);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

