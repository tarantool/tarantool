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
#include <third_party/khash.h>

#include <fiber.h>
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
#include "diagnostics.h"

static struct fiber sched;
struct fiber *fiber = &sched;
static struct fiber **sp, *call_stack[64];
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

struct fiber_msg {
	u32 fid;
	u32 data_len;
	u8 data[];
};

static inline struct fiber_msg *
fiber_msg(const struct tbuf *buf)
{
	return buf->data;
}

KHASH_MAP_INIT_INT(fid2fiber, void *, realloc);
static khash_t(fid2fiber) *fibers_registry;

static void
update_last_stack_frame(struct fiber *fiber)
{
#ifdef ENABLE_BACKTRACE
	fiber->last_stack_frame = frame_addess();
#else
	(void)fiber;
#endif /* ENABLE_BACKTRACE */
}

void
fiber_call(struct fiber *callee)
{
	struct fiber *caller = fiber;

	assert(sp - call_stack < 8);
	assert(caller);

	fiber = callee;
	*sp++ = caller;

	update_last_stack_frame(caller);

	callee->csw++;
	coro_transfer(&caller->coro.ctx, &callee->coro.ctx);
}

void
fiber_raise(struct fiber *callee, jmp_buf exc, int value)
{
	struct fiber *caller = fiber;

	assert(sp - call_stack < 8);
	assert(caller);

	fiber = callee;
	*sp++ = caller;

	update_last_stack_frame(caller);

	callee->csw++;
	coro_save_and_longjmp(&caller->coro.ctx, exc, value);
}

void
yield(void)
{
	struct fiber *callee = *(--sp);
	struct fiber *caller = fiber;

	fiber = callee;
	update_last_stack_frame(caller);

	callee->csw++;
	coro_transfer(&caller->coro.ctx, &callee->coro.ctx);
}

void
fiber_sleep(ev_tstamp delay)
{
	ev_timer_set(&fiber->timer, delay, 0.);
	ev_timer_start(&fiber->timer);
	yield();
}


/** Wait for a forked child to complete. */

void
wait_for_child(pid_t pid)
{
	ev_child_set(&fiber->cw, pid, 0);
	ev_child_start(&fiber->cw);
	yield();
	ev_child_stop(&fiber->cw);
}

void
wait_for(int events)
{
	ev_io *io = &fiber->io;

	if (io->fd != fiber->fd || io->events != events) {	/* events are not monitored */
		if (ev_is_active(io))
			ev_io_stop(io);
		ev_io_set(io, fiber->fd, events);
	}

	if (!ev_is_active(io))
		ev_io_start(io);

	yield();
}

void
unwait(int events)
{
	ev_io *io = &fiber->io;
	assert(io->fd == fiber->fd);

	if (!ev_is_active(io))
		return;

	if ((io->events & events) == 0)
		return;

	ev_io_stop(io);
}

static void
ev_schedule(ev_watcher *watcher, int event __unused__)
{
	assert(fiber == &sched);
	fiber_call(watcher->data);
}

static struct fiber *
fid2fiber(int fid)
{
	khiter_t k = kh_get(fid2fiber, fibers_registry, fid);

	if (k == kh_end(fibers_registry))
		return NULL;
	if (!kh_exist(fibers_registry, k))
		return NULL;
	return kh_value(fibers_registry, k);
}

static void
register_fid(struct fiber *fiber)
{
	int ret;
	khiter_t k = kh_put(fid2fiber, fibers_registry, fiber->fid, &ret);
	kh_key(fibers_registry, k) = fiber->fid;
	kh_value(fibers_registry, k) = fiber;
}

static void
unregister_fid(struct fiber *fiber)
{
	khiter_t k = kh_get(fid2fiber, fibers_registry, fiber->fid);
	kh_del(fid2fiber, fibers_registry, k);
}

static void
clear_inbox(struct fiber *fiber)
{
	for (size_t i = 0; i < fiber->inbox->size; i++)
		fiber->inbox->ring[i] = NULL;
	fiber->inbox->head = fiber->inbox->tail = 0;
}

