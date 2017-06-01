#ifndef TARANTOOL_IPROTO_PORT_H_INCLUDED
#define TARANTOOL_IPROTO_PORT_H_INCLUDED
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
#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct obuf;
struct obuf_svp;

int
iproto_prepare_select(struct obuf *buf, struct obuf_svp *svp);

/**
 * Write select header to a preallocated buffer.
 * This function doesn't throw (and we rely on this in iproto.cc).
 */
void
iproto_reply_select(struct obuf *buf, struct obuf_svp *svp, uint64_t sync,
		    uint32_t count);
#if defined(__cplusplus)
} /*  extern "C" */

/** Stack a reply to 'ping' packet. */
void
iproto_reply_ok(struct obuf *out, uint64_t sync);

/**
 * Write an error packet int output buffer. Doesn't throw if out
 * of memory
 */
int
iproto_reply_error(struct obuf *out, const struct error *e, uint64_t sync);

/** Write error directly to a socket. */
void
iproto_write_error(int fd, const struct error *e);

#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_IPROTO_PORT_H_INCLUDED */
