/*
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
#include "coeio.h"
#include "fiber.h"
#include "exception.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Asynchronous IO Tasks (libeio wrapper).
 * ---------------------------------------
 *
 * libeio request processing is designed in edge-trigger
 * manner, when libeio is ready to process some requests it
 * calls coeio_poller callback.
 *
 * Due to libeio design, want_poll callback is called while
 * locks are being held, so it's not possible to call any libeio
 * function inside this callback. Thus coeio_want_poll raises an
 * async event which will be dealt with normally as part of the
 * main Tarantool event loop.
 *
 * The async event handler, in turn, performs eio_poll(), which
 * will run on_complete callback for all ready eio tasks.
 * In case if some of the requests are not complete by the time
 * eio_poll() has been called, coeio_idle watcher is started, which
 * would periodically invoke eio_poll() until all requests are
 * complete.
 *
 * See for details:
 * http://pod.tst.eu/http://cvs.schmorp.de/libeio/eio.pod
*/

struct coeio_manager {
	ev_loop *loop;
	ev_idle coeio_idle;
	ev_async coeio_async;
} coeio_manager;

static void
coeio_idle_cb(ev_loop *loop, struct ev_idle *w, int /* events */)
{
	if (eio_poll() != -1) {
		/* nothing to do */
		ev_idle_stop(loop, w);
	}
}

static void
coeio_async_cb(ev_loop *loop, struct ev_async *w __attribute__((unused)),
	       int events __attribute__((unused)))
{
	if (eio_poll() == -1) {
		/* not all tasks are complete. */
		ev_idle_start(loop, &coeio_manager.coeio_idle);
	}
}

static void
coeio_want_poll_cb(void)
{
	ev_async_send(coeio_manager.loop, &coeio_manager.coeio_async);
}

/**
 * Init coeio subsystem.
 *
 * Create idle and async watchers, init eio.
 */
void
coeio_init(void)
{
	eio_init(coeio_want_poll_cb, NULL);
	coeio_manager.loop = loop();

	ev_idle_init(&coeio_manager.coeio_idle, coeio_idle_cb);
	ev_async_init(&coeio_manager.coeio_async, coeio_async_cb);

	ev_async_start(loop(), &coeio_manager.coeio_async);
}

/**
 * ReInit coeio subsystem (for example after 'fork')
 */
void
coeio_reinit(void)
{
	eio_init(coeio_want_poll_cb, NULL);
}

/**
 * A single task context.
 */
struct coeio_task {
	/** The calling fiber. */
	struct fiber *fiber;
	/** The callback. */
	ssize_t (*func)(va_list ap);
	/**
	 * If the callback sets errno, it's preserved across the
	 * call.
	 */
	/** Callback arguments. */
	va_list ap;
	/** Callback results. */
	ssize_t result;
	int complete;
	int errorno;
};

static void
coeio_custom_cb(eio_req *req)
{
	struct coeio_task *task = (struct coeio_task *) req->data;
	req->result = task->func(task->ap);
}

/**
 * A callback invoked by eio_poll when associated
 * eio_request is complete.
 */
static int
coeio_on_complete(eio_req *req)
{
	/*
	 * If the request is cancelled, libeio doesn't
	 * invoke finish callback. Indeed, it can't do
	 * anything since its execution context has
	 * already been gone.
	 */
	assert(!EIO_CANCELLED(req));

	struct coeio_task *task = (struct coeio_task *) req->data;
	task->result = req->result;
	task->errorno = req->errorno;
	task->complete = 1;
	fiber_wakeup(task->fiber);

	return 0;
}

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
 * @retval -1 and errno = ETIMEDOUT if timed out
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
 *	 if (coeio_custom(openfile_cb, 0.10, "/tmp/file", 0) == -1)
 *		// handle errors.
 *	...
 */
ssize_t
coeio_custom(ssize_t (*func)(va_list ap), ev_tstamp timeout, ...)
{
	struct coeio_task task;
	task.fiber = fiber();
	task.func = func;
	task.result = -1;
	task.complete = 0;
	/*
	 * Cancelling a fiber which is suspended
	 * on coeio_custom task would destroy
	 * its stack and crash the server when
	 * this stack is accessed in the worker thread.
	 */
	bool cancellable = fiber_set_cancellable(false);
	va_start(task.ap, timeout);
	struct eio_req *req = eio_custom(coeio_custom_cb, 0,
					 coeio_on_complete, &task);
	if (req == NULL) {
		errno = ENOMEM;
	} else if (fiber_yield_timeout(timeout) && !task.complete) {
		/* timeout. */
		errno = ETIMEDOUT;
		task.result = -1;
		eio_cancel(req);
	} else {
		/* the task is complete. */
		errno = task.errorno;
	}
	va_end(task.ap);
	fiber_set_cancellable(cancellable);
	return task.result;
}

/*
 * Resolver function, run in separate thread by
 * coeio (libeio).
*/
static ssize_t
getaddrinfo_cb(va_list ap)
{
	const char *host = va_arg(ap, const char *);
	const char *port = va_arg(ap, const char *);
	struct addrinfo *hints = va_arg(ap, struct addrinfo *);
	struct addrinfo **res = va_arg(ap, struct addrinfo **);

	int rc = getaddrinfo(host, port, hints, res);
	/* getaddrinfo can return EAI_ADDRFAMILY on attempt
	 * to resolve ::1, if machine has no public ipv6 addresses
	 * configured. Retry without AI_ADDRCONFIG flag set.
	 *
	 * See for details: https://bugs.launchpad.net/tarantool/+bug/1160877
	 */

	/* EAI_ADDRFAMILY is deprecated on FreeBSD */
	if (rc == EAI_BADFLAGS
#ifdef EAI_ADDRFAMILY
	    || rc == EAI_ADDRFAMILY
#endif
	   ) {
		hints->ai_flags &= ~AI_ADDRCONFIG;
		rc = getaddrinfo(host, port, hints, res);
	}
	if (rc) {
		errno = ERESOLVE;
		return -1;
	}
	return 0;
}

struct addrinfo *
coeio_resolve(int socktype, const char *host, const char *port,
              ev_tstamp timeout)
{
	/* Fill hinting information for use by connect(2) or bind(2). */
	struct addrinfo *result = NULL;
	struct addrinfo hints;
	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_UNSPEC; /* Allow IPv4 or IPv6 */
	hints.ai_socktype = socktype;
	hints.ai_flags = AI_ADDRCONFIG|AI_NUMERICSERV|AI_PASSIVE;
	hints.ai_protocol = 0;
	/* do resolving */
	errno = 0;
	/* make no difference between empty string and NULL for host */
	if (coeio_custom(getaddrinfo_cb, timeout, (host && *host) ? host : NULL,
			 port, &hints, &result))
		return NULL;
	return result;
}

ssize_t
cord_cojoin_cb(va_list ap)
{
	struct cord *cord = va_arg(ap, struct cord *);
	void *retval = NULL;
	int res = tt_pthread_join(cord->id, &retval);
	return res;
}

int
cord_cojoin(struct cord *cord)
{
	assert(cord() != cord); /* Can't join self. */
	int rc = coeio_custom(cord_cojoin_cb, TIMEOUT_INFINITY, cord);
	if (rc == 0 && cord->fiber->exception) {
		Exception::move(&cord->fiber->exception, &fiber()->exception);
		cord_destroy(cord);
		 /* re-throw exception in this fiber */
		fiber()->exception->raise();
	}
	cord_destroy(cord);
	return rc;
}

