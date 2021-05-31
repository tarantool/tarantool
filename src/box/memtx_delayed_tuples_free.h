#pragma once
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
#include "allocator.h"

#include <small/lifo.h>
#include <type_traits>

enum alloc_type {
	MEMTX_SMALL_ALLOCATOR = 0,
	MEMTX_SYSTEM_ALLOCATOR = 1,
	MEMTX_ALLOCATOR_COUNT,
};

class MemtxDelayedFree
{
public:
	static inline void
	init(void)
	{
		for (unsigned i = 0; i < MEMTX_ALLOCATOR_COUNT; i++)
			lifo_init(&lifos[i]);
	}
	template <class ALLOC>
	static inline void
	memtx_put_garbage_tuple(void *tuple)
	{
		lifo_push(get_allocator_lifo<ALLOC>(), tuple);
	}
	template <class ALLOC>
	static inline void *
	memtx_get_garbage_tuple(void)
	{
		return lifo_pop(get_allocator_lifo<ALLOC>());
	}
	template <class ALLOC>
	static inline bool
	memtx_garbage_lifo_is_empty(void)
	{
		return lifo_is_empty(get_allocator_lifo<ALLOC>());
	}
private:
	template <class ALLOC>
	static inline struct lifo *
	get_allocator_lifo(void)
	{
		if (std::is_same<ALLOC, SmallAlloc>::value)
			return &lifos[MEMTX_SMALL_ALLOCATOR];
		else if (std::is_same<ALLOC, SysAlloc>::value)
			return &lifos[MEMTX_SYSTEM_ALLOCATOR];
		unreachable();
		return NULL;
	}
	static struct lifo lifos[MEMTX_ALLOCATOR_COUNT];
};