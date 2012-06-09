
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

#include <connector/c/include/tarantool/tnt.h>
#include <connector/c/include/tarantool/tnt_net.h>
#include <connector/c/include/tarantool/tnt_io.h>

static void tnt_net_free(struct tnt_stream *s) {
	struct tnt_stream_net *sn = TNT_SNET_CAST(s);
	tnt_io_close(sn);
	tnt_iob_free(&sn->sbuf);
	tnt_iob_free(&sn->rbuf);
	tnt_opt_free(&sn->opt);
	tnt_mem_free(s->data);
}

static ssize_t
tnt_net_read(struct tnt_stream *s, char *buf, size_t size) {
	struct tnt_stream_net *sn = TNT_SNET_CAST(s);
	/* read doesn't touches wrcnt */
	return tnt_io_recv(sn, buf, size);
}

static ssize_t
tnt_net_write(struct tnt_stream *s, char *buf, size_t size) {
	struct tnt_stream_net *sn = TNT_SNET_CAST(s);
	ssize_t rc = tnt_io_send(sn, buf, size);
	if (rc != -1)
		s->wrcnt++;
	return rc;
}

static ssize_t
tnt_net_writev(struct tnt_stream *s, struct iovec *iov, int count) {
	struct tnt_stream_net *sn = TNT_SNET_CAST(s);
	ssize_t rc = tnt_io_sendv(sn, iov, count);
	if (rc != -1)
		s->wrcnt++;
	return rc;
}

static ssize_t
tnt_net_write_request(struct tnt_stream *s, struct tnt_request *r) {
	return tnt_net_writev(s, r->v, r->vc);
}

static ssize_t
tnt_net_recv_cb(struct tnt_stream *s, char *buf, ssize_t size) {
	struct tnt_stream_net *sn = TNT_SNET_CAST(s);
	return tnt_io_recv(sn, buf, size);
}

static int
tnt_net_reply(struct tnt_stream *s, struct tnt_reply *r) {
	if (s->wrcnt == 0)
		return 1;
	s->wrcnt--;
	return tnt_reply_from(r, (tnt_reply_t)tnt_net_recv_cb, s);
}

static int
tnt_net_request(struct tnt_stream *s, struct tnt_request *r) {
	/* read doesn't touches wrcnt */
	return tnt_request_from(r, (tnt_request_t)tnt_net_recv_cb, s, NULL);
}

/*
 * tnt_net()
 *
 * create and initialize network stream;
 *
 * s - stream pointer, maybe NULL
 * 
 * if stream pointer is NULL, then new stream will be created. 
 *
 * returns stream pointer, or NULL on error.
*/
struct tnt_stream *tnt_net(struct tnt_stream *s) {
	int allocated = s == NULL;
	s = tnt_stream_init(s);
	if (s == NULL)
		return NULL;
	/* allocating stream data */
	s->data = tnt_mem_alloc(sizeof(struct tnt_stream_net));
	if (s->data == NULL) {
		if (allocated)
			tnt_stream_free(s);
		return NULL;
	}
	memset(s->data, 0, sizeof(struct tnt_stream_net));
	/* initializing interfaces */
	s->read = tnt_net_read;
	s->read_reply = tnt_net_reply;
	s->read_request = tnt_net_request;
	s->write = tnt_net_write;
	s->writev = tnt_net_writev;
	s->write_request = tnt_net_write_request;
	s->free = tnt_net_free;
	/* initializing internal data */
	struct tnt_stream_net *sn = TNT_SNET_CAST(s);
	sn->fd = -1;
	tnt_opt_init(&sn->opt);
	return s;
}

/*
 * tnt_set()
 *
 * set options to network stream;
 *
 * s   - network stream pointer
 * opt - option id
 * ... - option value
 * 
 * returns 0 on success, or -1 on error.
*/
int tnt_set(struct tnt_stream *s, int opt, ...) {
	struct tnt_stream_net *sn = TNT_SNET_CAST(s);
	va_list args;
	va_start(args, opt);
	sn->error = tnt_opt_set(&sn->opt, opt, args);
	va_end(args);
	return (sn->error == TNT_EOK) ? 0 : -1;
}

