#ifndef TARANTOOL_COIO_BUF_H_INCLUDED
#define TARANTOOL_COIO_BUF_H_INCLUDED
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
#include "coio.h"
#include "iobuf.h"
/** Buffered cooperative IO */

/** Read at least sz bytes, buffered. Return 0 in case of EOF. */
static inline ssize_t
coio_bread(struct ev_io *coio, struct ibuf *buf, size_t sz)
{
	ibuf_reserve(buf, sz);
	ssize_t n = coio_read_ahead(coio, buf->end, sz, ibuf_unused(buf));
	buf->end += n;
	return n;
}

/** Read at least sz bytes, buffered. Throw an exception in case of EOF. */
static inline ssize_t
coio_breadn(struct ev_io *coio, struct ibuf *buf, size_t sz)
{
	ibuf_reserve(buf, sz);
	ssize_t n = coio_readn_ahead(coio, buf->end, sz, ibuf_unused(buf));
	buf->end += n;
	return n;
}

#endif /* TARANTOOL_COIO_BUF_H_INCLUDED */
