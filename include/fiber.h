#ifndef TARANTOOL_FIBER_H_INCLUDED
#define TARANTOOL_FIBER_H_INCLUDED
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

#include "config.h"

#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/uio.h>
#include <netinet/in.h>

#include <tarantool_ev.h>
#include <tbuf.h>
#include <coro.h>
#include <util.h>
#include "third_party/queue.h"

#include "exception.h"
#include "palloc.h"

#define FIBER_NAME_MAXLEN 32

#define FIBER_READING_INBOX 0x1
/** Can this fiber be cancelled? */
#define FIBER_CANCELLABLE   0x2
/** Indicates that a fiber has been cancelled. */
#define FIBER_CANCEL        0x4

/** This is thrown by fiber_* API calls when the fiber is
 * cancelled.
 */

@interface FiberCancelException: tnt_Exception
@end

struct msg {
	uint32_t sender_fid;
	struct tbuf *msg;
};

struct ring {
	size_t size, head, tail;
	struct msg *ring[];
};

struct fiber {
	ev_io io;
	ev_async async;
#ifdef ENABLE_BACKTRACE
	void *last_stack_frame;
#endif
	int csw;
	struct tarantool_coro coro;
	/* A garbage-collected memory pool. */
	struct palloc_pool *gc_pool;
	uint32_t fid;
	int fd;

	ev_timer timer;
	ev_child cw;

	struct tbuf *iov;
	size_t iov_cnt;
	struct tbuf *rbuf;
	struct tbuf *cleanup;

	SLIST_ENTRY(fiber) link, zombie_link;

	struct ring *inbox;

	/* ASCIIZ name of this fiber. */
	char name[FIBER_NAME_MAXLEN];
	void (*f) (void *);
	void *f_data;
	/* Store execution context in a fiber. */
	union {
		struct box_txn *txn;
	} mod_data;

	u64 cookie;
	bool has_peer;
	/* ASCIIZ name of the peer, if there is one. */
	char peer_name[32];

	u32 flags;

	struct fiber *waiter;
};

SLIST_HEAD(, fiber) fibers, zombie_fibers;

struct child {
	pid_t pid;
	struct fiber *in, *out;
};

static inline struct iovec *iovec(const struct tbuf *t)
{
	return (struct iovec *)t->data;
}

typedef void (*fiber_cleanup_handler) (void *);
void fiber_register_cleanup(fiber_cleanup_handler handler, void *data);

extern struct fiber *fiber;

void fiber_init(void);
void fiber_free(void);
struct fiber *fiber_create(const char *name, int fd, int inbox_size, void (*f) (void *), void *);
void fiber_set_name(struct fiber *fiber, const char *name);
void wait_for_child(pid_t pid);

void
fiber_io_start(int fd, int events);

void
fiber_io_yield();

void
fiber_io_stop(int fd, int events);

void
fiber_yield(void);
void fiber_destroy_all();

bool
fiber_is_caller(struct fiber *f);

struct msg *read_inbox(void);
ssize_t fiber_bread(struct tbuf *, size_t v);

inline static void iov_add_unsafe(const void *buf, size_t len)
{
	struct iovec *v;
	assert(fiber->iov->capacity - fiber->iov->size >= sizeof(*v));
	v = fiber->iov->data + fiber->iov->size;
	v->iov_base = (void *)buf;
	v->iov_len = len;
	fiber->iov->size += sizeof(*v);
	fiber->iov_cnt++;
}

inline static void iov_ensure(size_t count)
{
	tbuf_ensure(fiber->iov, sizeof(struct iovec) * count);
}

/* Add to fiber's iov vector. */
inline static void iov_add(const void *buf, size_t len)
{
	iov_ensure(1);
	iov_add_unsafe(buf, len);
}

inline static void iov_dup(const void *buf, size_t len)
{
	void *copy = palloc(fiber->gc_pool, len);
	memcpy(copy, buf, len);
	iov_add(copy, len);
}

/* Reset the fiber's iov vector. */
ssize_t iov_flush(void);
/* Write everything in the fiber's iov vector to fiber socket. */
void iov_reset();

bool write_inbox(struct fiber *recipient, struct tbuf *msg);
int inbox_size(struct fiber *recipient);
void wait_inbox(struct fiber *recipient);

const char *fiber_peer_name(struct fiber *fiber);
ssize_t fiber_read(void *buf, size_t count);
ssize_t fiber_write(const void *buf, size_t count);
int fiber_close(void);
void fiber_cleanup(void);
void fiber_gc(void);
bool fiber_checkstack();
void fiber_call(struct fiber *callee);
void fiber_wakeup(struct fiber *f);
struct fiber *fiber_find(int fid);
/** Cancel a fiber. A cancelled fiber will have
 * tnt_FiberCancelException raised in it.
 *
 * A fiber can be cancelled only if it is
 * FIBER_CANCELLABLE flag is set.
 */
void fiber_cancel(struct fiber *f);
/** Check if the current fiber has been cancelled.  Raises
 * tnt_FiberCancelException
 */
void fiber_testcancel(void);
/** Make it possible or not possible to cancel the current
 * fiber.
 */
void fiber_setcancelstate(bool enable);
int fiber_connect(struct sockaddr_in *addr);
void fiber_sleep(ev_tstamp s);
void fiber_info(struct tbuf *out);
int set_nonblock(int sock);

typedef void (*fiber_server_callback)(void *);

struct fiber *fiber_server(const char *name, int port,
			   fiber_server_callback callback, void *,
			   void (*on_bind) (void *));

/**
 * Create server socket and bind his on port. cfd.bind_ipaddr param using as IP address.
 *
 * @param type the fiber server type (TCP or UDP)
 * @param port the bind ip port.
 * @param retry the retry flag, if flag up the function will be try again to bind
 *              socket after delay.
 * @param delay the bind socket retry delay in sec.
 *
 * @return on success, zero is returned. on error, -1 is returned.
 */
int
fiber_serv_socket(struct fiber *fiber, unsigned short port, bool retry, ev_tstamp delay);

struct child *spawn_child(const char *name,
			  int inbox_size,
			  struct tbuf *(*handler) (void *, struct tbuf *), void *state);

#endif /* TARANTOOL_FIBER_H_INCLUDED */
