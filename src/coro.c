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
#include "coro.h"

#include "trivia/config.h"
#include <unistd.h>
#include <string.h>
#include <sys/mman.h>
#include "small/slab_cache.h"
#include "third_party/valgrind/memcheck.h"
#include "diag.h"
#include "tt_pthread.h"
#if ENABLE_ASAN
#include <sanitizer/asan_interface.h>
#endif

/*
 * coro_stack_size, coro_stack_offset
 * coro_guard_size, coro_guard_offset
 *
 * Stack geometry: relative placement of stack section and guard
 * section, if any. Offsets are relative to the begining of an aligned
 * memory block to host both stack and guard side by side.
 *
 * Note: we assume that the memory comes from a slab allocator and
 * contains a slab header at the beginning we should not touch.
 */
static size_t coro_page_size;
static size_t coro_stack_size;
static int coro_stack_direction;

enum {
	CORO_STACK_PAGES = 16,
};

static inline void *
coro_page_align_down(void *ptr)
{
	return (void *)((intptr_t)ptr & ~(coro_page_size - 1));
}

static inline void *
coro_page_align_up(void *ptr)
{
	return coro_page_align_down(ptr + coro_page_size - 1);
}

static __attribute__((noinline)) bool
test_stack_grows_down(void *prev_stack_frame)
{
	return __builtin_frame_address(0) < prev_stack_frame;
}

void
tarantool_coro_init()
{
	coro_page_size = sysconf(_SC_PAGESIZE);
	coro_stack_size = coro_page_size * CORO_STACK_PAGES;
	coro_stack_direction =
		test_stack_grows_down(__builtin_frame_address(0)) ? -1: 1;
}

int
tarantool_coro_create(struct tarantool_coro *coro,
		      struct slab_cache *slabc,
		      void (*f) (void *), void *data)
{
	memset(coro, 0, sizeof(*coro));

	coro->stack_slab = (char *) slab_get(slabc, coro_stack_size);

	if (coro->stack_slab == NULL) {
		diag_set(OutOfMemory, coro_stack_size,
			 "runtime arena", "coro stack");
		return -1;
	}
	void *guard;
	/* Adjust begin and size for stack memory chunk. */
	if (coro_stack_direction < 0) {
		/*
		 * A stack grows down. First page after begin of a
		 * stack memory chunk should be protected and memory
		 * after protected page until end of memory chunk can be
		 * used for coro stack usage.
		 */
		guard = coro_page_align_up(coro->stack_slab + slab_sizeof());
		coro->stack = guard + coro_page_size;
		coro->stack_size = coro_stack_size -
				   (coro->stack - coro->stack_slab);

	} else {
		/*
		 * A stack grows up. Last page should be protected and
		 * memory from begin of chunk until protected page can
		 * be used for coro stack usage
		 */
		guard = coro_page_align_down(coro->stack_slab +
					     coro_stack_size) -
			coro_page_size;
		coro->stack = coro->stack_slab + slab_sizeof();
		coro->stack_size = guard - coro->stack;
	}

	coro->stack_id = VALGRIND_STACK_REGISTER(coro->stack,
						 (char *) coro->stack +
						 coro->stack_size);

	mprotect(guard, coro_page_size, PROT_NONE);

	coro_create(&coro->ctx, f, data, coro->stack, coro->stack_size);
	return 0;
}

void
tarantool_coro_destroy(struct tarantool_coro *coro, struct slab_cache *slabc)
{
	if (coro->stack != NULL) {
		VALGRIND_STACK_DEREGISTER(coro->stack_id);
#if ENABLE_ASAN
		ASAN_UNPOISON_MEMORY_REGION(coro->stack, coro->stack_size);
#endif
		void *guard;
		if (coro_stack_direction < 0)
			guard = coro_page_align_up(coro->stack_slab + slab_sizeof());
		else
			guard = coro_page_align_down(coro->stack_slab +
						     coro_stack_size) -
				coro_page_size;

		mprotect(guard, coro_page_size, PROT_READ | PROT_WRITE);
		slab_put(slabc, coro->stack_slab);
	}
}
