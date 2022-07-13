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
#include "trivia/tuple.h"

void
memtx_allocators_init(struct allocator_settings *settings)
{
	foreach_allocator<allocator_create,
		struct allocator_settings *&>(settings);

	foreach_memtx_allocator<allocator_create>();
}

void
memtx_allocators_destroy()
{
	foreach_memtx_allocator<allocator_destroy>();
	foreach_allocator<allocator_destroy>();
}

struct memtx_allocator_open_read_view {
	/** Opens a read view for the specified MemtxAllocator. */
	template<typename Allocator>
	void invoke(memtx_allocators_read_view &rv_all,
		    const struct memtx_read_view_opts &opts)
	{
		util::get<typename Allocator::ReadView *>(rv_all) =
			Allocator::open_read_view(opts);
	}
};

memtx_allocators_read_view
memtx_allocators_open_read_view(struct memtx_read_view_opts opts)
{
	memtx_allocators_read_view rv;
	foreach_memtx_allocator<memtx_allocator_open_read_view,
				memtx_allocators_read_view &,
				const struct memtx_read_view_opts &>(rv, opts);
	return rv;
}

struct memtx_allocator_close_read_view {
	/** Closes a read view and sets the read view ptr to null. */
	template<typename Allocator>
	void invoke(memtx_allocators_read_view &rv_all)
	{
		typename Allocator::ReadView *&rv =
			util::get<typename Allocator::ReadView *>(rv_all);
		Allocator::close_read_view(rv);
		rv = nullptr;
	}
};

void
memtx_allocators_close_read_view(memtx_allocators_read_view rv)
{
	foreach_memtx_allocator<memtx_allocator_close_read_view,
				memtx_allocators_read_view &>(rv);
}