/*
 * tnt_init()
 *
 * initialize prepared network stream;
 *
 * s - network stream pointer
 * 
 * returns 0 on success, or -1 on error.
*/
int tnt_init(struct tnt_stream *s) {
	struct tnt_stream_net *sn = TNT_SNET_CAST(s);
	if (tnt_iob_init(&sn->sbuf, sn->opt.send_buf, sn->opt.send_cb,
		sn->opt.send_cbv, sn->opt.send_cb_arg) == -1) {
		sn->error = TNT_EMEMORY;
		return -1;
	}
	if (tnt_iob_init(&sn->rbuf, sn->opt.recv_buf, sn->opt.recv_cb, NULL, 
		sn->opt.recv_cb_arg) == -1) {
		sn->error = TNT_EMEMORY;
		return -1;
	}
	if (sn->opt.hostname == NULL) {
		sn->error = TNT_EBADVAL;
		return -1;
	}
	if (sn->opt.port == 0) {
		sn->error = TNT_EBADVAL;
		return -1;
	}
	return 0;
}

/*
 * tnt_connect()
 *
 * connect to server;
 * reconnect to server;
 *
 * s - network stream pointer
 * 
 * returns 0 on success, or -1 on error.
*/
int tnt_connect(struct tnt_stream *s) {
	struct tnt_stream_net *sn = TNT_SNET_CAST(s);
	if (sn->connected)
		tnt_close(s);
	sn->error = tnt_io_connect(sn, sn->opt.hostname, sn->opt.port);
	if (sn->error != TNT_EOK)
		return -1;
	return 0;
}

/*
 * tnt_close()
 *
 * close connection to server;
 *
 * s - network stream pointer
 * 
 * returns 0 on success, or -1 on error.
*/
void tnt_close(struct tnt_stream *s) {
	struct tnt_stream_net *sn = TNT_SNET_CAST(s);
	tnt_io_close(sn);
}

/*
 * tnt_flush()
 *
 * send bufferized data to server;
 *
 * s - network stream pointer
 * 
 * returns size of data been sended on success, or -1 on error.
*/
ssize_t tnt_flush(struct tnt_stream *s) {
	struct tnt_stream_net *sn = TNT_SNET_CAST(s);
	return tnt_io_flush(sn);
}

/*
 * tnt_fd()
 *
 * get connection socket description;
 *
 * s - network stream pointer
*/
int tnt_fd(struct tnt_stream *s) {
	struct tnt_stream_net *sn = TNT_SNET_CAST(s);
	return sn->fd;
}

/*
 * tnt_error()
 *
 * get library error status;
 *
 * s - network stream pointer
*/
enum tnt_error tnt_error(struct tnt_stream *s) {
	struct tnt_stream_net *sn = TNT_SNET_CAST(s);
	return sn->error;
}

/* must be in sync with enum tnt_error */

struct tnt_error_desc {
	enum tnt_error type;
	char *desc;
};

static struct tnt_error_desc tnt_error_list[] = 
{
	{ TNT_EOK,      "ok"                       },
	{ TNT_EFAIL,    "fail"                     },
	{ TNT_EMEMORY,  "memory allocation failed" },
	{ TNT_ESYSTEM,  "system error"             },
	{ TNT_EBIG,     "buffer is too big"        },
	{ TNT_ESIZE,    "bad buffer size"          },
	{ TNT_ERESOLVE, "gethostbyname(2) failed"  },
	{ TNT_ETMOUT,   "operation timeout"        },
	{ TNT_EBADVAL,  "bad argument"             },
	{ TNT_LAST,      NULL                      }
};

/*
 * tnt_strerror()
 *
 * get library error status description string;
 *
 * s - network stream pointer
*/
char *tnt_strerror(struct tnt_stream *s) {
	struct tnt_stream_net *sn = TNT_SNET_CAST(s);
	if (sn->error == TNT_ESYSTEM) {
		static char msg[256];
		snprintf(msg, sizeof(msg), "%s (errno: %d)",
			 strerror(sn->errno_), sn->errno_);
		return msg;
	}
	return tnt_error_list[(int)sn->error].desc;
}

/*
 * tnt_errno()
 *
 * get saved errno;
 *
 * s - network stream pointer
*/
int tnt_errno(struct tnt_stream *s) {
	struct tnt_stream_net *sn = TNT_SNET_CAST(s);
	return sn->errno_;
}
