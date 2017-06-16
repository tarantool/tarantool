/*
 * Copyright 2010-2017, Tarantool AUTHORS, please see AUTHORS file.
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
 * THIS SOFTWARE IS PROVIDED BY AUTHORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * AUTHORS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include "checkpoint.h"

#include <stdbool.h>
#include <stdint.h>

#include "engine.h"
#include "memtx_engine.h"

int64_t
checkpoint_last(struct vclock *vclock)
{
	struct MemtxEngine *memtx = (MemtxEngine *)engine_find("memtx");
	return memtx->lastSnapshot(vclock);
}

const struct vclock *
checkpoint_iterator_next(struct checkpoint_iterator *it)
{
	struct MemtxEngine *memtx = (MemtxEngine *)engine_find("memtx");
	it->curr = memtx->nextSnapshot(it->curr);
	return it->curr;
}

const struct vclock *
checkpoint_iterator_prev(struct checkpoint_iterator *it)
{
	struct MemtxEngine *memtx = (MemtxEngine *)engine_find("memtx");
	it->curr = memtx->prevSnapshot(it->curr);
	return it->curr;
}
