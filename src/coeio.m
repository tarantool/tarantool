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
#include <rlist.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Asynchronous IO Tasks (libeio wrapper).
 * ---
 *
 * Libeio request processing is designed in edge-trigger
 * manner, when libeio is ready to process some requests it
 * calls coeio_poller callback.
 *
 * Due to libeio design, coeio_poller is called while locks
 * are being held, so it's unable to call any libeio function
 * inside this callback.
 *
 * coeio_poller triggers coeio_watcher to start the polling process.
 * In case if none of the requests are complete by that time, it
 * starts idle_watcher, which would periodically invoke eio_poll
 * until any of requests are complete.
 *
 * See for details:
 * http://pod.tst.eu/http://cvs.schmorp.de/libeio/eio.pod
*/
struct coeio_manager {
	ev_idle coeio_repeat_watcher;
	ev_async coeio_watcher;
	struct rlist active;
};

static struct coeio_manager coeio_manager;

static void
coeio_schedule_repeat(struct ev_idle *w,
		     int events __attribute__((unused)))
{
	if (eio_poll() != -1)
		ev_idle_stop(w);
}

static void
coeio_schedule(struct ev_async *w __attribute__((unused)),
	      int events __attribute__((unused)))
{
	if (eio_poll() == -1)
		ev_idle_start(&coeio_manager.coeio_repeat_watcher);
}

static void coeio_poller(void)
{
	ev_async_send(&coeio_manager.coeio_watcher);
}

/**
 * Init coeio subsystem.
 *
 * Create idle and async watchers, init eio.
 */
void
coeio_init(void)
{
	memset(&coeio_manager, 0, sizeof(struct coeio_manager));

	rlist_init(&coeio_manager.active);

	ev_idle_init(&coeio_manager.coeio_repeat_watcher, coeio_schedule_repeat);
	ev_async_init(&coeio_manager.coeio_watcher, coeio_schedule);
	ev_async_start(&coeio_manager.coeio_watcher);

	eio_init(coeio_poller, NULL);
}

/**
 * Cancel active tasks and free memory.
 */
void
coeio_free(void)
{
	struct coeio_req *r;
	struct coeio_req *r_next;

	/* cancel active requests */
	r = rlist_first_entry(&coeio_manager.active, struct coeio_req, link);
	while (1) {
		if (r == rlist_last_entry(&coeio_manager.active,
					  struct coeio_req, link))
			break;
		r_next = rlist_next_entry(r, link);
		/* eio_cancel sets task as cancelled, this guarantees
		 * that coeio_on_complete would never be called for
		 * this request, thus we are allowed to free memory here. */
		eio_cancel(r->req);
		free(r);
		r = r_next;
	}
}

inline static struct coeio_req*
coeio_alloc(void)
{
	struct coeio_req *r = calloc(1, sizeof(struct coeio_req));
	if (r == NULL) {
		tnt_raise(LoggedError, :ER_MEMORY_ISSUE,
			  sizeof(struct coeio_req), "coeio_alloc",
			  "coeio_req");
	}
	rlist_init(&r->link);
	return r;
}

static int
coeio_on_complete(eio_req *req)
{
	struct coeio_req *r = req->data;
	struct fiber *f = r->f;
	r->complete = true;
	rlist_del_entry(r, link);
	if (r->wait)
		fiber_wakeup(f);
	return 0;
}

/**
 * Create new eio task with specified libeio function and
 * argument.
 *
 * @throws ER_MEMORY_ISSUE
 *
 * @return coeio object pointer.
 *
 * @code
 *	static void request(eio_req *req) {
 *		(void)req->data; // "arg"
 *
 *		req->result = "result";
 *	}
 *
 *      struct coeio_req *r = coeio_custom(request, "arg");
 *
 */
struct coeio_req*
coeio_custom(void (*f)(eio_req*), void *arg)
{
	struct coeio_req *r = coeio_alloc();
	r->f = fiber;
	r->f_data = arg;
	r->req = eio_custom(f, 0, coeio_on_complete, r);
	if (r->req == NULL) {
		tnt_raise(LoggedError, :ER_MEMORY_ISSUE,
			  sizeof(struct eio_req), "coeio_custom",
			  "eio_req");
	}
	rlist_add_tail_entry(&coeio_manager.active, r, link);
	return r;
}

/**
 * Yield and wait for a request completion.
 *
 * @throws FiberCancelException
 *
 * @return request result pointer.
 *
 * @code
 *      struct coeio_req *r = coeio_custom(callback, NULL);
 *
 *      // wait for result and free request object
 *      void *result = coeio_wait(r);
 *
 *      // continue with result
 */
void *coeio_wait(struct coeio_req *r)
{
	if (r->complete) {
		void *result = r->result;
		free(r);
		return result;
	}
	r->wait = true;
	fiber_yield();
	void *result = r->result;
	free(r);
	fiber_testcancel();
	return result;
}
