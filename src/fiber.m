/*
 * Copyright (C) 2010 Mail.RU
 * Copyright (C) 2010 Yuriy Vostrikov
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "fiber.h"
#include "config.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sysexits.h>
#include <third_party/queue.h>
#include <assoc.h>

#include <palloc.h>
#include <salloc.h>
#include <say.h>
#include <tarantool.h>
#include TARANTOOL_CONFIG
#include <tarantool_ev.h>
#include <tbuf.h>
#include <util.h>
#include <stat.h>
#include <pickle.h>

@implementation FiberCancelException
@end

#define FIBER_CALL_STACK 16

static struct fiber sched;
struct fiber *fiber = &sched;
static struct fiber **sp, *call_stack[FIBER_CALL_STACK];
static uint32_t last_used_fid;
static struct palloc_pool *ex_pool;

struct fiber_cleanup {
	void (*handler) (void *data);
	void *data;
};

struct fiber_server {
	int port;
	void *data;
	void (*handler) (void *data);
	void (*on_bind) (void *data);
};

static struct mh_i32ptr_t *fibers_registry;

static void
update_last_stack_frame(struct fiber *fiber)
{
#ifdef ENABLE_BACKTRACE
	fiber->last_stack_frame = __builtin_frame_address(0);
#else
	(void)fiber;
#endif /* ENABLE_BACKTRACE */
}

void
fiber_call(struct fiber *callee)
{
	struct fiber *caller = fiber;

	assert(sp - call_stack < FIBER_CALL_STACK);
	assert(caller);

	fiber = callee;
	*sp++ = caller;

	update_last_stack_frame(caller);

	callee->csw++;
	coro_transfer(&caller->coro.ctx, &callee->coro.ctx);
}


/** Interrupt a synchronous wait of a fiber inside the event loop.
 * We do so by keeping an "async" event in every fiber, solely
 * for this purpose, and raising this event here.
 */

void
fiber_wakeup(struct fiber *f)
{
	ev_async_send(&f->async);
}

/** Cancel the subject fiber.
 *
 * Note: this is not guaranteed to succeed, and requires a level
 * of cooperation on behalf of the fiber. A fiber may opt to set
 * FIBER_CANCELLABLE to false, and never test that it was
 * cancelled.  Such fiber we won't be ever to cancel, ever, and
 * for such fiber this call will lead to an infinite wait.
 * However, fiber_testcancel() is embedded to the rest of fiber_*
 * API (@sa fiber_yield()), which makes most of the fibers that opt in,
 * cancellable.
 *
 * Currently cancellation can only be synchronous: this call
 * returns only when the subject fiber has terminated.
 *
 * The fiber which is cancelled, has FiberCancelException raised
 * in it. For cancellation to work, this exception type should be
 * re-raised whenever (if) it is caught.
 */

void
fiber_cancel(struct fiber *f)
{
	assert(f->fid != 0);
	assert(!(f->flags & FIBER_CANCEL));

	f->flags |= FIBER_CANCEL;

	if (f == fiber) {
		fiber_testcancel();
		return;
	}
	/**
	 * In most cases the fiber is CANCELLABLE and
	 * will notice it's been cancelled right away.
	 * So we just invoke it here in hope it'll die
	 * and yield to us without a full scheduler loop.
	 */
	fiber_call(f);

	if (f->fid) {
		/*
		 * Syncrhonous cancel did not work: apparently
		 * the fiber is not CANCELLABLE or for some reason
		 * chose to yield without dying. We have no
		 * choice but to wait asynchronously.
		 */
		assert(f->waiter == NULL);
		f->waiter = fiber;
		fiber_yield();
	}
	/*
	 * Here we can't even check f->fid is 0 since
	 * f could have already been reused. Knowing
	 * at least that we can't get scheduled ourselves
	 * unless asynchronously woken up is somewhat a relief.
	 */

	fiber_testcancel(); /* Check if we're ourselves cancelled. */
}

static bool
fiber_is_cancelled()
{
	return (fiber->flags & FIBER_CANCELLABLE &&
		fiber->flags & FIBER_CANCEL);
}

/** Test if this fiber is in a cancellable state and was indeed
 * cancelled, and raise an exception (FiberCancelException) if
 * that's the case.
 */

void
fiber_testcancel(void)
{
	if (fiber_is_cancelled())
		tnt_raise(FiberCancelException);
}