static void
fiber_alloc(struct fiber *fiber)
{
	prelease(fiber->pool);
	fiber->rbuf = tbuf_alloc(fiber->pool);
	fiber->iov = tbuf_alloc(fiber->pool);
	fiber->cleanup = tbuf_alloc(fiber->pool);

	fiber->iov_cnt = 0;
	clear_inbox(fiber);
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
	int i = fiber->cleanup->len / sizeof(struct fiber_cleanup);

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

	if (palloc_allocated(fiber->pool) < 128 * 1024)
		return;

	tmp = fiber->pool;
	fiber->pool = ex_pool;
	ex_pool = tmp;
	palloc_set_name(fiber->pool, fiber->name);
	palloc_set_name(ex_pool, "ex_pool");

	fiber->rbuf = tbuf_clone(fiber->pool, fiber->rbuf);
	fiber->cleanup = tbuf_clone(fiber->pool, fiber->cleanup);

	struct tbuf *new_iov = tbuf_alloc(fiber->pool);
	for (int i = 0; i < fiber->iov_cnt; i++) {
		struct iovec *v;
		size_t o = tbuf_reserve(new_iov, sizeof(*v));
		v = new_iov->data + o;
		memcpy(v, iovec(fiber->iov) + i, sizeof(*v));
	}
	fiber->iov = new_iov;

	for (int i = 0; i < fiber->inbox->size; i++) {
		struct msg *ri = fiber->inbox->ring[i];
		if (ri != NULL) {
			fiber->inbox->ring[i] = palloc(fiber->pool, sizeof(*ri));
			fiber->inbox->ring[i]->sender_fid = ri->sender_fid;
			fiber->inbox->ring[i]->msg = tbuf_clone(fiber->pool, ri->msg);
		}
	}

	prelease(ex_pool);
}


/** Destroy the currently active fiber and prepare it for reuse.
 */

static void
fiber_zombificate()
{
	diag_clear();
	fiber_set_name(fiber, "zombie");
	fiber->f = NULL;
	fiber->data = NULL;
	unregister_fid(fiber);
	fiber->fid = 0;
	fiber_alloc(fiber);

	SLIST_INSERT_HEAD(&zombie_fibers, fiber, zombie_link);
}

static void
fiber_loop(void *data __unused__)
{
	while (42) {
		assert(fiber != NULL && fiber->f != NULL && fiber->fid != 0);
		if (setjmp(fiber->exc) == 0) {
			fiber->f(fiber->f_data);
		} else {
			panic("fiber %s failure, exiting", fiber->name);
		}

		fiber_close();
		fiber_zombificate();
		yield();	/* give control back to scheduler */
	}
}

#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
#endif

/** Set fiber name. 
* @Param[in] name the new name of the fiber. Truncated to FIBER_NAME_MAXLEN.
*/

void
fiber_set_name(struct fiber *fiber, const char *name)
{
	assert(name != NULL);
	snprintf(fiber->name, sizeof(fiber->name), "%s", name);
}

/* fiber never dies, just become zombie */
struct fiber *
fiber_create(const char *name, int fd, int inbox_size, void (*f) (void *), void *f_data)
{
	struct fiber *fiber = NULL;
	if (inbox_size <= 0)
		inbox_size = 64;

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

		fiber->pool = palloc_create_pool(name);
		fiber->inbox = palloc(eter_pool, (sizeof(*fiber->inbox) +
						  inbox_size * sizeof(struct tbuf *)));
		fiber->inbox->size = inbox_size;

		fiber_alloc(fiber);
		ev_init(&fiber->io, (void *)ev_schedule);
		ev_init(&fiber->timer, (void *)ev_schedule);
		ev_init(&fiber->cw, (void *)ev_schedule);
		fiber->io.data = fiber->timer.data = fiber->cw.data = fiber;

		SLIST_INSERT_HEAD(&fibers, fiber, link);
	}

	fiber->fd = fd;
	fiber->f = f;
	fiber->f_data = f_data;
	while (++last_used_fid <= 100) ;	/* fids from 0 to 100 are reserved */
	fiber->fid = last_used_fid;
	fiber_set_name(fiber, name);
	palloc_set_name(fiber->pool, fiber->name);
	register_fid(fiber);

	return fiber;
}

