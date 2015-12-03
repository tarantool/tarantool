/*
 * Copyright 2010-2015, Tarantool AUTHORS, please see AUTHORS file.
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
#include "iobuf.h"
#include "fiber.h"

__thread struct mempool iobuf_pool;

/**
 * Network readahead. A signed integer to avoid
 * automatic type coercion to an unsigned type.
 * We assign it without locks in txn thread and
 * use in iproto thread -- it's OK that
 * readahead has a stale value while until the thread
 * caches have synchronized, after all, it's used
 * in new connections only.
 *
 * Notice that the default is not a strict power of two.
 * slab metadata takes some space, and we want
 * allocation steps to be correlated to slab buddy
 * sizes, so when we ask slab cache for 16320 bytes,
 * we get a slab of size 16384, not 32768.
 */
static int iobuf_readahead = 16320;

/**
 * How big is a buffer which needs to be shrunk before it is put
 * back into buffer cache.
 */
static int iobuf_max_size()
{
	return 18 * iobuf_readahead;
}

/** Create an instance of input/output buffer or take one from cache. */
struct iobuf *
iobuf_new()
{
	return iobuf_new_mt(&cord()->slabc);
}

struct iobuf *
iobuf_new_mt(struct slab_cache *slabc_out)
{
	struct iobuf *iobuf;
	iobuf = (struct iobuf *) mempool_alloc_xc(&iobuf_pool);
	/* Note: do not allocate memory upfront. */
	ibuf_create(&iobuf->in, &cord()->slabc, iobuf_readahead);
	obuf_create(&iobuf->out, slabc_out, iobuf_readahead);
	return iobuf;
}

/** Destroy an instance and delete it. */
void
iobuf_delete(struct iobuf *iobuf)
{
	ibuf_destroy(&iobuf->in);
	obuf_destroy(&iobuf->out);
	mempool_free(&iobuf_pool, iobuf);
}

/** Second part of multi-threaded destroy. */
void
iobuf_delete_mt(struct iobuf *iobuf)
{
	ibuf_destroy(&iobuf->in);
	/* Destroyed by the caller. */
	assert(iobuf->out.pos == 0 && iobuf->out.iov[0].iov_base == NULL);
	mempool_free(&iobuf_pool, iobuf);
}

void
iobuf_reset(struct iobuf *iobuf)
{
	/*
	 * If we happen to have fully processed the input,
	 * move the pos to the start of the input buffer.
	 */
	if (ibuf_used(&iobuf->in) == 0) {
		if (ibuf_capacity(&iobuf->in) < iobuf_max_size()) {
			ibuf_reset(&iobuf->in);
		} else {
			ibuf_reinit(&iobuf->in);
		}
	}
	if (obuf_capacity(&iobuf->out) < iobuf_max_size()) {
		/* Cheap to do even if already done. */
		obuf_reset(&iobuf->out);
	} else {
		struct slab_cache *slabc = iobuf->out.slabc;
		obuf_destroy(&iobuf->out);
		obuf_create(&iobuf->out, slabc, iobuf_readahead);
	}
}

void
iobuf_init()
{
	mempool_create(&iobuf_pool, &cord()->slabc, sizeof(struct iobuf));
}

void
iobuf_set_readahead(int readahead)
{
	iobuf_readahead =  readahead;
}
