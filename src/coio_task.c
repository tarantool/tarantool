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
#include "coio_task.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>

#include "fiber.h"
#include "third_party/tarantool_ev.h"

/*
 * Asynchronous IO Tasks (libeio wrapper).
 * ---------------------------------------
 *
 * libeio request processing is designed in edge-trigger
 * manner, when libeio is ready to process some requests it
 * calls coio_poller callback.
 *
 * Due to libeio design, want_poll callback is called while
 * locks are being held, so it's not possible to call any libeio
 * function inside this callback. Thus coio_want_poll raises an
 * async event which will be dealt with normally as part of the
 * main Tarantool event loop.
 *
 * The async event handler, in turn, performs eio_poll(), which
 * will run on_complete callback for all ready eio tasks.
 * In case if some of the requests are not complete by the time
 * eio_poll() has been called, coio_idle watcher is started, which
 * would periodically invoke eio_poll() until all requests are
 * complete.
 *
 * See for details:
 * http://pod.tst.eu/http://cvs.schmorp.de/libeio/eio.pod
*/

struct coio_manager {
	ev_loop *loop;
	ev_idle coio_idle;
	ev_async coio_async;
};

static __thread struct coio_manager coio_manager;

static void
coio_idle_cb(ev_loop *loop, struct ev_idle *w, int events)
{
	(void) events;
	if (eio_poll() != -1) {
		/* nothing to do */
		ev_idle_stop(loop, w);
	}
}

static void
coio_async_cb(ev_loop *loop, MAYBE_UNUSED struct ev_async *w,
	       MAYBE_UNUSED int events)
{
	if (eio_poll() == -1) {
		/* not all tasks are complete. */
		ev_idle_start(loop, &coio_manager.coio_idle);
	}
}

static void
coio_want_poll_cb(void *ptr)
{
	struct coio_manager *manager = ptr;
	ev_async_send(manager->loop, &manager->coio_async);
}

static void
coio_done_poll_cb(void *ptr)
{
	(void)ptr;
}

static int
coio_on_start(void *data)
{
	(void) data;
	struct cord *cord = (struct cord *)calloc(sizeof(struct cord), 1);
	if (!cord)
		return -1;
	cord_create(cord, "coio");
	return 0;
}

static int
coio_on_stop(void *data)
{
	(void) data;
	cord_destroy(cord());
	return 0;
}

void
coio_init(void)
{
	eio_set_thread_on_start(coio_on_start, NULL);
	eio_set_thread_on_stop(coio_on_stop, NULL);
}

/**
 * Init coio subsystem.
 *
 * Create idle and async watchers, init eio.
 */
void
coio_enable(void)
{
	eio_init(&coio_manager, coio_want_poll_cb, coio_done_poll_cb);
	coio_manager.loop = loop();

	ev_idle_init(&coio_manager.coio_idle, coio_idle_cb);
	ev_async_init(&coio_manager.coio_async, coio_async_cb);

	ev_async_start(loop(), &coio_manager.coio_async);
}

void
coio_shutdown(void)
{
	eio_set_max_parallel(0);
}

static void
coio_on_feed(eio_req *req)
{
	struct coio_task *task = (struct coio_task *) req;
	req->result = task->task_cb(task);
	if (req->result)
		diag_move(diag_get(), &task->diag);
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
		/*
		 * Timed out. Resources will be freed by coio_on_destroy.
		 * NOTE: it is not safe to run timeout_cb handler here.
		 */
		return 0;
	}

	task->complete = 1;
	/* Reset on_timeout hook - resources will be freed by coio_task user */
	task->base.destroy = NULL;
	fiber_wakeup(task->fiber);
	return 0;
}

/*
 * Free resources on timeout.
 */
static void
coio_on_destroy(eio_req *req)
{
	struct coio_task *task = (struct coio_task *) req;
	assert(task->fiber == NULL && task->complete == 0);
	if (task->timeout_cb != NULL)
		task->timeout_cb(task);
}

void
coio_task_create(struct coio_task *task,
		 coio_task_cb func, coio_task_cb on_timeout)
{
	assert(func != NULL && on_timeout != NULL);

	/* from eio.c: REQ() definition */
	memset(&task->base, 0, sizeof(task->base));
	task->base.type = EIO_CUSTOM;
	task->base.feed = coio_on_feed;
	task->base.finish = coio_on_finish;
	task->base.destroy = coio_on_destroy;
	/* task->base.pri = 0; */

	task->fiber = fiber();
	task->task_cb = func;
	task->timeout_cb = on_timeout;
	task->complete = 0;
	diag_create(&task->diag);
}

void
coio_task_destroy(struct coio_task *task)
{
	diag_destroy(&task->diag);
}

int
coio_task_post(struct coio_task *task, double timeout)
{
	assert(task->base.type == EIO_CUSTOM);
	assert(task->fiber == fiber());

	eio_submit(&task->base);
	if (timeout == 0) {
		/*
		* This is a special case:
		* we don't wait any response from the task
		* and just perform just asynchronous post.
		*/
		task->fiber = NULL;
		return 0;
	}
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
	return 0;
}

static void
coio_on_call(eio_req *req)
{
	struct coio_task *task = (struct coio_task *) req;
	req->result = task->call_cb(task->ap);
	if (req->result)
		diag_move(diag_get(), &task->diag);
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
	diag_create(&task->diag);

	va_start(task->ap, func);
	eio_submit(&task->base);

	do {
		fiber_yield();
	} while (task->complete == 0);
	va_end(task->ap);

	ssize_t result = task->base.result;
	int save_errno = errno;
	if (result)
		diag_move(&task->diag, diag_get());
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
 * coio (libeio).
*/
static int
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

static int
getaddrinfo_free_cb(struct coio_task *ptr)
{
	struct async_getaddrinfo_task *task =
		(struct async_getaddrinfo_task *) ptr;
	if (task->host != NULL)
		free(task->host);
	if (task->port != NULL)
		free(task->port);
	if (task->result != NULL)
		freeaddrinfo(task->result);
	coio_task_destroy(&task->base);
	TRASH(task);
	free(task);
	return 0;
}

int
coio_getaddrinfo(const char *host, const char *port,
		 const struct addrinfo *hints, struct addrinfo **res,
		 double timeout)
{
	struct async_getaddrinfo_task *task =
		(struct async_getaddrinfo_task *) calloc(1, sizeof(*task));
	if (task == NULL) {
		diag_set(OutOfMemory, sizeof(*task), "malloc", "getaddrinfo");
		return -1;
	}

	coio_task_create(&task->base, getaddrinfo_cb, getaddrinfo_free_cb);

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
			diag_set(OutOfMemory, strlen(host), "malloc",
				 "getaddrinfo");
			getaddrinfo_free_cb(&task->base);
			return -1;
		}
	}
	if (port != NULL) {
		task->port = strdup(port);
		if (task->port == NULL) {
			diag_set(OutOfMemory, strlen(port), "malloc",
				 "getaddrinfo");
			getaddrinfo_free_cb(&task->base);
			return -1;
		}
	}

	/* Post coio task */
	if (coio_task_post(&task->base, timeout) != 0)
		return -1; /* timed out or cancelled */

	/* Task finished */
	if (task->rc < 0) {
		/* getaddrinfo() failed */
		errno = EIO;
		diag_set(SystemError, "getaddrinfo: %s",
			 gai_strerror(task->rc));
		getaddrinfo_free_cb(&task->base);
		return -1;
	}

	/* getaddrinfo() succeed */
	*res = task->result;
	task->result = NULL;
	getaddrinfo_free_cb(&task->base);
	return 0;
}
