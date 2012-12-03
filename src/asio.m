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
#include "asio.h"
#include "fiber.h"
#include "config.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#include <say.h>
#include <tarantool.h>
#include <rlist.h>

/*
 * Asynchronous IO Tasks (libeio wrapper).
 * ---
 *
 * Libeio request processing is designed in edge-trigger
 * manner, when libeio is ready to process some requests it
 * calls asio_poller callback.
 *
 * Due to libeio desing, asio_poller is called while locks
 * are being held, so it's unable to call any libeio function
 * inside this callback.
 *
 * asio_poller triggers asio_watcher to start polling process.
 * In case if none of requests are complete by that time, it
 * starts idle_watcher, which would periodically invoke eio_poll
 * until any of requests are complete.
 *
 * See for details:
 * http://pod.tst.eu/http://cvs.schmorp.de/libeio/eio.pod
*/
struct asio_manager {
	ev_idle asio_repeat_watcher;
	ev_async asio_watcher;
	struct rlist active;
	struct rlist complete;
	struct rlist ready;
};

static struct asio_manager asio_mgr;

inline static void
asio_put(struct asio *a, struct rlist *list)
{
	rlist_del_entry(a, link);
	rlist_add_tail_entry(list, a, link);
}

static void
asio_schedule_repeat(struct ev_idle *w,
		     int events __attribute__((unused)))
{
	if (eio_poll() != -1)
		ev_idle_stop(w);
}

static void
asio_schedule(struct ev_async *w __attribute__((unused)),
	      int events __attribute__((unused)))
{
	if (eio_poll() == -1)
		ev_idle_start(&asio_mgr.asio_repeat_watcher);
}

static void asio_poller(void)
{
	ev_async_send(&asio_mgr.asio_watcher);
}

/**
 * Init asio subsystem.
 *
 * Create idle and async watchers, init eio.
 */
void
asio_init(void)
{
	memset(&asio_mgr, 0, sizeof(struct asio_manager));

	rlist_init(&asio_mgr.active);
	rlist_init(&asio_mgr.complete);
	rlist_init(&asio_mgr.ready);

	ev_idle_init(&asio_mgr.asio_repeat_watcher, asio_schedule_repeat);
	ev_async_init(&asio_mgr.asio_watcher, asio_schedule);
	ev_async_start(&asio_mgr.asio_watcher);

	eio_init(asio_poller, NULL);
}

static inline void
asio_free_list(struct rlist *list)
{
	struct asio *a;
	struct asio *a_next;
	a = rlist_first_entry(list, struct asio, link);
	while (a != rlist_last_entry(list, struct asio, link)) {
		a_next = rlist_next_entry(a, link);
		free(a);
		a = a_next;
	}
}

/**
 * Cancel active tasks and free memory.
 */
void
asio_free(void)
{
	struct asio *a;
	/* cancel active requests */
	rlist_foreach_entry(a, &asio_mgr.active, link)
		asio_cancel(a);
	/* free all complete and ready requests */
	asio_free_list(&asio_mgr.complete);
	asio_free_list(&asio_mgr.ready);
}

inline static struct asio*
asio_alloc(void)
{
	struct asio *a = malloc(sizeof(struct asio));
	if (a == NULL)
		return NULL;
	memset(a, 0, sizeof(struct asio));
	rlist_init(&a->link);
	return a;
}

static int
asio_on_complete(eio_req *req)
{
	struct asio *a = req->data;
	struct fiber *f = a->f;
	a->complete = true;
	asio_put(a, &asio_mgr.complete);
	if (a->wakeup)
		fiber_wakeup(f);
	return 0;
}

/**
 * Create new eio task with specified libeio function and
 * argument.
 *
 * @return asio object pointer, or NULL on error.
 *
 * @code
 * 	static void request(eio_req *req) {
 * 		(void)req->data; // "arg"
 *
 * 		req->result = "result";
 * 	}
 *
 *      struct asio *a = asio_create(request, "arg");
 *      assert(a != NULL);
 *
 */
struct asio*
asio_create(void (*f)(eio_req*), void *arg)
{
	struct asio *a;
	if (!rlist_empty(&asio_mgr.complete)) {
		a = rlist_first_entry(&asio_mgr.complete, struct asio, link);
	} else {
		a = asio_alloc();
		if (a == NULL)
			return NULL;
	}
	a->wakeup = false;
	a->complete = false;
	a->f = fiber;
	a->f_data = arg;
	a->result = NULL;
	a->req = eio_custom(f, 0, asio_on_complete, a);
	if (a->req == NULL)
		return NULL;
	asio_put(a, &asio_mgr.active);
	return a;
}

/**
 * Yield and wait for a request completion.
 *
 * @return true if timeout exceeded
 */
bool
asio_wait(struct asio *a, ev_tstamp timeout)
{
	if (a->complete)
		return 0;
	a->wakeup = true;
	bool rc = fiber_yield_timeout(timeout);
	fiber_testcancel();
	return rc;
}

/**
 * Cancel eio request.
 */
void
asio_cancel(struct asio *a)
{
	eio_cancel(a->req);
	asio_put(a, &asio_mgr.ready);
}

/** 
 * Finish with request interaction.
 */
void
asio_finish(struct asio *a)
{
	asio_put(a, &asio_mgr.ready);
}