/** Change the current cancellation state of a fiber. This is not
 * a cancellation point.
 */

void fiber_setcancelstate(bool enable)
{
	if (enable == true)
		fiber->flags |= FIBER_CANCELLABLE;
	else
		fiber->flags &= ~FIBER_CANCELLABLE;
}

/**
 * @note: this is not a cancellation point (@sa fiber_testcancel())
 * but it is considered good practice to call testcancel()
 * after each yield.
 */

void
fiber_yield(void)
{
	struct fiber *callee = *(--sp);
	struct fiber *caller = fiber;

	fiber = callee;
	update_last_stack_frame(caller);

	callee->csw++;
	coro_transfer(&caller->coro.ctx, &callee->coro.ctx);
}

void
fiber_yield_to(struct fiber *f)
{
	fiber_wakeup(f);
	fiber_yield();
	fiber_testcancel();
}

/**
 * @note: this is a cancellation point (@sa fiber_testcancel())
 */

void
fiber_sleep(ev_tstamp delay)
{
	ev_timer_set(&fiber->timer, delay, 0.);
	ev_timer_start(&fiber->timer);
	fiber_yield();
	ev_timer_stop(&fiber->timer);
	fiber_testcancel();
}

/** Wait for a forked child to complete.
 * @note: this is a cancellation point (@sa fiber_testcancel()).
*/

void
wait_for_child(pid_t pid)
{
	ev_child_set(&fiber->cw, pid, 0);
	ev_child_start(&fiber->cw);
	fiber_yield();
	ev_child_stop(&fiber->cw);
	fiber_testcancel();
}


void
fiber_io_start(int fd, int events)
{
	ev_io *io = &fiber->io;

	assert (!ev_is_active(io));

	ev_io_set(io, fd, events);
	ev_io_start(io);
}

/** @note: this is a cancellation point.
 */

void
fiber_io_yield()
{
	assert(ev_is_active(&fiber->io));

	fiber_yield();

	if (fiber_is_cancelled()) {
		ev_io_stop(&fiber->io);
		fiber_testcancel();
	}
}

void
fiber_io_stop(int fd __attribute__((unused)), int events __attribute__((unused)))
{
	ev_io *io = &fiber->io;

	assert(ev_is_active(io) && io->fd == fd && (io->events & events));

	ev_io_stop(io);
}

static void
ev_schedule(ev_watcher *watcher, int event __attribute__((unused)))
{
	assert(fiber == &sched);
	fiber_call(watcher->data);
}

struct fiber *
fiber_find(int fid)
{
	mh_int_t k = mh_i32ptr_get(fibers_registry, fid);

	if (k == mh_end(fibers_registry))
		return NULL;
	if (!mh_exist(fibers_registry, k))
		return NULL;
	return mh_value(fibers_registry, k);
}

static void
register_fid(struct fiber *fiber)
{
	int ret;
	mh_i32ptr_put(fibers_registry, fiber->fid, fiber, &ret);
}

static void
unregister_fid(struct fiber *fiber)
{
	mh_int_t k = mh_i32ptr_get(fibers_registry, fiber->fid);
	mh_i32ptr_del(fibers_registry, k);
}

static void
fiber_alloc(struct fiber *fiber)
{
	prelease(fiber->gc_pool);
	fiber->rbuf = tbuf_alloc(fiber->gc_pool);
	fiber->iov = tbuf_alloc(fiber->gc_pool);
	fiber->cleanup = tbuf_alloc(fiber->gc_pool);

	fiber->iov_cnt = 0;
}

void
fiber_register_cleanup(fiber_cleanup_handler handler, void *data)
{
	struct fiber_cleanup i;
	i.handler = handler;
	i.data = data;
	tbuf_append(fiber->cleanup, &i, sizeof(struct fiber_cleanup));
}

void
fiber_cleanup(void)
{
	struct fiber_cleanup *cleanup = fiber->cleanup->data;
	int i = fiber->cleanup->size / sizeof(struct fiber_cleanup);

	while (i-- > 0) {
		cleanup->handler(cleanup->data);
		cleanup++;
	}
	tbuf_reset(fiber->cleanup);
}

