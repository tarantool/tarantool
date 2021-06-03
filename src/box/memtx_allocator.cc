/*
 * Copyright 2010-2021, Tarantool AUTHORS, please see AUTHORS file.
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
#include "memtx_allocator.h"

struct memtx_allocator_set_mode {
	template<typename Allocator, typename...Arg>
	void
	invoke(Arg&&...mode)
	{
		Allocator::set_mode(mode...);
	}
};

void
memtx_allocators_init(struct memtx_engine *memtx,
		      struct allocator_settings *settings)
{
	foreach_allocator<allocator_create,
		struct allocator_settings *&>(settings);

	foreach_memtx_allocator<allocator_create,
		enum memtx_engine_free_mode &>(memtx->free_mode);
}

void
memtx_allocators_set_mode(enum memtx_engine_free_mode mode)
{
	foreach_memtx_allocator<memtx_allocator_set_mode,
		enum memtx_engine_free_mode &>(mode);
}

void
memtx_allocators_destroy()
{
	foreach_memtx_allocator<allocator_destroy>();
	foreach_allocator<allocator_destroy>();
}