char *
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

	unwait(-1);
	int r = close(fiber->fd);
	if (r != -1) {
		fiber->io.fd = fiber->fd = -1;
		fiber->has_peer = false;
		fiber->peer_name[0] = 0;
	}
	return r;
}

static int
ring_size(struct ring *inbox)
{
	return (inbox->size + inbox->head - inbox->tail) % inbox->size;
}

int
inbox_size(struct fiber *recipient)
{
	return ring_size(recipient->inbox);
}

void
wait_inbox(struct fiber *recipient)
{
	while (ring_size(recipient->inbox) == 0) {
		recipient->reading_inbox = true;
		yield();
		recipient->reading_inbox = false;
	}
}

bool
write_inbox(struct fiber *recipient, struct tbuf *msg)
{
	struct ring *inbox = recipient->inbox;
	if (ring_size(inbox) == inbox->size - 1)
		return false;

	inbox->ring[inbox->head] = palloc(recipient->pool, sizeof(struct msg));
	inbox->ring[inbox->head]->sender_fid = fiber->fid;
	inbox->ring[inbox->head]->msg = tbuf_clone(recipient->pool, msg);
	inbox->head = (inbox->head + 1) % inbox->size;

	if (recipient->reading_inbox)
		fiber_call(recipient);
	return true;
}

struct msg *
read_inbox(void)
{
	struct ring *restrict inbox = fiber->inbox;
	while (ring_size(inbox) == 0) {
		fiber->reading_inbox = true;
		yield();
		fiber->reading_inbox = false;
	}

	struct msg *msg = inbox->ring[inbox->tail];
	inbox->ring[inbox->tail] = NULL;
	inbox->tail = (inbox->tail + 1) % inbox->size;

	return msg;
}