void
fiber_gc(void)
{
	struct palloc_pool *tmp;

	fiber_cleanup();

	if (palloc_allocated(fiber->gc_pool) < 128 * 1024)
		return;

	tmp = fiber->gc_pool;
	fiber->gc_pool = ex_pool;
	ex_pool = tmp;
	palloc_set_name(fiber->gc_pool, fiber->name);
	palloc_set_name(ex_pool, "ex_pool");

	fiber->rbuf = tbuf_clone(fiber->gc_pool, fiber->rbuf);
	fiber->cleanup = tbuf_clone(fiber->gc_pool, fiber->cleanup);

	struct tbuf *new_iov = tbuf_alloc(fiber->gc_pool);
	for (int i = 0; i < fiber->iov_cnt; i++) {
		struct iovec *v;
		size_t o = tbuf_reserve(new_iov, sizeof(*v));
		v = new_iov->data + o;
		memcpy(v, iovec(fiber->iov) + i, sizeof(*v));
	}
	fiber->iov = new_iov;

	prelease(ex_pool);
}


/** Destroy the currently active fiber and prepare it for reuse.
 */

static void
fiber_zombificate()
{
	if (fiber->waiter)
		fiber_wakeup(fiber->waiter);
	fiber->waiter = NULL;
	fiber_set_name(fiber, "zombie");
	fiber->f = NULL;
	unregister_fid(fiber);
	fiber->fid = 0;
	fiber->flags = 0;
	fiber_alloc(fiber);

	SLIST_INSERT_HEAD(&zombie_fibers, fiber, zombie_link);
}

static void
fiber_loop(void *data __attribute__((unused)))
{
	for (;;) {
		assert(fiber != NULL && fiber->f != NULL && fiber->fid != 0);
		@try {
			fiber->f(fiber->f_data);
		}
		@catch (FiberCancelException *e) {
			say_info("fiber `%s' has been cancelled", fiber->name);
			say_info("fiber `%s': exiting", fiber->name);
		}
		@catch (id e) {
			say_error("fiber `%s': exception `%s'", fiber->name, object_getClassName(e));
			panic("fiber `%s': exiting", fiber->name);
		}
		fiber_close();
		fiber_zombificate();
		fiber_yield();	/* give control back to scheduler */
	}
}

/** Set fiber name.
 *
 * @param[in] name the new name of the fiber. Truncated to
 * FIBER_NAME_MAXLEN.
*/

void
fiber_set_name(struct fiber *fiber, const char *name)
{
	assert(name != NULL);
	snprintf(fiber->name, sizeof(fiber->name), "%s", name);
}

/* fiber never dies, just become zombie */
struct fiber *
fiber_create(const char *name, int fd, void (*f) (void *), void *f_data)
{
	struct fiber *fiber = NULL;

	if (!SLIST_EMPTY(&zombie_fibers)) {
		fiber = SLIST_FIRST(&zombie_fibers);
		SLIST_REMOVE_HEAD(&zombie_fibers, zombie_link);
	} else {
		fiber = palloc(eter_pool, sizeof(*fiber));
		if (fiber == NULL)
			return NULL;

		memset(fiber, 0, sizeof(*fiber));
		if (tarantool_coro_create(&fiber->coro, fiber_loop, NULL) == NULL)
			return NULL;

		fiber->gc_pool = palloc_create_pool("");

		fiber_alloc(fiber);
		ev_init(&fiber->io, (void *)ev_schedule);
		ev_async_init(&fiber->async, (void *)ev_schedule);
		ev_async_start(&fiber->async);
		ev_init(&fiber->timer, (void *)ev_schedule);
		ev_init(&fiber->cw, (void *)ev_schedule);
		fiber->io.data = fiber->async.data = fiber->timer.data = fiber->cw.data = fiber;

		SLIST_INSERT_HEAD(&fibers, fiber, link);
	}

	fiber->fd = fd;
	fiber->f = f;
	fiber->f_data = f_data;
	while (++last_used_fid <= 100) ;	/* fids from 0 to 100 are reserved */
	fiber->fid = last_used_fid;
	fiber->flags = 0;
	fiber->waiter = NULL;
	fiber_set_name(fiber, name);
	palloc_set_name(fiber->gc_pool, fiber->name);
	register_fid(fiber);

	return fiber;
}

/*
 * note, we can't release memory allocated via palloc(eter_pool, ...)
 * so, struct fiber and some of its members are leaked forever
 */

void
fiber_destroy(struct fiber *f)
{
	if (f == fiber) /* do not destroy running fiber */
		return;
	if (strcmp(f->name, "sched") == 0)
		return;

	ev_async_stop(&f->async);
	palloc_destroy_pool(f->gc_pool);
	tarantool_coro_destroy(&f->coro);
}

