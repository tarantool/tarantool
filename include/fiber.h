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

#ifndef TARANTOOL_FIBER_H
#define TARANTOOL_FIBER_H

#include <stdint.h>
#include <unistd.h>
#include <sys/uio.h>
#include <setjmp.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <tarantool_ev.h>
#include <palloc.h>
#include <tbuf.h>
#include <say.h>
#include <coro.h>
#include <util.h>

#define FIBER_EXIT -1

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
#ifdef BACKTRACE
	void *last_stack_frame;
#endif
	int csw;
	struct tarantool_coro coro;
	struct palloc_pool *pool;
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

	jmp_buf exc;
	const char *errstr;

	const char *name;
	void (*f) (void *);
	void *f_data;

	void *data;
	/** Information about the last error. */
	void *diagnostics;

	u64 cookie;
	bool has_peer;
	char peer_name[32];
	bool reading_inbox;
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
struct fiber *fiber_create(const char *name, int fd, int inbox_size, void (*f) (void *), void *);
void wait_for(int events);
void wait_for_child(pid_t pid);
void unwait(int events);
void yield(void);
void raise_(int);
#define raise(v, err)							\
	({								\
		say_debug("raise 0x%x/%s at %s:%i", v, err, __FILE__, __LINE__); \
		fiber->errstr = (err);					\
		longjmp(fiber->exc, (v));				\
	})

struct msg *read_inbox(void);
int fiber_bread(struct tbuf *, size_t v);

inline static void add_iov_unsafe(void *buf, size_t len)
{
	struct iovec *v;
	assert(fiber->iov->size - fiber->iov->len >= sizeof(*v));
	v = fiber->iov->data + fiber->iov->len;
	v->iov_base = buf;
	v->iov_len = len;
	fiber->iov->len += sizeof(*v);
	fiber->iov_cnt++;
}

inline static void iov_ensure(size_t count)
{
	tbuf_ensure(fiber->iov, sizeof(struct iovec) * count);
}

inline static void add_iov(void *buf, size_t len)
{
	iov_ensure(1);
	add_iov_unsafe(buf, len);
}

void add_iov_dup(void *buf, size_t len);
bool write_inbox(struct fiber *recipient, struct tbuf *msg);
int inbox_size(struct fiber *recipient);
void wait_inbox(struct fiber *recipient);

char *fiber_peer_name(struct fiber *fiber);
ssize_t fiber_read(void *buf, size_t count);
ssize_t fiber_write(const void *buf, size_t count);
int fiber_close(void);
ssize_t fiber_flush_output(void);
void fiber_cleanup(void);
void fiber_gc(void);
void fiber_call(struct fiber *callee);
void fiber_raise(struct fiber *callee, jmp_buf exc, int value);
int fiber_connect(struct sockaddr_in *addr);
void fiber_sleep(ev_tstamp s);
void fiber_info(struct tbuf *out);
int set_nonblock(int sock);

typedef enum fiber_server_type {
	tcp_server,
	udp_server
} fiber_server_type;

struct fiber *fiber_server(fiber_server_type type, int port, void (*handler) (void *), void *,
			   void (*on_bind) (void *));

struct child *spawn_child(const char *name,
			  int inbox_size,
			  struct tbuf *(*handler) (void *, struct tbuf *), void *state);

#endif
