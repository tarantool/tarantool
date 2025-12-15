#include "lua.h"
/* XXX: The "lj_arch.h" header is included for the skipcond. */
#include "lj_arch.h"

#include "test.h"

#include <stdlib.h>

/*
 * LuaJIT requires at least 12000 something bytes for initial
 * allocations. The `GG_State` requires a little bit more than
 * 6000 bytes (around 3000 bytes is the `jit_State`).
 */

/* Currently allocated Lua memory and its limit. */
static size_t current_memory = 0;
const size_t memory_limit = 7000;

void *limited_alloc_f(void *msp, void *ptr, size_t osize, size_t nsize)
{
	void *ret_ptr = NULL;
	/* Overflow is OK here. */
	const size_t requested_diff = nsize - osize;
	(void)msp;

	if (current_memory + requested_diff > memory_limit)
		return NULL;

	if (nsize == 0) {
		free(ptr);
		current_memory -= osize;
	} else if (ptr == NULL) {
		ret_ptr = malloc(nsize);
		current_memory += ret_ptr ? nsize : 0;
	} else {
		ret_ptr = realloc(ptr, nsize);
		current_memory += ret_ptr ? requested_diff : 0;
	}
	return ret_ptr;
}

static int limited_memory_on_lua_newstate(void *test_state)
{
	(void)test_state;
#if LJ_64 && !LJ_GC64
	(void)limited_alloc_f;
	return skip("Can't use custom allocator for 64-bit host without GC64");
#else
	/*
	 * Check that there is no crash and the limit is small enough.
	 */
	const lua_State *L = lua_newstate(limited_alloc_f, NULL);
	assert_true(L == NULL);
	return TEST_EXIT_SUCCESS;
#endif
}

#ifndef LJ_NO_UNWIND
#  define LJ_NO_UNWIND 0
#endif

int main(void)
{
	/* See https://github.com/LuaJIT/LuaJIT/issues/1311. */
	if (!LJ_NO_UNWIND)
		return skip_all("Disabled for external unwinding build due to #1311");
	const struct test_unit tgroup[] = {
		test_unit_def(limited_memory_on_lua_newstate),
	};
	return test_run_group(tgroup, NULL);
}
