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
#include "coeio.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>

#include "fiber.h"
#include "diag.h"
#include "third_party/tarantool_ev.h"

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
};

static __thread struct coeio_manager coeio_manager;

static void
coeio_idle_cb(ev_loop *loop, struct ev_idle *w, int events)
{
	(void) events;
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
coeio_want_poll_cb(void *ptr)
{
	struct coeio_manager *manager = ptr;
	ev_async_send(manager->loop, &manager->coeio_async);
}

static void
coeio_done_poll_cb(void *ptr)
{
	(void)ptr;
}

/**
 * Init coeio subsystem.
 *
 * Create idle and async watchers, init eio.
 */
void
coeio_init(void)
{
	eio_init(&coeio_manager, coeio_want_poll_cb, coeio_done_poll_cb);
	coeio_manager.loop = loop();

	ev_idle_init(&coeio_manager.coeio_idle, coeio_idle_cb);
	ev_async_init(&coeio_manager.coeio_async, coeio_async_cb);

	ev_async_start(loop(), &coeio_manager.coeio_async);
}

static void
coio_on_exec(eio_req *req)
{
	struct coio_task *task = (struct coio_task *) req;
	req->result = task->task_cb(task);
}

/**
 * A callback invoked by eio_poll when associated
 * eio_request is complete.
 */
static int
coio_on_finish(eio_req *req)
{
	struct coio_task *task = (struct coio_task *) req;
	if (task->fiber == NULL) {
		/* timed out (only coio_task() )*/
		if (task->timeout_cb != NULL) {
			task->timeout_cb(task);
		}
		return 0;
	}

	task->complete = 1;
	fiber_wakeup(task->fiber);
	return 0;
}

/**
 * @retval -1 timeout or the waiting fiber was cancelled;
 *            the caller should not free the task, it
 *            will be freed when it's finished in the timeout
 *            callback.
 * @retval 0  the task completed successfully. Check the result
 *            code in task->rc and free the task.
 */

static ssize_t
coio_task(struct coio_task *task, coio_task_cb func,
	  coio_task_cb on_timeout, double timeout)
{
	/* from eio.c: REQ() definition */
	memset(&task->base, 0, sizeof(task->base));
	task->base.type = EIO_CUSTOM;
	task->base.feed = coio_on_exec;
	task->base.finish = coio_on_finish;
	/* task->base.destroy = NULL; */
	/* task->base.pri = 0; */

	task->fiber = fiber();
	task->task_cb = func;
	task->timeout_cb = on_timeout;
	task->complete = 0;

	eio_submit(&task->base);
	fiber_yield_timeout(timeout);
	if (!task->complete) {
		/* timed out or cancelled. */
		task->fiber = NULL;
		if (fiber_is_cancelled())
			diag_set(FiberIsCancelled);
		else
			diag_set(TimedOut);
		return -1;
	}
	diag_clear(&fiber()->diag);
	return 0;
}

static void
coio_on_call(eio_req *req)
{
	struct coio_task *task = (struct coio_task *) req;
	req->result = task->call_cb(task->ap);
}

ssize_t
coio_call(ssize_t (*func)(va_list ap), ...)
{
	struct coio_task *task = (struct coio_task *) calloc(1, sizeof(*task));
	if (task == NULL)
		return -1; /* errno = ENOMEM */
	/* from eio.c: REQ() definition */
	task->base.type = EIO_CUSTOM;
	task->base.feed = coio_on_call;
	task->base.finish = coio_on_finish;
	/* task->base.destroy = NULL; */
	/* task->base.pri = 0; */

	task->fiber = fiber();
	task->call_cb = func;
	task->complete = 0;

	bool cancellable = fiber_set_cancellable(false);

	va_start(task->ap, func);
	eio_submit(&task->base);

	fiber_yield();
	/* Spurious wakeup indicates a severe BUG, fail early. */
	if (task->complete == 0)
		panic("Wrong fiber woken");
	va_end(task->ap);

	fiber_set_cancellable(cancellable);

	ssize_t result = task->base.result;
	int save_errno = errno;
	free(task);
	errno = save_errno;
	return result;
}

struct async_getaddrinfo_task {
	struct coio_task base;
	struct addrinfo *result;
	int rc;
	char *host;
	char *port;
	struct addrinfo hints;
};

#ifndef EAI_ADDRFAMILY
#define EAI_ADDRFAMILY EAI_BADFLAGS /* EAI_ADDRFAMILY is deprecated on BSD */
#endif

/*
 * Resolver function, run in separate thread by
 * coeio (libeio).
*/
static ssize_t
getaddrinfo_cb(struct coio_task *ptr)
{
	struct async_getaddrinfo_task *task =
		(struct async_getaddrinfo_task *) ptr;

	task->rc = getaddrinfo(task->host, task->port, &task->hints,
			     &task->result);

	/* getaddrinfo can return EAI_ADDRFAMILY on attempt
	 * to resolve ::1, if machine has no public ipv6 addresses
	 * configured. Retry without AI_ADDRCONFIG flag set.
	 *
	 * See for details: https://bugs.launchpad.net/tarantool/+bug/1160877
	 */
	if ((task->rc == EAI_BADFLAGS || task->rc == EAI_ADDRFAMILY) &&
	    (task->hints.ai_flags & AI_ADDRCONFIG)) {
		task->hints.ai_flags &= ~AI_ADDRCONFIG;
		task->rc = getaddrinfo(task->host, task->port, &task->hints,
			     &task->result);
	}
	return 0;
}

static ssize_t
getaddrinfo_free_cb(struct coio_task *ptr)
{
	struct async_getaddrinfo_task *task =
		(struct async_getaddrinfo_task *) ptr;
	free(task->host);
	free(task->port);
	if (task->result != NULL)
		freeaddrinfo(task->result);
	free(task);
	return 0;
}

int
coio_getaddrinfo(const char *host, const char *port,
		 const struct addrinfo *hints, struct addrinfo **res,
		 double timeout)
{
	int rc = EAI_SYSTEM;
	int save_errno = 0;

	struct async_getaddrinfo_task *task =
		(struct async_getaddrinfo_task *) calloc(1, sizeof(*task));
	if (task == NULL)
		return rc;

	/*
	 * getaddrinfo() on osx upto osx 10.8 crashes when AI_NUMERICSERV is
	 * set and servername is either NULL or "0" ("00" works fine)
	 *
	 * Based on the workaround in https://bugs.python.org/issue17269
	 */
#if defined(__APPLE__) && defined(AI_NUMERICSERV)
	if (hints && (hints->ai_flags & AI_NUMERICSERV) &&
	    (port == NULL || (port[0]=='0' && port[1]=='\0'))) port = "00";
#endif
	/* Fill hinting information for use by connect(2) or bind(2). */
	memcpy(&task->hints, hints, sizeof(task->hints));
	/* make no difference between empty string and NULL for host */
	if (host != NULL && *host) {
		task->host = strdup(host);
		if (task->host == NULL) {
			save_errno = errno;
			goto cleanup_task;
		}
	}
	if (port != NULL) {
		task->port = strdup(port);
		if (task->port == NULL) {
			save_errno = errno;
			goto cleanup_host;
		}
	}
	/* do resolving */
	/* coio_task() don't throw. */
	if (coio_task(&task->base, getaddrinfo_cb, getaddrinfo_free_cb,
		       timeout) == -1) {
		return -1;
	}

	rc = task->rc;
	*res = task->result;
	free(task->port);
cleanup_host:
	free(task->host);
cleanup_task:
	free(task);
	errno = save_errno;
	return rc;
}

