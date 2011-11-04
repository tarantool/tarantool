
/*
 * Copyright (C) 2011 Mail.RU
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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>

#include <tnt_queue.h>
#include <tnt_error.h>
#include <tnt_mem.h>
#include <tnt_opt.h>
#include <tnt_buf.h>
#include <tnt_main.h>
#include <tnt_io.h>

#if !defined(MIN)
#	define MIN(a, b) (a) < (b) ? (a) : (b)
#endif /* !defined(MIN) */

static enum tnt_error
tnt_io_resolve(struct sockaddr_in *addr,
	       const char *hostname, unsigned short port)
{
	memset(addr, 0, sizeof(struct sockaddr_in));
	addr->sin_family = AF_INET;
	addr->sin_port = htons(port);
	struct addrinfo *addr_info = NULL;
	if (getaddrinfo(hostname, NULL, NULL, &addr_info) == 0) {
		memcpy(&addr->sin_addr,
		       (void*)&((struct sockaddr_in *)addr_info->ai_addr)->sin_addr,
		       sizeof(addr->sin_addr));
		freeaddrinfo(addr_info);
		return TNT_EOK;
	}
	if (addr_info)
		freeaddrinfo(addr_info);
	return TNT_ERESOLVE;
}

static enum tnt_error
tnt_io_nonblock(struct tnt *t, int set)
{
	int flags = fcntl(t->fd, F_GETFL);
	if (flags == -1) {
		t->errno_ = errno;
		return TNT_ESYSTEM;
	}
	if (set)
		flags |= O_NONBLOCK;
	else
		flags &= ~O_NONBLOCK;
	if (fcntl(t->fd, F_SETFL, flags) == -1) {
		t->errno_ = errno;
		return TNT_ESYSTEM;
	}
	return TNT_EOK;
}

static enum tnt_error
tnt_io_connect_do(struct tnt *t, char *host, int port)
{
	struct sockaddr_in addr;
	enum tnt_error result = tnt_io_resolve(&addr, host, port);
	if (result != TNT_EOK)
		return result;

	result = tnt_io_nonblock(t, 1);
	if (result != TNT_EOK)
		return result;

	if (connect(t->fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
		if (errno == EINPROGRESS) {
			/** waiting for connection while handling signal events */
			const int64_t micro = 1000000;
			int64_t tmout_usec = t->opt.tmout_connect * micro;
			/* get start connect time */
			struct timeval start_connect;
			if (gettimeofday(&start_connect, NULL) == -1) {
				t->errno_ = errno;
				return TNT_ESYSTEM;
			}

			/* set initial timer */
			struct timeval tmout;
			tmout.tv_sec  = t->opt.tmout_connect;
			tmout.tv_usec = 0;
			while (1) {
				fd_set fds;
				FD_ZERO(&fds);
				FD_SET(t->fd, &fds);

				int ret = select(t->fd + 1, NULL, &fds, NULL, &tmout);
				if (ret == -1) {
					if (errno == EINTR || errno == EAGAIN) {
						/* get current time */
						struct timeval curr;
						if (gettimeofday(&curr, NULL) == -1) {
							t->errno_ = errno;
							return TNT_ESYSTEM;
						}

						/* calculate timeout last time */
						int64_t passd_usec = (curr.tv_sec - start_connect.tv_sec) * micro +
							(curr.tv_usec - start_connect.tv_usec);
						int64_t curr_tmeout = passd_usec - tmout_usec;
						if (curr_tmeout <= 0) {
							/* timeout */
							return TNT_ETMOUT;
						}

						tmout.tv_sec = curr_tmeout / micro;
						tmout.tv_usec = curr_tmeout % micro;
					} else {
						t->errno_ = errno;
						return TNT_ESYSTEM;
					}
				} else if (ret == 0) {
					/* timeout */
					return TNT_ETMOUT;
				} else {
					/* we have a event on socket */
					break;
				}
			}

			int opt = 0;
			socklen_t len = sizeof(opt);
			if ((getsockopt(t->fd, SOL_SOCKET, SO_ERROR, &opt, &len) == -1) || opt) {
				t->errno_ = (opt) ? opt: errno;
				return TNT_ESYSTEM;
			}
		} else {
			t->errno_ = errno;
			return TNT_ESYSTEM;
		}
	}

	result = tnt_io_nonblock(t, 0);
	if (result != TNT_EOK)
		return result;

	return TNT_EOK;
}

static enum tnt_error
tnt_io_xbufmax(struct tnt *t, int opt, int min)
{
	int max = 128 * 1024 * 1024;
	if (min == 0)
		min = 16384;
	unsigned int avg = 0;
	while (min <= max) {
		avg = ((unsigned int)(min + max)) / 2;
		if (setsockopt(t->fd, SOL_SOCKET, opt, &avg, sizeof(avg)) == 0)
			min = avg + 1;
		else
			max = avg - 1;
	}
	return TNT_EOK;
}

static enum tnt_error
tnt_io_setopts(struct tnt *t)
{
	int opt = 1;
	if (setsockopt(t->fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt)) == -1)
		goto error;

	tnt_io_xbufmax(t, SO_SNDBUF, t->opt.send_buf);
	tnt_io_xbufmax(t, SO_RCVBUF, t->opt.recv_buf);

	if (t->opt.tmout_send) {
		struct timeval tmout;
		tmout.tv_sec  = t->opt.tmout_send;
		tmout.tv_usec = t->opt.tmout_send_ms;
		if (setsockopt(t->fd, SOL_SOCKET, SO_SNDTIMEO, &tmout, sizeof(tmout)) == -1)
			goto error;
	}
	if (t->opt.tmout_recv) {
		struct timeval tmout;
		tmout.tv_sec  = t->opt.tmout_recv;
		tmout.tv_usec = t->opt.tmout_recv_ms;
		if (setsockopt(t->fd, SOL_SOCKET, SO_RCVTIMEO, &tmout, sizeof(tmout)) == -1)
			goto error;
	}
	return TNT_EOK;
error:
	t->errno_ = errno;
	return TNT_ESYSTEM;
}

