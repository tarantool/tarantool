
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
#include <stdint.h>
#include <string.h>

#include <connector/c/include/tarantool/tnt_mem.h>
#include <connector/c/include/tarantool/tnt_tuple.h>
#include <connector/c/include/tarantool/tnt_reply.h>
#include <connector/c/include/tarantool/tnt_stream.h>
#include <connector/c/include/tarantool/tnt_buf.h>

static struct tnt_stream *tnt_buf_tryalloc(struct tnt_stream *s) {
	if (s) {
		memset(s, 0, sizeof(struct tnt_stream));
		return s;
	}
	s = tnt_mem_alloc(sizeof(struct tnt_stream));
	if (s == NULL)
		return NULL;
	memset(s, 0, sizeof(struct tnt_stream));
	s->alloc = 1;
	return s;
}

static void tnt_buf_free(struct tnt_stream *s) {
	struct tnt_stream_buf *sb = TNT_SBUF_CAST(s);
	if (sb->data)
		tnt_mem_free(sb->data);
	tnt_mem_free(s->data);
}

static ssize_t
tnt_buf_read(struct tnt_stream *s, char *buf, size_t size) {
	struct tnt_stream_buf *sb = TNT_SBUF_CAST(s);
	if (sb->data == NULL)
		return 0;
	if (sb->size == sb->rdoff)
		return 0;
	size_t avail = sb->size - sb->rdoff;
	if (size > avail)
		size = avail;
	memcpy(sb->data + sb->rdoff, buf, size);
	sb->rdoff += size;
	return size;
}

static char* tnt_buf_resize(struct tnt_stream *s, size_t size) {
	struct tnt_stream_buf *sb = TNT_SBUF_CAST(s);
	size_t off = sb->size;
	size_t nsize = off + size;
	char *nd = realloc(sb->data, nsize);
	if (nd == NULL)
		return NULL;
	sb->data = nd;
	sb->size = nsize;
	return sb->data + off;
}

static ssize_t
tnt_buf_write(struct tnt_stream *s, char *buf, size_t size) {
	char *p = tnt_buf_resize(s, size);
	if (p == NULL)
		return -1;
	memcpy(p, buf, size);
	s->wrcnt++;
	return size;
}

static ssize_t
tnt_buf_writev(struct tnt_stream *s, struct iovec *iov, int count) {
	size_t size = 0;
	int i;
	for (i = 0 ; i < count ; i++)
		size += iov[i].iov_len;
	char *p = tnt_buf_resize(s, size);
	if (p == NULL)
		return -1;
	for (i = 0 ; i < count ; i++) {
		memcpy(p, iov[i].iov_base, iov[i].iov_len);
		p += iov[i].iov_len;
	}
	s->wrcnt++;
	return size;
}

static int
tnt_buf_reply(struct tnt_stream *s, struct tnt_reply *r) {
	struct tnt_stream_buf *sb = TNT_SBUF_CAST(s);
	if (sb->data == NULL)
		return -1;
	if (sb->size == sb->rdoff)
		return 1;
	size_t off = 0;
	int rc = tnt_reply(r, sb->data + sb->rdoff, sb->size - sb->rdoff, &off);
	if (rc == 0)
		sb->rdoff += off;
	return rc;
}

/*
 * tnt_buf()
 *
 * create and initialize buffer stream;
 *
 * s - stream pointer, maybe NULL
 * 
 * if stream pointer is NULL, then new stream will be created. 
 *
 * returns stream pointer, or NULL on error.
*/
struct tnt_stream *tnt_buf(struct tnt_stream *s) {
	int allocated = s == NULL;
	s = tnt_buf_tryalloc(s);
	if (s == NULL)
		return NULL;
	/* allocating stream data */
	s->data = tnt_mem_alloc(sizeof(struct tnt_stream_buf));
	if (s->data == NULL) {
		if (allocated)
			tnt_stream_free(s);
		return NULL;
	}
	/* initializing interfaces */
	s->read = tnt_buf_read;
	s->reply = tnt_buf_reply;
	s->write = tnt_buf_write;
	s->writev = tnt_buf_writev;
	s->free = tnt_buf_free;
	/* initializing internal data */
	struct tnt_stream_buf *sb = TNT_SBUF_CAST(s);
	sb->rdoff = 0;
	sb->size = 0;
	sb->data = NULL;
	return s;
}