void
fiber_destroy_all()
{
	struct fiber *f;
	SLIST_FOREACH(f, &fibers, link)
		fiber_destroy(f);
}


const char *
fiber_peer_name(struct fiber *fiber)
{
	struct sockaddr_in peer;
	socklen_t peer_len = sizeof(peer);

	if (!fiber->has_peer || fiber->fd < 3)
		return NULL;

	if (fiber->peer_name[0] != 0)
		return fiber->peer_name;

	memset(&peer, 0, peer_len);
	if (getpeername(fiber->fd, (struct sockaddr *)&peer, &peer_len) < 0)
		return NULL;

	uint32_t zero = 0;
	if (memcmp(&peer.sin_addr, &zero, sizeof(zero)) == 0)
		return NULL;

	snprintf(fiber->peer_name, sizeof(fiber->peer_name),
		 "%s:%d", inet_ntoa(peer.sin_addr), ntohs(peer.sin_port));

	fiber->cookie = 0;
	memcpy(&fiber->cookie, &peer, MIN(sizeof(peer), sizeof(fiber->cookie)));
	return fiber->peer_name;
}

int
fiber_close(void)
{
	if (fiber->fd < 0)
		return 0;

	/* We don't know if IO is active if there was an error. */
	if (ev_is_active(&fiber->io))
		fiber_io_stop(fiber->fd, -1);

	int r = close(fiber->fd);

	fiber->fd = -1;
	fiber->has_peer = false;
	fiber->peer_name[0] = 0;
	tbuf_reset(fiber->rbuf);

	return r;
}

/**
 * Read at least at_least bytes from a socket.
 *
 * @retval 0   socket is closed by the sender
 * @reval -1   a system error
 * @retval >0  success, size of the last read chunk is returned
 *
 * @note: this is a cancellation point.
 */

ssize_t
fiber_bread(struct tbuf *buf, size_t at_least)
{
	ssize_t r = 0;
	tbuf_ensure(buf, MAX(cfg.readahead, at_least));
	size_t stop_at = buf->size + at_least;

	fiber_io_start(fiber->fd, EV_READ);

	while (buf->size < stop_at) {
		fiber_io_yield();

		r = read(fiber->fd, buf->data + buf->size, buf->capacity - buf->size);
		if (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
			continue;
		else if (r <= 0)
			break;

		buf->size += r;
	}
	fiber_io_stop(fiber->fd, EV_READ);

	return r;
}

void
iov_reset()
{
	fiber->iov_cnt = 0;	/* discard anything unwritten */
	tbuf_reset(fiber->iov);
}

/**
 * @note: this is a cancellation point.
 */

ssize_t
iov_flush(void)
{
	ssize_t result, r = 0, bytes = 0;
	struct iovec *iov = iovec(fiber->iov);
	size_t iov_cnt = fiber->iov_cnt;

	fiber_io_start(fiber->fd, EV_WRITE);
	while (iov_cnt > 0) {
		fiber_io_yield();
		bytes += r = writev(fiber->fd, iov, MIN(iov_cnt, IOV_MAX));
		if (r <= 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				continue;
			else
				break;
		}

		while (iov_cnt > 0) {
			if (iov->iov_len > r) {
				iov->iov_base += r;
				iov->iov_len -= r;
				break;
			} else {
				r -= iov->iov_len;
				iov++;
				iov_cnt--;
			}
		}
	}
	fiber_io_stop(fiber->fd, EV_WRITE);

	if (r < 0) {
		size_t rem = 0;
		for (int i = 0; i < iov_cnt; i++)
			rem += iov[i].iov_len;

		say_syserror("client unexpectedly gone, %" PRI_SZ " bytes unwritten", rem);
		result = r;
	} else
		result = bytes;

	iov_reset();
	return result;
}

/**
 * @note: this is a cancellation point.
 */

ssize_t
fiber_read(void *buf, size_t count)
{
	ssize_t r, done = 0;

	fiber_io_start(fiber->fd, EV_READ);
	while (count != done) {

		fiber_io_yield();

		if ((r = read(fiber->fd, buf + done, count - done)) <= 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				continue;
			else
				break;
		}
		done += r;
	}
	fiber_io_stop(fiber->fd, EV_READ);

	return done;
}

/**
 * @note: this is a cancellation point.
 */

ssize_t
fiber_write(const void *buf, size_t count)
{
	int r;
	unsigned int done = 0;

	fiber_io_start(fiber->fd, EV_WRITE);

	while (count != done) {
		fiber_io_yield();
		if ((r = write(fiber->fd, buf + done, count - done)) == -1) {
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				continue;
			else
				break;
		}
		done += r;
	}
	fiber_io_stop(fiber->fd, EV_WRITE);

	return done;
}

/**
 * @note: this is a cancellation point.
 */

int
fiber_connect(struct sockaddr_in *addr)
{
	fiber->fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fiber->fd < 0)
		goto error;

	if (set_nonblock(fiber->fd) < 0)
		goto error;

	/* set SO_KEEPALIVE flag */
	int keepalive = 1;
	if (setsockopt(fiber->fd, SOL_SOCKET, SO_KEEPALIVE,
		       &keepalive, sizeof(int)) != 0)
		/* just print error, it's not critical error */
		say_syserror("setsockopt()");

	if (connect(fiber->fd, (struct sockaddr *)addr, sizeof(*addr)) < 0) {

		if (errno != EINPROGRESS)
			goto error;

		fiber_io_start(fiber->fd, EV_WRITE);
		fiber_io_yield();
		fiber_io_stop(fiber->fd, EV_WRITE);

		int error;
		socklen_t error_size = sizeof(error);

		if (getsockopt(fiber->fd, SOL_SOCKET, SO_ERROR,
			       &error, &error_size) < 0)
			goto error;

		assert(error_size == sizeof(error));

		if (error != 0) {
			errno = error;
			goto error;
		}
	}

	return fiber->fd;

      error:
	fiber_close();
	return fiber->fd;
}

