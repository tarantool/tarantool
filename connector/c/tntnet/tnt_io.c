
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
#include <stdarg.h>
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

#include <connector/c/include/tarantool/tnt.h>
#include <connector/c/include/tarantool/tnt_net.h>
#include <connector/c/include/tarantool/tnt_io.h>

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

static enum tnt_error tnt_io_nonblock(struct tnt_stream_net *s, int set) {
	int flags = fcntl(s->fd, F_GETFL);
	if (flags == -1) {
		s->errno_ = errno;
		return TNT_ESYSTEM;
	}
	if (set)
		flags |= O_NONBLOCK;
	else
		flags &= ~O_NONBLOCK;
	if (fcntl(s->fd, F_SETFL, flags) == -1) {
		s->errno_ = errno;
		return TNT_ESYSTEM;
	}
	return TNT_EOK;
}

static enum tnt_error
tnt_io_connect_do(struct tnt_stream_net *s, char *host, int port)
{
	/* resolving address */
	struct sockaddr_in addr;
	enum tnt_error result = tnt_io_resolve(&addr, host, port);
	if (result != TNT_EOK)
		return result;

	/* setting nonblock */
	result = tnt_io_nonblock(s, 1);
	if (result != TNT_EOK)
		return result;

	if (connect(s->fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
		if (errno == EINPROGRESS) {
			/** waiting for connection while handling signal events */
			const int64_t micro = 1000000;
			int64_t tmout_usec = s->opt.tmout_connect.tv_sec * micro;
			/* get start connect time */
			struct timeval start_connect;
			if (gettimeofday(&start_connect, NULL) == -1) {
				s->errno_ = errno;
				return TNT_ESYSTEM;
			}
			/* set initial timer */
			struct timeval tmout;
			memcpy(&tmout, &s->opt.tmout_connect, sizeof(tmout));
			while (1) {
				fd_set fds;
				FD_ZERO(&fds);
				FD_SET(s->fd, &fds);
				int ret = select(s->fd + 1, NULL, &fds, NULL, &tmout);
				if (ret == -1) {
					if (errno == EINTR || errno == EAGAIN) {
						/* get current time */
						struct timeval curr;
						if (gettimeofday(&curr, NULL) == -1) {
							s->errno_ = errno;
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
						s->errno_ = errno;
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
			/* checking error status */
			int opt = 0;
			socklen_t len = sizeof(opt);
			if ((getsockopt(s->fd, SOL_SOCKET, SO_ERROR, &opt, &len) == -1) || opt) {
				s->errno_ = (opt) ? opt: errno;
				return TNT_ESYSTEM;
			}
		} else {
			s->errno_ = errno;
			return TNT_ESYSTEM;
		}
	}

	/* setting block */
	result = tnt_io_nonblock(s, 0);
	if (result != TNT_EOK)
		return result;
	return TNT_EOK;
}

static enum tnt_error tnt_io_xbufmax(struct tnt_stream_net *s, int opt, int min) {
	int max = 128 * 1024 * 1024;
	if (min == 0)
		min = 16384;
	unsigned int avg = 0;
	while (min <= max) {
		avg = ((unsigned int)(min + max)) / 2;
		if (setsockopt(s->fd, SOL_SOCKET, opt, &avg, sizeof(avg)) == 0)
			min = avg + 1;
		else
			max = avg - 1;
	}
	return TNT_EOK;
}

static enum tnt_error tnt_io_setopts(struct tnt_stream_net *s) {
	int opt = 1;
	if (setsockopt(s->fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt)) == -1)
		goto error;

	tnt_io_xbufmax(s, SO_SNDBUF, s->opt.send_buf);
	tnt_io_xbufmax(s, SO_RCVBUF, s->opt.recv_buf);

	if (setsockopt(s->fd, SOL_SOCKET, SO_SNDTIMEO,
		       &s->opt.tmout_send, sizeof(s->opt.tmout_send)) == -1)
		goto error;
	if (setsockopt(s->fd, SOL_SOCKET, SO_RCVTIMEO,
		       &s->opt.tmout_recv, sizeof(s->opt.tmout_recv)) == -1)
		goto error;
	return TNT_EOK;
error:
	s->errno_ = errno;
	return TNT_ESYSTEM;
}

enum tnt_error
tnt_io_connect(struct tnt_stream_net *s, char *host, int port)
{
	s->fd = socket(AF_INET, SOCK_STREAM, 0);
	if (s->fd < 0) {
		s->errno_ = errno;
		return TNT_ESYSTEM;
	}
	enum tnt_error result = tnt_io_setopts(s);
	if (result != TNT_EOK)
		goto out;
	result = tnt_io_connect_do(s, host, port);
	if (result != TNT_EOK)
		goto out;
	s->connected = 1;
	return TNT_EOK;
out:
	tnt_io_close(s);
	return result;
}

void tnt_io_close(struct tnt_stream_net *s)
{
	if (s->fd > 0) {
		close(s->fd);
		s->fd = -1;
	}
	s->connected = 0;
}

ssize_t tnt_io_flush(struct tnt_stream_net *s) {
	if (s->sbuf.off == 0)
		return 0;
	ssize_t rc = tnt_io_send_raw(s, s->sbuf.buf, s->sbuf.off, 1);
	if (rc == -1)
		return -1;
	s->sbuf.off = 0;
	return rc;
}

ssize_t
tnt_io_send_raw(struct tnt_stream_net *s, char *buf, size_t size, int all)
{
	size_t off = 0;
	do {
		ssize_t r;
		if (s->sbuf.tx) {
			r = s->sbuf.tx(s->sbuf.buf, buf + off, size - off);
		} else {
			do {
				r = send(s->fd, buf + off, size - off, 0);
			} while (r == -1 && (errno == EINTR));
		}
		if (r <= 0) {
			s->error = TNT_ESYSTEM;
			s->errno_ = errno;
			return -1;
		}
		off += r;
	} while (off != size && all);
	return off;
}

ssize_t
tnt_io_sendv_raw(struct tnt_stream_net *s, struct iovec *iov, int count, int all)
{
       	size_t total = 0;
	while (count > 0) {
		ssize_t r;
		if (s->sbuf.txv) {
			r = s->sbuf.txv(s->sbuf.buf, iov, MIN(count, IOV_MAX));
		} else {
			do {
				r = writev(s->fd, iov, count);
			} while (r == -1 && (errno == EINTR));
		}
		if (r <= 0) {
			s->error = TNT_ESYSTEM;
			s->errno_ = errno;
			return -1;
		}
		total += r;
		if (!all)
			break;
		while (count > 0) {
			if (iov->iov_len > (size_t)r) {
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
	return total;
}

ssize_t
tnt_io_send(struct tnt_stream_net *s, char *buf, size_t size)
{
	if (s->sbuf.buf == NULL)
		return tnt_io_send_raw(s, buf, size, 1);
	if (size > s->sbuf.size) {
		s->error = TNT_EBIG;
		return -1;
	}
	if ((s->sbuf.off + size) <= s->sbuf.size) {
		memcpy(s->sbuf.buf + s->sbuf.off, buf, size);
		s->sbuf.off += size;
		return size;
	}
	ssize_t r = tnt_io_send_raw(s, s->sbuf.buf, s->sbuf.off, 1);
	if (r == -1)
		return -1;
	s->sbuf.off = size;
	memcpy(s->sbuf.buf, buf, size);
	return size;
}

inline static void
tnt_io_sendv_put(struct tnt_stream_net *s, struct iovec *iov, int count) {
	int i;
	for (i = 0 ; i < count ; i++) {
		memcpy(s->sbuf.buf + s->sbuf.off,
		       iov[i].iov_base,
		       iov[i].iov_len);
		s->sbuf.off += iov[i].iov_len;
	}
}

ssize_t
tnt_io_sendv(struct tnt_stream_net *s, struct iovec *iov, int count)
{
	if (s->sbuf.buf == NULL)
		return tnt_io_sendv_raw(s, iov, count, 1);
	size_t size = 0;
	int i;
	for (i = 0 ; i < count ; i++)
		size += iov[i].iov_len;
	if (size > s->sbuf.size) {
		s->error = TNT_EBIG;
		return -1;
	}
	if ((s->sbuf.off + size) <= s->sbuf.size) {
		tnt_io_sendv_put(s, iov, count);
		return size;
	}
	ssize_t r = tnt_io_send_raw(s, s->sbuf.buf, s->sbuf.off, 1);
	if (r == -1)
		return -1;
	s->sbuf.off = 0;
	tnt_io_sendv_put(s, iov, count);
	return size;
}

ssize_t
tnt_io_recv_raw(struct tnt_stream_net *s, char *buf, size_t size, int all)
{
	size_t off = 0;
	do {
		ssize_t r;
		if (s->rbuf.tx) {
			r = s->rbuf.tx(s->rbuf.buf, buf + off, size - off);
		} else {
			do {
				r = recv(s->fd, buf + off, size - off, 0);
			} while (r == -1 && (errno == EINTR));
		}
		if (r <= 0) {
			s->error = TNT_ESYSTEM;
			s->errno_ = errno;
			return -1;
		}
		off += r;
	} while (off != size && all);
	return off;
}

ssize_t
tnt_io_recv(struct tnt_stream_net *s, char *buf, size_t size)
{
	if (s->rbuf.buf == NULL)
		return tnt_io_recv_raw(s, buf, size, 1);
	size_t lv, rv, off = 0, left = size;
	while (1) {
		if ((s->rbuf.off + left) <= s->rbuf.top) {
			memcpy(buf + off, s->rbuf.buf + s->rbuf.off, left);
			s->rbuf.off += left;
			return size;
		}

		lv = s->rbuf.top - s->rbuf.off;
		rv = left - lv;
		if (lv) {
			memcpy(buf + off, s->rbuf.buf + s->rbuf.off, lv);
			off += lv;
		}

		s->rbuf.off = 0;
		ssize_t top = tnt_io_recv_raw(s, s->rbuf.buf, s->rbuf.size, 0);
		if (top <= 0) {
			s->errno_ = errno;
			s->error = TNT_ESYSTEM;
			return -1;
		}

		s->rbuf.top = top;
		if (rv <= s->rbuf.top) {
			memcpy(buf + off, s->rbuf.buf, rv);
			s->rbuf.off = rv;
			return size;
		}
		left -= lv;
	}
	return -1;
}
