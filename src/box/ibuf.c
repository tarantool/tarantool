/*
 * Copyright 2020, Tarantool AUTHORS, please see AUTHORS file.
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

#include <stdlib.h>

#include "ibuf.h"
#include "error.h"
#include "diag.h"
#include "small/ibuf.h"

void *
box_ibuf_reserve(box_ibuf_t *ibuf, size_t size)
{
	void * p = ibuf_reserve(ibuf, size);
	if (p == NULL)
		diag_set(OutOfMemory, size, "ibuf_reserve", "box_ibuf_reserve");
	return p;
}

void
box_ibuf_read_range(box_ibuf_t *ibuf, char ***rpos, char ***wpos)
{
	assert(ibuf != NULL);
	*rpos = &ibuf->rpos;
	if (wpos != NULL)
		*wpos = &ibuf->wpos;
}

void
box_ibuf_write_range(box_ibuf_t *ibuf, char ***wpos, char ***end)
{
	assert(ibuf != NULL);
	*wpos = &ibuf->wpos;
	if (end != NULL)
		*end = &ibuf->end;
}

