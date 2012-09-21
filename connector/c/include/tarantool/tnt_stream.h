#ifndef TNT_STREAM_H_INCLUDED
#define TNT_STREAM_H_INCLUDED

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

#include <sys/types.h>
#include <sys/uio.h>

/* stream interface */

struct tnt_stream {
	int alloc;
	ssize_t (*write)(struct tnt_stream *s, const char *buf, size_t size);
	ssize_t (*writev)(struct tnt_stream *s, struct iovec *iov, int count);
	ssize_t (*write_request)(struct tnt_stream *s, struct tnt_request *r);
	ssize_t (*read)(struct tnt_stream *s, char *buf, size_t size);
	int (*read_reply)(struct tnt_stream *s, struct tnt_reply *r);
	int (*read_request)(struct tnt_stream *s, struct tnt_request *r);
	int (*read_tuple)(struct tnt_stream *s, struct tnt_tuple *t);
	void (*free)(struct tnt_stream *s);
	void *data;
	uint32_t wrcnt; /* count of write operations */
	uint32_t reqid;
};

struct tnt_stream *tnt_stream_init(struct tnt_stream *s);
void tnt_stream_free(struct tnt_stream *s);

uint32_t tnt_stream_reqid(struct tnt_stream *s, uint32_t reqid);

#endif /* TNT_STREAM_H_INCLUDED */
