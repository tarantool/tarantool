#ifndef TARANTOOL_LIB_CORE_COIO_TASK_H_INCLUDED
#define TARANTOOL_LIB_CORE_COIO_TASK_H_INCLUDED
/*
 * Copyright 2010-2016, Tarantool AUTHORS, please see AUTHORS file.
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

#include "trivia/config.h"

#include <sys/types.h> /* ssize_t */
#include <stdarg.h>

#include <tarantool_eio.h>
#include "diag.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

/**
 * Asynchronous IO Tasks (libeio wrapper)
 *
 * Yield the current fiber until a created task is complete.
 */

void coio_init(void);
void coio_enable(void);
void coio_shutdown(void);

struct coio_task;

typedef ssize_t (*coio_call_cb)(va_list ap);
typedef int (*coio_task_cb)(struct coio_task *task); /* like eio_req */

/**
 * A single task context.
 */
struct coio_task {
	struct eio_req base; /* eio_task - must be first */
	/**
	 * The calling fiber. When set to NULL, the task is
	 * detached - its resources are freed eventually, and such
	 * a task should not be accessed after detachment.
	 */
	struct fiber *fiber;
	/** Callbacks. */
	union {
		struct { /* coio_task() */
			coio_task_cb task_cb;
			coio_task_cb timeout_cb;
		};
		struct { /* coio_call() */
			coio_call_cb call_cb;
			va_list ap;
		};
	};
	/** Callback results. */
	int complete;
	/** Task diag **/
	struct diag diag;
};

/**
 * Create coio_task.
 *
 * @param task coio task
 * @param func a callback to execute in EIO thread pool.
 * @param on_timeout a callback to execute on timeout
 */
void
coio_task_create(struct coio_task *task, coio_task_cb func,
		 coio_task_cb on_timeout);

/**
 * Destroy coio task.
 *
 * @param task coio task.
 */
void
coio_task_destroy(struct coio_task *task);

/**
 * Execute a coio task in a worker thread.
 *
 * @param task coio task.
 * @param timeout timeout in seconds.
 * @retval 0  the task completed successfully. Check the result
 *            code in task->base.result and free the task.
 * @retval -1 timeout or the waiting fiber was cancelled (check diag);
 *            the caller should not free the task, it
 *            will be freed when it's finished in the timeout
 *            callback.
 */
int
coio_task_execute(struct coio_task *task, double timeout);

/**
 * Post a task in detached state. Its result can't be obtained,
 * and destructor is called after completion.
 */
void
coio_task_post(struct coio_task *task);

/** \cond public */

/**
 * Create new eio task with specified function and
 * arguments. Yield and wait until the task is complete.
 *
 * This function doesn't throw exceptions to avoid double error
 * checking: in most cases it's also necessary to check the return
 * value of the called function and perform necessary actions. If
 * func sets errno, the errno is preserved across the call.
 *
 * @retval -1 and errno = ENOMEM if failed to create a task
 * @retval the function return (errno is preserved).
 *
 * @code
 *	static ssize_t openfile_cb(va_list ap)
 *	{
 *	         const char *filename = va_arg(ap);
 *	         int flags = va_arg(ap);
 *	         return open(filename, flags);
 *	}
 *
 *	if (coio_call(openfile_cb, "/tmp/file", 0) == -1)
 *		// handle errors.
 *	...
 * @endcode
 */
ssize_t
coio_call(ssize_t (*func)(va_list), ...);

struct addrinfo;

/**
 * Fiber-friendly version of getaddrinfo(3).
 *
 * @param host host name, i.e. "tarantool.org"
 * @param port service name, i.e. "80" or "http"
 * @param hints hints, see getaddrinfo(3)
 * @param res[out] result, see getaddrinfo(3)
 * @param timeout timeout
 * @retval  0 on success, please free @a res using freeaddrinfo(3).
 * @retval -1 on error, check diag.
 *            Please note that the return value is not compatible with
 *            getaddrinfo(3).
 * @sa getaddrinfo()
 */
int
coio_getaddrinfo(const char *host, const char *port,
		 const struct addrinfo *hints, struct addrinfo **res,
		 double timeout);
/** \endcond public */

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_LIB_CORE_COIO_TASK_H_INCLUDED */
