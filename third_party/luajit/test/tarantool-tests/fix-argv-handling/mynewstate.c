#include <stddef.h>

struct lua_State;

/* Error-injected mock. */
struct lua_State *luaL_newstate(void)
{
	return NULL;
}
