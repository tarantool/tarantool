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
#include "coro.h"

#include "trivia/config.h"
#include <unistd.h>
#include <string.h>
#include <sys/mman.h>
#include "small/slab_cache.h"
#include "third_party/valgrind/memcheck.h"
#include "diag.h"

int
tarantool_coro_create(struct tarantool_coro *coro,
		      struct slab_cache *slabc,
		      void (*f) (void *), void *data)
{
	const int page = sysconf(_SC_PAGESIZE);

	memset(coro, 0, sizeof(*coro));

	/* TODO: guard pages */
#ifdef __APPLE__
	/*
         * We are on the verge of stack overflow already, and without
         * guard pages it is super fragile. Especially se_metaserialize
         * is wicked (srmeta meta[1024] local, sizeof meta[0] == 48).
         *
         * Mysteriously it only fails on osx and since no one really
         * cares about Tarantool performance on osx we simply make fiber
         * stacks bigger.
	 */
	coro->stack_size = page * 32 - slab_sizeof();
#else
	coro->stack_size = page * 16 - slab_sizeof();
#endif
	coro->stack = (char *) slab_get(slabc, coro->stack_size)
					+ slab_sizeof();

	if (coro->stack == NULL) {
		diag_set(OutOfMemory, coro->stack_size + slab_sizeof(),
			 "runtime arena", "coro stack");
		return -1;
	}

	(void) VALGRIND_STACK_REGISTER(coro->stack, (char *)
				       coro->stack + coro->stack_size);

	coro_create(&coro->ctx, f, data, coro->stack, coro->stack_size);
	return 0;
}

void
tarantool_coro_destroy(struct tarantool_coro *coro, struct slab_cache *slabc)
{
	if (coro->stack != NULL) {
		slab_put(slabc, (struct slab *)
			 ((char *) coro->stack - slab_sizeof()));
	}
}
