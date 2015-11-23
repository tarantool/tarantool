#ifndef TARANTOOL_COEIO_H_INCLUDED
#define TARANTOOL_COEIO_H_INCLUDED
/*
 * Copyright 2010-2015, Tarantool AUTHORS, please see AUTHORS file.
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

#include "third_party/tarantool_eio.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

/**
 * Asynchronous IO Tasks (libeio wrapper)
 *
 * Yield the current fiber until a created task is complete.
 */

void coeio_init(void);
void coeio_reinit(void);

struct coio_task;

typedef ssize_t (*coio_task_cb)(struct coio_task *task); /* like eio_req */
typedef ssize_t (*coio_call_cb)(va_list ap);

/**
 * A single task context.
 */
struct coio_task {
	struct eio_req base; /* eio_task - must be first */
	/** The calling fiber. */
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
};

/** \cond public */

/**
 * Create new eio task with specified function and
 * arguments. Yield and wait until the task is complete
 * or a timeout occurs.
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
 *	if (coio_call(openfile_cb, 0.10, "/tmp/file", 0) == -1)
 *		// handle errors.
 *	...
 * @endcode
 */
ssize_t
coio_call(ssize_t (*func)(va_list), ...);

struct addrinfo;

/**
 * Fiber-friendly version of getaddrinfo(3).
 * \sa getaddrinfo().
 */
int
coio_getaddrinfo(const char *host, const char *port,
		 const struct addrinfo *hints, struct addrinfo **res,
		 double timeout);
/** \endcond public */

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_COEIO_H_INCLUDED */
