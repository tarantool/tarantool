#include <lua.h>
#include <lauxlib.h>
#include <module.h>
#include <msgpuck.h>
#include <stddef.h>
#include <stdint.h>

#include "trivia/config.h"
#include "trivia/util.h"
#include "arrow/abi.h"

static uint32_t
rng(void)
{
	static uint32_t state = 1;
	return state = (uint64_t)state * 48271 % 0x7fffffff;
}

static int
test_lua_func(struct lua_State *L)
{
	uint32_t space_id = luaL_checkinteger(L, 1);
	uint32_t index_id = luaL_checkinteger(L, 2);
	uint32_t update_count = luaL_checkinteger(L, 3);
	uint32_t column_count = luaL_checkinteger(L, 4);

	char key[10];
	char *key_val = mp_encode_array(key, 1);

	char ops[64];
	char *ops_var = mp_encode_array(ops, 1);
	ops_var = mp_encode_array(ops_var, 3);
	ops_var = mp_encode_str0(ops_var, "=");

	for (uint32_t i = 0; i < update_count; i++) {
		char *key_end = mp_encode_uint(key_val, i + 1);

		/* Do not update the first field. */
		char *ops_end = mp_encode_uint(ops_var,
					       1 + rng() % (column_count - 1));
		ops_end = mp_encode_uint(ops_end, 0);

		box_tuple_t *result;
		if (box_update(space_id, index_id, key, key_end,
			       ops, ops_end, 0, &result) != 0) {
			return luaT_error(L);
		}
	}
	return 0;
}

LUA_API int
luaopen_column_update_module(struct lua_State *L)
{
	static const struct luaL_Reg lib[] = {
		{"test", test_lua_func},
		{NULL, NULL},
	};
	luaL_register(L, "column_update_module", lib);
	return 1;
}