int
set_nonblock(int sock)
{
	int flags;
	if ((flags = fcntl(sock, F_GETFL, 0)) < 0 || fcntl(sock, F_SETFL, flags | O_NONBLOCK) < 0)
		return -1;
	return sock;
}

static void
tcp_server_handler(void *data)
{
	struct fiber_server *server = (void*) data;
	struct fiber *h;
	char name[FIBER_NAME_MAXLEN];
	int fd;
	int one = 1;

	if (fiber_serv_socket(fiber, server->port, true, 0.1) != 0) {
		say_error("init server socket on port %i fail", server->port);
		exit(EX_OSERR);
	}

	if (server->on_bind != NULL) {
		server->on_bind(server->data);
	}

	fiber_io_start(fiber->fd, EV_READ);
	for (;;) {
		fiber_io_yield();

		while ((fd = accept(fiber->fd, NULL, NULL)) > 0) {
			if (set_nonblock(fd) == -1) {
				say_error("can't set nonblock");
				close(fd);
				continue;
			}
			if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY,
				       &one, sizeof(one)) == -1) {
				say_syserror("setsockopt failed");
				/* Do nothing, not a fatal error.  */
			}

			snprintf(name, sizeof(name), "%i/handler", server->port);
			h = fiber_create(name, fd, server->handler, server->data);
			if (h == NULL) {
				say_error("can't create handler fiber, dropping client connection");
				close(fd);
				continue;
			}

			h->has_peer = true;
			fiber_call(h);
		}
		if (fd < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
			say_syserror("accept");
			continue;
		}
	}
	fiber_io_stop(fiber->fd, EV_READ);
}

struct fiber *
fiber_server(const char *name, int port, void (*handler) (void *data), void *data,
	     void (*on_bind) (void *data))
{
	char server_name[FIBER_NAME_MAXLEN];
	struct fiber_server *server;
	struct fiber *s;

	snprintf(server_name, sizeof(server_name), "%i/%s", port, name);
	server = palloc(eter_pool, sizeof(struct fiber_server));
	assert(server != NULL);
	server->data = data;
	server->port = port;
	server->handler = handler;
	server->on_bind = on_bind;
	s = fiber_create(server_name, -1, tcp_server_handler, server);

	fiber_call(s);		/* give a handler a chance */
	return s;
}

