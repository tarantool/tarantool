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
#include "iproto.h"
#include "exception.h"

#include <stdio.h>
#include <string.h>

#include <errcode.h>
#include <palloc.h>
#include <fiber.h>
#include <tbuf.h>
#include <say.h>
#include "coio_buf.h"

const uint32_t msg_ping = 0xff00;

static void iproto_reply(iproto_callback callback, struct tbuf *request);

static void
iproto_validate_header(struct iproto_header *header);

inline static void
iproto_flush(struct coio *coio, ssize_t to_read)
{
	/*
	 * Flush output and garbage collect before reading
	 * next header.
	 */
	if (to_read > 0) {
		iov_flush(coio);
		fiber_gc();
	}
}

void
iproto_interact(va_list ap)
{
	struct coio *coio = va_arg(ap, struct coio *);
	iproto_callback callback = va_arg(ap, iproto_callback);
	struct tbuf *in = &fiber->rbuf;
	ssize_t to_read = sizeof(struct iproto_header);
	@try {
		for (;;) {
			if (to_read > 0 && coio_bread(coio, in, to_read) <= 0)
				break;

			/* validating iproto package header */
			iproto_validate_header(iproto(in));

			ssize_t request_len = sizeof(struct iproto_header)
				+ iproto(in)->len;
			to_read = request_len - in->size;

			iproto_flush(coio, to_read);

			if (to_read > 0 && coio_bread(coio, in, to_read) <= 0)
				break;

			struct tbuf *request = tbuf_split(in, request_len);
			iproto_reply(callback, request);

			to_read = sizeof(struct iproto_header) - in->size;
			iproto_flush(coio, to_read);
		}
	} @finally {
		coio_close(coio);
		free(coio);
	}
}

/** Stack a reply to a single request to the fiber's io vector. */

static void iproto_reply(iproto_callback callback, struct tbuf *request)
{
	struct iproto_header_retcode *reply;

	reply = palloc(fiber->gc_pool, sizeof(*reply));
	reply->msg_code = iproto(request)->msg_code;
	reply->sync = iproto(request)->sync;

	if (unlikely(reply->msg_code == msg_ping)) {
		reply->len = 0;
		iov_add(reply, sizeof(struct iproto_header));
		return;
	}

	reply->len = sizeof(uint32_t); /* ret_code */
	iov_add(reply, sizeof(struct iproto_header_retcode));
	size_t saved_iov_cnt = fiber->iov_cnt;
	/* make request point to iproto data */
	request->size = iproto(request)->len;
	request->data = iproto(request)->data;

	@try {
		callback(reply->msg_code, request);
		reply->ret_code = 0;
	}
	@catch (ClientError *e) {
		fiber->iov.size -= (fiber->iov_cnt - saved_iov_cnt) * sizeof(struct iovec);
		fiber->iov_cnt = saved_iov_cnt;
		reply->ret_code = tnt_errcode_val(e->errcode);
		iov_dup(e->errmsg, strlen(e->errmsg)+1);
	}
	for (; saved_iov_cnt < fiber->iov_cnt; saved_iov_cnt++)
		reply->len += iovec(&fiber->iov)[saved_iov_cnt].iov_len;
}

static void
iproto_validate_header(struct iproto_header *header)
{
	if (header->len > IPROTO_BODY_LEN_MAX) {
		/*
		 * The package is too big, just close connection for now to
		 * avoid DoS.
		 */
		say_error("received package is too big: %llu",
			  (unsigned long long)header->len);
		tnt_raise(FiberCancelException);
	}
}
