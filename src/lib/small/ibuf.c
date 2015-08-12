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
#include "ibuf.h"
#include <string.h>
#include "slab_cache.h"

/** Initialize an input buffer. */
void
ibuf_create(struct ibuf *ibuf, struct slab_cache *slabc, size_t start_capacity)
{
	ibuf->slabc = slabc;
	ibuf->buf = ibuf->rpos = ibuf->wpos = ibuf->end = NULL;
	ibuf->start_capacity = start_capacity;
	/* Don't allocate the buffer yet. */
}

void
ibuf_destroy(struct ibuf *ibuf)
{
	if (ibuf->buf) {
		struct slab *slab = slab_from_data(ibuf->buf);
		slab_put(ibuf->slabc, slab);
	 }
}

/** Free memory allocated by this buffer */
void
ibuf_reinit(struct ibuf *ibuf)
{
	struct slab_cache *slabc = ibuf->slabc;
	size_t start_capacity = ibuf->start_capacity;
	ibuf_destroy(ibuf);
	ibuf_create(ibuf, slabc, start_capacity);
}

/**
 * Ensure the buffer has sufficient capacity
 * to store size bytes, and return pointer to
 * the beginning.
 */
void *
ibuf_reserve_nothrow_slow(struct ibuf *ibuf, size_t size)
{
	assert(ibuf->wpos + size > ibuf->end);
	size_t used = ibuf_used(ibuf);
	size_t capacity = ibuf_capacity(ibuf);
	/*
	 * Check if we have enough space in the
	 * current buffer. In this case de-fragment it
	 * by moving existing data to the beginning.
	 * Otherwise, get a bigger buffer.
	 */
	if (size + used <= capacity) {
		memmove(ibuf->buf, ibuf->rpos, used);
	} else {
		/* Use iobuf_readahead as allocation factor. */
		size_t new_capacity = capacity * 2;
		if (new_capacity < ibuf->start_capacity)
			new_capacity = ibuf->start_capacity;

		while (new_capacity < used + size)
			new_capacity *= 2;

		struct slab *slab = slab_get(ibuf->slabc, new_capacity);
		if (slab == NULL)
			return NULL;
		char *ptr = (char *) slab_data(slab);
		memcpy(ptr, ibuf->rpos, used);
		if (ibuf->buf)
			slab_put(ibuf->slabc, slab_from_data(ibuf->buf));
		ibuf->buf = ptr;
		ibuf->end = ibuf->buf + slab_capacity(slab);
	}
	ibuf->rpos = ibuf->buf;
	ibuf->wpos = ibuf->rpos + used;
	return ibuf->wpos;
}

