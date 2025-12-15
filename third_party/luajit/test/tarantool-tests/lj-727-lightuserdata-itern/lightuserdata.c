#include <lua.h>
#include <lauxlib.h>

#undef NDEBUG
#include <assert.h>
#include <string.h>

/* To stay within 47 bits, lightuserdata is segmented. */
#define LJ_LIGHTUD_BITS_SEG 8
#define NSEGMENTS (1 << LJ_LIGHTUD_BITS_SEG)

/*
 * The function to wrap: get a number to form lightuserdata to
 * return with the 0xXXXXXfff00000002 format.
 * It may raise an error, when the available lightuserdata
 * segments are run out.
 */
static int craft_ptr(lua_State *L)
{
	const unsigned long long i = lua_tonumber(L, 1);
	lua_pushlightuserdata(L, (void *)((i << 44) + 0xfff00000002));
	return 1;
}

/*
 * The function to generate bunch of lightuserdata of the
 * 0xXXXXXfff00000002 format and push the last one on the stack.
 */
static int craft_ptr_wp(lua_State *L)
{
	void *ptr = NULL;
	/*
	 * There are only 255 available lightuserdata segments.
	 * Generate a bunch of pointers to take them all.
	 * XXX: After this patch the last userdata segment is
	 * reserved for ISNEXT/ITERC/ITERN control variable, so
	 * `craft_ptr()` function will raise an error at the last
	 * iteration.
	 */
	unsigned long long i = 0;
	for (; i < NSEGMENTS; i++) {
		lua_pushcfunction(L, craft_ptr);
		lua_pushnumber(L, i);
		if (lua_pcall(L, 1, 1, 0) == LUA_OK)
			ptr = (void *)lua_topointer(L, -1);
		else
			/*
			 * The first segment is occupied by NULL
			 * from `lj_vm_cpcall()`.
			 * The last segment is reserved for LuaJIT
			 * internal usage.
			 * It would be nice to add a corresponding
			 * assertion kinda i == NSEGMENTS - 1,
			 * *but* since this test it being used by
			 * both LuaJIT and Tarantool, nobody can
			 * rely on the fact that any of them are
			 * not occupied an additional segment for
			 * lightuserdata at the start.
			 * At the same time the error message can
			 * be checked against LJ_ERR_BADLU text.
			 */
			assert(!strcmp(lua_tostring(L, -1),
				       "bad light userdata pointer"));
	}
	assert(ptr != NULL);
	/* Overwrite possible error message. */
	lua_pushlightuserdata(L, ptr);
	return 1;
}

static const struct luaL_Reg lightuserdata[] = {
	{"craft_ptr_wp", craft_ptr_wp},
	{NULL, NULL}
};

LUA_API int luaopen_lightuserdata(lua_State *L)
{
	luaL_register(L, "lightuserdata", lightuserdata);
	return 1;
}
