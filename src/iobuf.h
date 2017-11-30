#ifndef TARANTOOL_IOBUF_H_INCLUDED
#define TARANTOOL_IOBUF_H_INCLUDED
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
#include <stdbool.h>
#include "small/ibuf.h"
#include "small/obuf.h"

extern unsigned iobuf_readahead;

struct iobuf
{
	/** Input buffer. */
	struct ibuf in;
	/** Output buffer. */
	struct obuf out;
};

/**
 * How big is a buffer which needs to be shrunk before it is put
 * back into buffer cache.
 */
unsigned
iobuf_max_size();

/**
 * Create an instance of input/output buffer.
 * @warning not safe to use in case of multi-threaded
 * access to in and out.
 */
struct iobuf *
iobuf_new();

/**
 * Destroy an input/output buffer.
 * @warning a counterpart of iobuf_new(), only for single threaded
 * access.
 */
void
iobuf_delete(struct iobuf *iobuf);

/**
 * Multi-threaded constructor of iobuf - 'out'
 * may use slab caches of another (consumer) cord.
 */
struct iobuf *
iobuf_new_mt(struct slab_cache *slabc_out);

/**
 * Must be called when we are done sending all output,
 * and there is likely no cached input.
 * Is automatically called by iobuf_flush().
 */
void
iobuf_reset(struct iobuf *iobuf);

/**
 * Got to be called in each thread iobuf subsystem is
 * used in.
 */
void
iobuf_init();

#endif /* TARANTOOL_IOBUF_H_INCLUDED */
