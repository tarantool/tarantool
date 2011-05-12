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

#include <stdio.h>
#include <string.h>

#include <errcode.h>
#include <palloc.h>
#include <fiber.h>
#include <iproto.h>
#include <tbuf.h>
#include <say.h>

const uint32_t msg_ping = 0xff00;

static struct tbuf *
iproto_parse(struct tbuf *in)
{
	if (in->len < sizeof(struct iproto_header))
		return NULL;
	if (in->len < sizeof(struct iproto_header) + iproto(in)->len)
		return NULL;

	return tbuf_split(in, sizeof(struct iproto_header) + iproto(in)->len);
}

void
iproto_interact(void *data)
{
	uint32_t (*callback) (uint32_t msg, struct tbuf *requst_data) = data;
	struct tbuf *request;
	struct iproto_header_retcode *reply;
	ssize_t r;

	for (;;) {
		if (fiber_bread(fiber->rbuf, sizeof(struct iproto_header)) <= 0)
			break;
		while ((request = iproto_parse(fiber->rbuf)) != NULL) {
			reply = palloc(fiber->pool, sizeof(*reply));
			reply->msg_code = iproto(request)->msg_code;
			reply->sync = iproto(request)->sync;

			if (unlikely(reply->msg_code == msg_ping)) {
				reply->len = 0;
				add_iov(reply, sizeof(struct iproto_header));
			} else {
				add_iov(reply, sizeof(struct iproto_header_retcode));
				/* j is last used iov in loop */
				int j = fiber->iov_cnt;

				/* make requst point to iproto data */
				u32 msg_code = iproto(request)->msg_code;
				request->len = iproto(request)->len;
				request->data = iproto(request)->data;
				u32 err = callback(msg_code, request);
				reply->ret_code = tnt_errcode_val(err);

				/*
				 * retcode is uint32_t and included int struct iproto_header_retcode
				 * but we has to count it anyway
				 */
				reply->len = sizeof(uint32_t);

				for (; j < fiber->iov_cnt; j++)
					reply->len += iovec(fiber->iov)[j].iov_len;
			}
		}
		r = fiber_flush_output();
		fiber_gc();

		if (r < 0) {
			say_warn("io_error: %s", strerror(errno));
			break;
		}
	}
}
