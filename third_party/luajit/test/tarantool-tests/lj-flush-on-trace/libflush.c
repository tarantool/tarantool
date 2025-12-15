#include <lua.h>
#include <luajit.h>

struct flush {
	lua_State *L; /* Coroutine saved to change JIT mode */
	int trigger;  /* Trigger for flushing all traces */
};

void flush(struct flush *state, int i)
{
	if (i < state->trigger)
		return;

	/* Trace flushing is triggered */
	(void)luaJIT_setmode(state->L, 0, LUAJIT_MODE_ENGINE|LUAJIT_MODE_FLUSH);
}

static int init(lua_State *L)
{
	struct flush *state = lua_newuserdata(L, sizeof(struct flush));

	state->L = L;
	state->trigger = lua_tonumber(L, 1);
	return 1;
}

LUA_API int luaopen_libflush(lua_State *L)
{
	lua_pushcfunction(L, init);
	return 1;
}