enum tnt_error
tnt_io_connect(struct tnt *t, char *host, int port)
{
	t->fd = socket(AF_INET, SOCK_STREAM, 0);
	if (t->fd < 0) {
		t->errno_ = errno;
		return TNT_ESYSTEM;
	}
	enum tnt_error result = tnt_io_setopts(t);
	if (result != TNT_EOK)
		goto out;
	result = tnt_io_connect_do(t, host, port);
	if (result != TNT_EOK)
		goto out;
	t->connected = 1;
	return TNT_EOK;
out:
	tnt_io_close(t);
	return result;
}

void
tnt_io_close(struct tnt *t)
{
	if (t->fd > 0) {
		close(t->fd);
		t->fd = 0;
	}
	t->connected = 0;
}

enum tnt_error
tnt_io_flush(struct tnt *t)
{
	if (t->sbuf.off == 0)
		return TNT_EOK;
	enum tnt_error r = tnt_io_send(t, t->sbuf.buf, t->sbuf.off);
	if (r != TNT_EOK)
		return r;
	t->sbuf.off = 0;
	return TNT_EOK;
}

ssize_t
tnt_io_send_raw(struct tnt *t, char *buf, size_t size)
{
	ssize_t result;
	if (t->sbuf.tx) {
		result = t->sbuf.tx(t->sbuf.buf, buf, size);
	} else {
		do {
			result = send(t->fd, buf, size, 0);
		} while (result == -1 && (errno == EINTR));
		if (result <= 0)
			t->errno_ = errno;
	}
	return result;
}

ssize_t
tnt_io_sendv_raw(struct tnt *t, struct iovec *iov, int count)
{
	ssize_t result;
	if (t->sbuf.txv) {
		result = t->sbuf.txv(t->sbuf.buf, iov, count);
	} else {
		do {
			result = writev(t->fd, iov, count);
		} while (result == -1 && (errno == EINTR));
		if (result <= 0)
			t->errno_ = errno;
	}
	return result;
}

enum tnt_error
tnt_io_send(struct tnt *t, char *buf, size_t size)
{
	ssize_t r, off = 0;
	do {
		r = tnt_io_send_raw(t, buf + off, size - off);
		if (r <= 0)
			return TNT_ESYSTEM;
		off += r;
	} while (off != size);
	return TNT_EOK;
}

