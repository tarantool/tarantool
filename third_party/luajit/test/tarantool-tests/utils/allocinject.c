#include "lua.h"
#include "lauxlib.h"

#undef NDEBUG
#include <assert.h>

static lua_Alloc old_allocf = NULL;
static void *old_alloc_state = NULL;

/* Functions to be used instead of the default allocator. */

/* Always OOM on allocation (not on realloc). */
static void *allocf_inj_null_alloc(void *ud, void *ptr, size_t osize,
				   size_t nsize)
{
	assert(old_allocf != NULL);
	if (ptr == NULL)
		return NULL;
	else
		return old_allocf(ud, ptr, osize, nsize);
}

/* Returns `NULL` on reallocations doubling used memory. */
static void *allocf_inj_null_doubling_realloc(void *ud, void *ptr, size_t osize,
					      size_t nsize)
{
	assert(old_allocf != NULL);
	/*
	 * Check the specific reallocation related to the IR
	 * buffer or the snapshot buffer.
	 */
	if (osize * 2 == nsize)
		return NULL;
	return old_allocf(ud, ptr, osize, nsize);
}

static size_t limit = 0;
/* Returns `NULL` on allocations beyond the given limit. */
static void *allocf_inj_null_limited_alloc(void *ud, void *ptr, size_t osize,
					   size_t nsize)
{
	assert(old_allocf != NULL);
	assert(limit != 0);
	/* Check the specific allocation. */
	if (osize == 0 && nsize > limit)
		return NULL;
	return old_allocf(ud, ptr, osize, nsize);
}

static int enable(lua_State *L, lua_Alloc allocf_with_injection)
{
	assert(old_allocf == NULL);
	old_allocf = lua_getallocf(L, &old_alloc_state);
	lua_setallocf(L, allocf_with_injection, old_alloc_state);
	return 0;
}

static int enable_null_alloc(lua_State *L)
{
	return enable(L, allocf_inj_null_alloc);
}

static int enable_null_doubling_realloc(lua_State *L)
{
	return enable(L, allocf_inj_null_doubling_realloc);
}

static int enable_null_limited_alloc(lua_State *L)
{
	limit = lua_tointeger(L, 1);
	assert(limit != 0);
	return enable(L, allocf_inj_null_limited_alloc);
}

/* Restore the default allocator function. */
static int disable(lua_State *L)
{
	assert(old_allocf != NULL);
	lua_setallocf(L, old_allocf, old_alloc_state);
	old_allocf = NULL;
	old_alloc_state = NULL;
	return 0;
}

static const struct luaL_Reg allocinject[] = {
	{"enable_null_alloc", enable_null_alloc},
	{"enable_null_doubling_realloc", enable_null_doubling_realloc},
	{"enable_null_limited_alloc", enable_null_limited_alloc},
	{"disable", disable},
	{NULL, NULL}
};

LUA_API int luaopen_allocinject(lua_State *L)
{
	luaL_register(L, "allocinject", allocinject);
	return 1;
}