int
fiber_bread(struct tbuf *buf, size_t at_least)
{
	ssize_t r;
	tbuf_ensure(buf, MAX(cfg.readahead, at_least));

	for (;;) {
		wait_for(EV_READ);
		r = read(fiber->fd, buf->data + buf->len, buf->size - buf->len);
		if (r > 0) {
			buf->len += r;
			if (buf->len >= at_least)
				break;
		} else {
			if (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
				continue;
			break;
		}
	}
	unwait(EV_READ);

	return r;
}

void
add_iov_dup(void *buf, size_t len)
{
	void *copy = palloc(fiber->pool, len);
	memcpy(copy, buf, len);
	add_iov(copy, len);
}

ssize_t
fiber_flush_output(void)
{
	ssize_t result, r = 0, bytes = 0;
	struct iovec *iov = iovec(fiber->iov);
	size_t iov_cnt = fiber->iov_cnt;

	while (iov_cnt > 0) {
		wait_for(EV_WRITE);
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
	unwait(EV_WRITE);

	if (r < 0) {
		size_t rem = 0;
		for (int i = 0; i < iov_cnt; i++)
			rem += iov[i].iov_len;

		say_syserror("client unexpectedly gone, %" PRI_SZ " bytes unwritten", rem);
		result = r;
	} else
		result = bytes;

	fiber->iov_cnt = 0;	/* discard anything unwritten */
	tbuf_reset(fiber->iov);
	return result;
}

ssize_t
fiber_read(void *buf, size_t count)
{
	ssize_t r, done = 0;

	if (count == 0)
		return 0;

	while (count != done) {
		wait_for(EV_READ);

		if ((r = read(fiber->fd, buf + done, count - done)) <= 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				continue;
			else
				break;
		}
		done += r;
	}

	unwait(EV_READ);
	return done;
}

ssize_t
fiber_write(const void *buf, size_t count)
{
	int r;
	unsigned int done = 0;

	if (count == 0)
		return 0;

	while (count != done) {
		wait_for(EV_WRITE);
		if ((r = write(fiber->fd, buf + done, count - done)) == -1) {
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				continue;
			else
				break;
		}
		done += r;
	}

	unwait(EV_WRITE);
	return done;
}

int
fiber_connect(struct sockaddr_in *addr)
{
	int error;
	socklen_t error_size = sizeof(error);

	fiber->fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fiber->fd < 0)
		goto error;

	if (set_nonblock(fiber->fd) < 0)
		goto error;

	if (connect(fiber->fd, (struct sockaddr *)addr, sizeof(*addr)) < 0) {
		if (errno != EINPROGRESS)
			goto error;
	}

	wait_for(EV_WRITE);
	if (getsockopt(fiber->fd, SOL_SOCKET, SO_ERROR, &error, &error_size) < 0)
		goto error;

	assert(error_size == sizeof(error));

	if (error != 0) {
		errno = error;
		goto error;
	}

	unwait(EV_WRITE);
	return fiber->fd;
      error:
	unwait(EV_WRITE);
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

static int
read_atleast(int fd, struct tbuf *b, size_t to_read)
{
	tbuf_ensure(b, to_read);
	while (to_read > 0) {
		int r = read(fd, b->data + b->len, to_read);
		if (r <= 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		to_read -= r;
		b->len += r;
	}
	return 0;
}

/** Write all data to a socket.
 *
 * This function is equivalent to 'write', except it would ensure
 * that all data is written to the file unless a non-ignorable
 * error occurs.
 *
 * @retval 0  Success
 *
 * @reval  1  An error occurred (not EINTR).
 */
static int
write_all(int fd, void *data, size_t len)
{
	while (len > 0) {
		ssize_t r = write(fd, data, len);
		if (r < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		data += r;
		len -= r;
	}
	return 0;
}

void __attribute__ ((noreturn))
blocking_loop(int fd, struct tbuf *(*handler) (void *state, struct tbuf *), void *state)
{
	struct tbuf *request, *request_body, *reply, *reply_body;
	u32 *request_size, reply_size;
	int result = EXIT_FAILURE;

	for (;;) {
		request = tbuf_alloc(fiber->pool);
		if (read_atleast(fd, request, sizeof(u32)) < 0) {
			result = EXIT_SUCCESS;
			break;
		}

		if ((request_size = tbuf_peek(request, sizeof(u32))) == NULL) {
			result = EXIT_SUCCESS;
			break;
		}
		*request_size = ntohl(*request_size);

		if (read_atleast(fd, request, *request_size) < 0) {
			result = EXIT_SUCCESS;
			break;
		}

		request_body = tbuf_alloc(fiber->pool);
		tbuf_append(request_body, fiber_msg(request)->data, fiber_msg(request)->data_len);

		reply_body = handler(state, request_body);

		reply_size = sizeof(struct fiber_msg) + reply_body->len;
		reply = tbuf_alloc(fiber->pool);
		tbuf_reserve(reply, reply_size);

		fiber_msg(reply)->fid = fiber_msg(request)->fid;
		fiber_msg(reply)->data_len = reply_body->len;
		memcpy(fiber_msg(reply)->data, reply_body->data, reply_body->len);

		reply_size = htonl(reply_size);
		if (write_all(fd, &reply_size, sizeof(reply_size)) < 0) {
			result = EXIT_FAILURE;
			break;
		}
		if (write_all(fd, reply->data, reply->len) < 0) {
			result = EXIT_FAILURE;
			break;
		}

		prelease(fiber->pool);
	}

	handler(state, NULL);
	exit(result);
}

static void
inbox2sock(void *_data __unused__)
{
	struct tbuf *msg, *out;
	struct msg *m;
	u32 len;

	for (;;) {
		out = tbuf_alloc(fiber->pool);

		do {
			m = read_inbox();
			msg = tbuf_alloc(fiber->pool);

			/* TODO: do not copy message twice */
			tbuf_reserve(msg, sizeof(struct fiber_msg) + m->msg->len);
			fiber_msg(msg)->fid = m->sender_fid;
			fiber_msg(msg)->data_len = m->msg->len;
			memcpy(fiber_msg(msg)->data, m->msg->data, m->msg->len);
			len = htonl(msg->len);

			tbuf_append(out, &len, sizeof(len));
			tbuf_append(out, msg->data, msg->len);
		} while (ring_size(fiber->inbox) > 0);

		if (fiber_write(out->data, out->len) != out->len)
			panic("child is dead");
		fiber_gc();
		unwait(-1);
	}
}

static void
sock2inbox(void *_data __unused__)
{
	struct tbuf *msg, *msg_body;
	struct fiber *recipient;
	u32 len;

	for (;;) {
		if (fiber->rbuf->len < sizeof(len)) {
			if (fiber_bread(fiber->rbuf, sizeof(len)) <= 0)
				panic("child is dead");
		}

		len = read_u32(fiber->rbuf);

		len = ntohl(len);
		if (fiber->rbuf->len < len) {
			if (fiber_bread(fiber->rbuf, len) <= 0)
				panic("child is dead");
		}

		msg = tbuf_split(fiber->rbuf, len);
		recipient = fid2fiber(fiber_msg(msg)->fid);
		if (recipient == NULL) {
			say_error("recipient is lost");
			continue;
		}

		msg_body = tbuf_alloc(recipient->pool);
		tbuf_append(msg_body, fiber_msg(msg)->data, fiber_msg(msg)->data_len);
		write_inbox(recipient, msg_body);
		fiber_gc();
	}
}

struct child *
spawn_child(const char *name, int inbox_size, struct tbuf *(*handler) (void *, struct tbuf *),
	    void *state)
{
	char *proxy_name;
	int socks[2];
	int pid;

	if (socketpair(AF_UNIX, SOCK_STREAM, 0, socks) == -1) {
		say_syserror("socketpair");
		return NULL;
	}

	if ((pid = fork()) == -1) {
		say_syserror("fork");
		return NULL;
	}

	if (pid) {
		close(socks[0]);
		if (set_nonblock(socks[1]) == -1)
			return NULL;

		struct child *c = palloc(eter_pool, sizeof(*c));
		c->pid = pid;

		proxy_name = palloc(eter_pool, 64);
		snprintf(proxy_name, 64, "%s/sock2inbox", name);
		c->in = fiber_create(proxy_name, socks[1], inbox_size, sock2inbox, NULL);
		fiber_call(c->in);
		proxy_name = palloc(eter_pool, 64);
		snprintf(proxy_name, 64, "%s/inbox2sock", name);
		c->out = fiber_create(proxy_name, socks[1], inbox_size, inbox2sock, NULL);
		c->out->reading_inbox = true;
		return c;
	} else {
		char child_name[sizeof(fiber->name)];

		salloc_destroy();
		close_all_xcpt(2, socks[0], sayfd);
		snprintf(child_name, sizeof(child_name), "%s/child", name);
		fiber_set_name(&sched, child_name);
		set_proc_title(name);
		say_crit("%s initialized", name);
		blocking_loop(socks[0], handler, state);
	}
}

static void
tcp_server_handler(void *data)
{
	struct fiber_server *server = fiber->data;
	struct fiber *h;
	char name[64];
	int fd;
	bool warning_said = false;
	int one = 1;
	struct sockaddr_in sin;
	struct linger ling = { 0, 0 };

	if ((fiber->fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1) {
		say_syserror("socket");
		exit(EX_OSERR);
	}

	if (setsockopt(fiber->fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) == -1 ||
	    setsockopt(fiber->fd, SOL_SOCKET, SO_KEEPALIVE, &one, sizeof(one)) == -1 ||
	    setsockopt(fiber->fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one)) == -1 ||
	    setsockopt(fiber->fd, SOL_SOCKET, SO_LINGER, &ling, sizeof(ling)) == -1) {
		say_syserror("setsockopt");
		exit(EX_OSERR);
	}

	if (set_nonblock(fiber->fd) == -1)
		exit(EX_OSERR);

	memset(&sin, 0, sizeof(struct sockaddr_in));
	sin.sin_family = AF_INET;
	sin.sin_port = htons(server->port);
	sin.sin_addr.s_addr = INADDR_ANY;

	for (;;) {
		if (bind(fiber->fd, (struct sockaddr *)&sin, sizeof(sin)) == -1) {
			if (errno == EADDRINUSE)
				goto sleep_and_retry;
			say_syserror("bind");
			exit(EX_OSERR);
		}
		if (listen(fiber->fd, cfg.backlog) == -1) {
			if (errno == EADDRINUSE)
				goto sleep_and_retry;
			say_syserror("listen");
			exit(EX_OSERR);
		}

		say_info("bound to TCP port %i", server->port);
		break;

	      sleep_and_retry:
		if (!warning_said) {
			say_warn("port %i is already in use, "
				 "will retry binding after 0.1 seconds.", server->port);
			warning_said = true;
		}
		fiber_sleep(0.1);
	}

	if (server->on_bind != NULL)
		server->on_bind(server->data);

	while (1) {
		wait_for(EV_READ);

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
			h = fiber_create(name, fd, -1, server->handler, data);
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
}

static void
udp_server_handler(void *data)
{
	struct fiber_server *server = fiber->data;
	bool warning_said = false;
	struct sockaddr_in sin;

	if ((fiber->fd = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
		say_syserror("socket");
		exit(EX_OSERR);
	}

	if (set_nonblock(fiber->fd) == -1)
		exit(EX_OSERR);

	memset(&sin, 0, sizeof(struct sockaddr_in));
	sin.sin_family = AF_INET;
	sin.sin_port = htons(server->port);
	sin.sin_addr.s_addr = INADDR_ANY;

	for (;;) {
		if (bind(fiber->fd, (struct sockaddr *)&sin, sizeof(sin)) == -1) {
			if (errno == EADDRINUSE)
				goto sleep_and_retry;
			say_syserror("bind");
			exit(EX_OSERR);
		}

		say_info("bound to UDP port %i", server->port);
		break;

	      sleep_and_retry:
		if (!warning_said) {
			say_warn("port %i is already in use, "
				 "will retry binding after 0.1 seconds.", server->port);
			warning_said = true;
		}
		fiber_sleep(0.1);
	}

	if (server->on_bind != NULL)
		server->on_bind(server->data);

	while (1) {
#define MAXUDPPACKETLEN	128
		char buf[MAXUDPPACKETLEN];
		struct sockaddr_in addr;
		socklen_t addrlen;
		ssize_t sz;

		wait_for(EV_READ);

		for (;;) {
			addrlen = sizeof(addr);
			sz = recvfrom(fiber->fd, buf, MAXUDPPACKETLEN, MSG_DONTWAIT,
				      (struct sockaddr *)&addr, &addrlen);

			if (sz <= 0) {
				if (!(errno == EAGAIN || errno == EWOULDBLOCK))
					say_syserror("recvfrom");
				break;
			} else {
				if (server->handler) {
					server->handler(data);
				} else {
					void (*f) (char *, int) = data;
					f(buf, (int)sz);
				}
			}
		}
	}
}

struct fiber *
fiber_server(fiber_server_type type, int port, void (*handler) (void *data), void *data,
	     void (*on_bind) (void *data))
{
	char *server_name;
	struct fiber_server *server;
	struct fiber *s;

	server_name = palloc(eter_pool, 64);
	snprintf(server_name, 64, "%i/acceptor", port);
	s = fiber_create(server_name, -1, -1,
			 (type == tcp_server) ? tcp_server_handler : udp_server_handler, data);
	s->data = server = palloc(eter_pool, sizeof(struct fiber_server));
	assert(server != NULL);
	server->port = port;
	server->handler = handler;
	server->on_bind = on_bind;

	fiber_call(s);		/* give a handler a chance */
	return s;
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
		tbuf_printf(out, "    inbox: %i" CRLF, ring_size(fiber->inbox));
		tbuf_printf(out, "    fd: %4i" CRLF, fiber->fd);
		tbuf_printf(out, "    peer: %s" CRLF, fiber_peer_name(fiber));
		tbuf_printf(out, "    stack: %p" CRLF, stack_top);
		tbuf_printf(out, "    exc: %p" CRLF,
			    ((void **)fiber->exc)[3]);
		tbuf_printf(out, "    exc_frame: %p,"CRLF, ((void **)fiber->exc)[3] + 2 * sizeof(void *));
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
	fibers_registry = kh_init(fid2fiber, NULL);

	ex_pool = palloc_create_pool("ex_pool");

	memset(&sched, 0, sizeof(sched));
	sched.fid = 1;
	fiber_set_name(&sched, "sched");
	sched.pool = palloc_create_pool(sched.name);

	sp = call_stack;
	fiber = &sched;
	last_used_fid = 100;
}