/** sendv unbufferized version */
enum tnt_error
tnt_io_sendv_direct(struct tnt *t, struct iovec *iov, int count)
{
       	ssize_t r = 0;
	while (count > 0) {
		r = tnt_io_sendv_raw(t, iov, MIN(count, IOV_MAX));
		if (r <= 0)
			return TNT_ESYSTEM;
		while (count > 0) {
			if (iov->iov_len > r) {
				iov->iov_base += r;
				iov->iov_len -= r;
				break;
			} else {
				r -= iov->iov_len;
				iov++;
				count--;
			}
		}
	}
	return TNT_EOK;
}

inline static void
tnt_io_sendv_put(struct tnt *t, struct iovec *iov, int count)
{
	size_t i;
	for (i = 0 ; i < count ; i++) {
		memcpy(t->sbuf.buf + t->sbuf.off,
		       iov[i].iov_base,
		       iov[i].iov_len);
		t->sbuf.off += iov[i].iov_len;
	}
}

/** sendv bufferized version */
enum tnt_error
tnt_io_sendv(struct tnt *t, struct iovec *iov, int count)
{
	if (t->sbuf.buf == NULL)
		return tnt_io_sendv_direct(t, iov, count);

	size_t i, size = 0;
	for (i = 0 ; i < count ; i++)
		size += iov[i].iov_len;
	if (size > t->sbuf.size)
		return TNT_EBIG;

	if ((t->sbuf.off + size) <= t->sbuf.size) {
		tnt_io_sendv_put(t, iov, count);
		return TNT_EOK;
	}

	enum tnt_error r = tnt_io_send(t, t->sbuf.buf, t->sbuf.off);
	if (r != TNT_EOK)
		return r;

	t->sbuf.off = 0;
	tnt_io_sendv_put(t, iov, count);
	return TNT_EOK;
}

ssize_t
tnt_io_recv_raw(struct tnt *t, char *buf, size_t size)
{
	ssize_t result;
	if (t->rbuf.tx) {
		result = t->rbuf.tx(t->rbuf.buf, buf, size);
	} else {
		do {
			result = recv(t->fd, buf, size, 0);
		} while (result == -1 && (errno == EINTR));
		if (result <= 0)
			t->errno_ = errno;
	}
	return result;
}

inline static enum tnt_error
tnt_io_recv_asis(struct tnt *t, char *buf, size_t size, size_t off)
{
	do {
		ssize_t r = tnt_io_recv_raw(t, buf + off, size - off);
		if (r <= 0)
			return TNT_ESYSTEM;
		off += r;
	} while (off != size);
	return TNT_EOK;
}

enum tnt_error
tnt_io_recv(struct tnt *t, char *buf, size_t size)
{
	if (t->rbuf.buf == NULL)
		return tnt_io_recv_asis(t, buf, size, 0);

	size_t lv, rv, off = 0, left = size;
	while (1) {
		if ((t->rbuf.off + left) <= t->rbuf.top) {
			memcpy(buf + off, t->rbuf.buf + t->rbuf.off, left);
			t->rbuf.off += left;
			return TNT_EOK;
		}

		lv = t->rbuf.top - t->rbuf.off;
		rv = left - lv;
		if (lv) {
			memcpy(buf + off, t->rbuf.buf + t->rbuf.off, lv);
			off += lv;
		}

		t->rbuf.off = 0;
		ssize_t top = tnt_io_recv_raw(t, t->rbuf.buf, t->rbuf.size);
		if (top <= 0) {
			t->errno_ = errno;
			return TNT_ESYSTEM;
		}

		t->rbuf.top = top;
		if (rv <= t->rbuf.top) {
			memcpy(buf + off, t->rbuf.buf, rv);
			t->rbuf.off = rv;
			return TNT_EOK;
		}
		left -= lv;
	}

	return TNT_EOK;
}

enum tnt_error
tnt_io_recv_char(struct tnt *t, char buf[1])
{
	return tnt_io_recv(t, buf, 1);
}

enum tnt_error
tnt_io_recv_expect(struct tnt *t, char *sz)
{
	char buf[256];
	int len = strlen(sz);
	if (len > (int)sizeof(buf))
		return TNT_EBIG;
	enum tnt_error e = tnt_io_recv(t, buf, len);
	if (e != TNT_EOK)
		return e;
	if (!memcmp(buf, sz, len))
		return TNT_EOK;
	return TNT_EPROTO;
}
