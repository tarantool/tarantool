#include "lua.h"
#include "lauxlib.h"

#include <sys/mman.h>
#include <unistd.h>

#include "test.h"
#include "utils.h"

#define START ((void *)-1)

/* XXX: Still need normal assert to validate mmap correctness. */
#undef NDEBUG
#include <assert.h>

static int crafted_ptr(void *test_state)
{
	lua_State *L = test_state;
	/*
	 * We know that for arm64 at least 48 bits are available.
	 * So emulate manually push of lightuseradata within
	 * this range.
	 */
	void *longptr = (void *)(1llu << 48);
	lua_pushlightuserdata(L, longptr);
	assert_ptr_equal(longptr, lua_topointer(L, -1));
	/* Clear our stack. */
	lua_pop(L, 0);
	return TEST_EXIT_SUCCESS;
}

static int mmapped_ptr(void *test_state)
{
	lua_State *L = test_state;
	/*
	 * If start mapping address is not NULL, then the kernel
	 * takes it as a hint about where to place the mapping, so
	 * we try to get the highest memory address by hint, that
	 * equals to -1.
	 */
	const size_t pagesize = getpagesize();
	void *mmapped = mmap(START, pagesize, PROT_NONE, MAP_PRIVATE | MAP_ANON,
			    -1, 0);
	if (mmapped != MAP_FAILED) {
		lua_pushlightuserdata(L, mmapped);
		assert_ptr_equal(mmapped, lua_topointer(L, -1));
		assert(munmap(mmapped, pagesize) == 0);
	}
	/* Clear our stack. */
	lua_pop(L, 0);
	return TEST_EXIT_SUCCESS;
}

int main(void)
{
	lua_State *L = utils_lua_init();
	const struct test_unit tgroup[] = {
		test_unit_def(crafted_ptr),
		test_unit_def(mmapped_ptr)
	};
	const int test_result = test_run_group(tgroup, L);
	utils_lua_close(L);
	return test_result;
}
