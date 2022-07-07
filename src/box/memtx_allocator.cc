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

/** Helper for memtx_allocator_enter_delayed_free_mode(). */
struct memtx_allocator_enter_delayed_free_mode {
	template<typename Allocator>
	void invoke()
	{
		Allocator::enter_delayed_free_mode();
	}
};

void
memtx_allocators_enter_delayed_free_mode()
{
	foreach_memtx_allocator<memtx_allocator_enter_delayed_free_mode>();
}

/** Helper for memtx_allocator_leave_delayed_free_mode(). */
struct memtx_allocator_leave_delayed_free_mode {
	template<typename Allocator>
	void invoke()
	{
		Allocator::leave_delayed_free_mode();
	}
};

void
memtx_allocators_leave_delayed_free_mode()
{
	foreach_memtx_allocator<memtx_allocator_leave_delayed_free_mode>();
}
