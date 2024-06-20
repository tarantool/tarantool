/*
 * Copyright 2010-2016, Tarantool AUTHORS, please see AUTHORS file.
 *
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
#include <sys/uio.h>
#include "xrow_io.h"
#include "xrow.h"
#include "coio.h"
#include "coio_buf.h"
#include "errinj.h"
#include "error.h"
#include "msgpuck/msgpuck.h"
#include "tweaks.h"

void
coio_read_xrow(struct iostream *io, struct ibuf *in, struct xrow_header *row)
{
	/* Read fixed header */
	if (ibuf_used(in) < 1)
		coio_breadn(io, in, 1);

	/* Read length */
	if (mp_typeof(*in->rpos) != MP_UINT) {
		tnt_raise(ClientError, ER_INVALID_MSGPACK,
			  "packet length");
	}
	ssize_t to_read = mp_check_uint(in->rpos, in->wpos);
	if (to_read > 0)
		coio_breadn(io, in, to_read);

	uint64_t len = mp_decode_uint((const char **)&in->rpos);

	/* Read header and body */
	if (len > ibuf_used(in))
		coio_breadn(io, in, len - ibuf_used(in));

	xrow_decode_xc(row, (const char **)&in->rpos, in->rpos + len, true);
}

void
coio_read_xrow_timeout_xc(struct iostream *io, struct ibuf *in,
			  struct xrow_header *row, ev_tstamp timeout)
{
	ev_tstamp start, delay;
	coio_timeout_init(&start, &delay, timeout);
	/* Read fixed header */
	if (ibuf_used(in) < 1)
		coio_breadn_timeout(io, in, 1, delay);
	coio_timeout_update(&start, &delay);

	/* Read length */
	if (mp_typeof(*in->rpos) != MP_UINT) {
		tnt_raise(ClientError, ER_INVALID_MSGPACK,
			  "packet length");
	}
	ssize_t to_read = mp_check_uint(in->rpos, in->wpos);
	if (to_read > 0)
		coio_breadn_timeout(io, in, to_read, delay);
	coio_timeout_update(&start, &delay);

	uint64_t len = mp_decode_uint((const char **)&in->rpos);

	/* Read header and body */
	if (len > ibuf_used(in))
		coio_breadn_timeout(io, in, len - ibuf_used(in), delay);

	xrow_decode_xc(row, (const char **)&in->rpos, in->rpos + len, true);
}


void
coio_write_xrow(struct iostream *io, const struct xrow_header *row)
{
	RegionGuard region_guard(&fiber()->gc);
	int iovcnt;
	struct iovec iov[XROW_IOVMAX];
	xrow_to_iovec(row, iov, &iovcnt);
	if (coio_writev(io, iov, iovcnt, 0) < 0)
		diag_raise();
}

uint64_t xrow_stream_flush_size = 16384;
TWEAK_UINT(xrow_stream_flush_size);

void
xrow_stream_write(struct xrow_stream *stream, const struct xrow_header *row)
{
	/* Fixheader - length of packet. */
	size_t fixheader_len = mp_sizeof_uint(UINT32_MAX);
	size_t len = 0;
	len += xrow_header_sizeof(row, row->sync);
	for (int i = 0; i < row->bodycnt; i++) {
		len += row->body[i].iov_len;
	}
	char *data = (char *)xlsregion_alloc(&stream->lsregion,
					     len + fixheader_len,
					     ++stream->lsr_id);
	*(data) = 0xce; /* MP_UINT32 */
	store_u32(data + 1, mp_bswap_u32(len));
	data = data + fixheader_len;
	data += xrow_header_encode(row, row->sync, data);
	for (int i = 0; i < row->bodycnt; i++) {
		size_t l = row->body[i].iov_len;
		memcpy(data, row->body[i].iov_base, l);
		data += l;
	}
}

int
xrow_stream_flush(struct xrow_stream *stream, struct iostream *io)
{
	ssize_t to_flush = lsregion_used(&stream->lsregion);
	/*
	 * Might flush more than requested if data is added to the buffer
	 * during the coio_writev yield.
	 */
	while (to_flush > 0) {
		struct iovec iov[IOV_MAX];
		int iovcnt = lengthof(iov);
		int64_t gc_id = lsregion_to_iovec(&stream->lsregion, iov,
						  &iovcnt, &stream->flush_pos);
		ssize_t written = coio_writev(io, iov, iovcnt, 0);
		if (written < 0)
			return -1;
		to_flush -= written;
		lsregion_gc(&stream->lsregion, gc_id);
	}
	struct errinj *inj = errinj(ERRINJ_RELAY_TIMEOUT, ERRINJ_DOUBLE);
	if (inj != NULL && inj->dparam > 0)
		fiber_sleep(inj->dparam);
	return 0;
}