/** create new fiber's socket and set standat options. */
static int
create_socket(struct fiber *fiber)
{
	if (fiber->fd != -1) {
		say_error("fiber is already has socket");
		goto create_socket_fail;
	}

	fiber->fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (fiber->fd == -1) {
		say_syserror("socket");
		goto create_socket_fail;
	}

	int one = 1;
	if (setsockopt(fiber->fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) != 0) {
		say_syserror("setsockopt");
		goto create_socket_fail;
	}

	struct linger ling = { 0, 0 };
	if (setsockopt(fiber->fd, SOL_SOCKET, SO_KEEPALIVE, &one, sizeof(one)) != 0 ||
	    setsockopt(fiber->fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one)) != 0 ||
	    setsockopt(fiber->fd, SOL_SOCKET, SO_LINGER, &ling, sizeof(ling)) != 0) {
		say_syserror("setsockopt");
		goto create_socket_fail;
	}

	if (set_nonblock(fiber->fd) == -1) {
		goto create_socket_fail;
	}

	return 0;

create_socket_fail:

	if (fiber->fd != -1) {
		close(fiber->fd);
	}
	return -1;
}

/** Create server socket and bind his on port. */
int
fiber_serv_socket(struct fiber *fiber, unsigned short port, bool retry, ev_tstamp delay)
{
	const ev_tstamp min_delay = 0.001; /* minimal delay is 1 msec */
	struct sockaddr_in sin;
	bool warning_said = false;

	if (delay < min_delay) {
		delay = min_delay;
	}

	if (create_socket(fiber) != 0) {
		return -1;
	}

	/* clean sockaddr_in struct */
	memset(&sin, 0, sizeof(struct sockaddr_in));

	/* fill sockaddr_in struct */
	sin.sin_family = AF_INET;
	sin.sin_port = htons(port);
	if (strcmp(cfg.bind_ipaddr, "INADDR_ANY") == 0) {
		sin.sin_addr.s_addr = INADDR_ANY;
	} else {
		if (!inet_aton(cfg.bind_ipaddr, &sin.sin_addr)) {
			say_syserror("inet_aton");
			return -1;
		}
	}

	while (true) {
		if (bind(fiber->fd, (struct sockaddr *)&sin, sizeof(sin)) != 0) {
			if (retry && (errno == EADDRINUSE)) {
				/* retry mode, try, to bind after delay */
				goto sleep_and_retry;
			}
			say_syserror("bind");
			return -1;
		}
		if (listen(fiber->fd, cfg.backlog) != 0) {
			if (retry && (errno == EADDRINUSE)) {
				/* retry mode, try, to bind after delay */
				goto sleep_and_retry;
			}
			say_syserror("listen");
			return -1;
		}

		say_info("bound to port %i", port);
		break;

	sleep_and_retry:
		if (!warning_said) {
			say_warn("port %i is already in use, "
				 "will retry binding after %lf seconds.", port, delay);
			warning_said = true;
		}
		fiber_sleep(delay);
	}

	return 0;
}

void
fiber_info(struct tbuf *out)
{
	struct fiber *fiber;

	tbuf_printf(out, "fibers:" CRLF);
	SLIST_FOREACH(fiber, &fibers, link) {
		void *stack_top = fiber->coro.stack + fiber->coro.stack_size;

		tbuf_printf(out, "  - fid: %4i" CRLF, fiber->fid);
		tbuf_printf(out, "    csw: %i" CRLF, fiber->csw);
		tbuf_printf(out, "    name: %s" CRLF, fiber->name);
		tbuf_printf(out, "    fd: %4i" CRLF, fiber->fd);
		tbuf_printf(out, "    peer: %s" CRLF, fiber_peer_name(fiber));
		tbuf_printf(out, "    stack: %p" CRLF, stack_top);
#ifdef ENABLE_BACKTRACE
		tbuf_printf(out, "    backtrace:" CRLF "%s",
			    backtrace(fiber->last_stack_frame,
				      fiber->coro.stack, fiber->coro.stack_size));
#endif /* ENABLE_BACKTRACE */
	}
}

void
fiber_init(void)
{
	SLIST_INIT(&fibers);
	fibers_registry = mh_i32ptr_init();

	ex_pool = palloc_create_pool("ex_pool");

	memset(&sched, 0, sizeof(sched));
	sched.fid = 1;
	fiber_set_name(&sched, "sched");
	sched.gc_pool = palloc_create_pool(sched.name);

	sp = call_stack;
	fiber = &sched;
	last_used_fid = 100;
}

void
fiber_free(void)
{
	/* Only clean up if initialized. */
	if (fibers_registry) {
		fiber_destroy_all();
		mh_i32ptr_destroy(fibers_registry);
	}
}
