#include "test.h"
#include "utils.h"

#include "lj_def.h"

/*
 * XXX: In C language, objects with static storage duration have
 * to be initialized with constant expressions or with aggregate
 * initializers containing constant expressions.
 *
 * Moreover, in C language, the term "constant" refers to literal
 * constants (like 1, 'a', 0xFF, and so on), enum members, and the
 * results of such operators as sizeof. Const-qualified objects
 * (of any type) are not constants in C language terminology. They
 * cannot be used in initializers of objects with the static
 * storage duration, regardless of their type.
 *
 * That's why the first one byte from the Lua bytecode signature
 * is defined explicitly below with the static assertion to
 * prevent the source code discrepancy.
 */
#define LUA_BC_HEADER '\033'
LJ_STATIC_ASSERT(LUA_SIGNATURE[0] == LUA_BC_HEADER);

/*
 * Function generates a huge chunk of "bytecode" with a size
 * bigger than LJ_MAX_BUF. The generated chunk must enable
 * endmark in a Lex state.
 */
static const char *
bc_reader_with_endmark(lua_State *L, void *data, size_t *size)
{
	UNUSED(L);
	UNUSED(data);
	*size = ~(size_t)0;

	return NULL;
}

static int bc_loader_with_endmark(void *test_state)
{
	lua_State *L = test_state;
	void *ud = NULL;
	int res = lua_load(L, bc_reader_with_endmark, ud, "endmark");

	/*
	 * Make sure we passed the condition with lj_err_mem
	 * in the function `lex_more`.
	 */
	assert_true(res != LUA_ERRMEM);
	assert_true(lua_gettop(L) == 1);
	lua_settop(L, 0);

	return TEST_EXIT_SUCCESS;
}

enum bc_emission_state {
	EMIT_BC,
	EMIT_EOF,
};

/*
 * Function returns the bytecode chunk on the first call and NULL
 * and size equal to zero on the second call. Triggers the flag
 * `END_OF_STREAM` in the function `lex_more`.
 */
static const char *
bc_reader_with_eof(lua_State *L, void *data, size_t *size)
{
	UNUSED(L);
	enum bc_emission_state *state = (enum bc_emission_state *)data;
	if (*state == EMIT_EOF) {
		*size = 0;
		return NULL;
	}

	/*
	 * `lua_load` automatically detects whether the chunk is text
	 * or binary and loads it accordingly. We need a trace for
	 * *bytecode* input, so it is necessary to deceive a check in
	 * `lj_lex_setup`, that makes a sanity check and detects
	 * whether input is bytecode or text by the first char.
	 * Put `LUA_SIGNATURE[0]` at the beginning of the allocated
	 * null-terminated region.
	 */
	static const char bc_chunk[] = {LUA_BC_HEADER, 0};
	*size = sizeof(bc_chunk);
	*state = EMIT_EOF;

	return bc_chunk;
}

static int bc_loader_with_eof(void *test_state)
{
	lua_State *L = test_state;
	enum bc_emission_state state = EMIT_BC;
	int res = lua_load(L, bc_reader_with_eof, &state, "eof");

	/*
	 * Attempt of loading LuaJIT bytecode via Lua source
	 * loader function failed: `lj_lex_setup` routine throws
	 * LUA_ERRSYNTAX error with LJ_ERR_BCBAD payload.
	 */
	assert_true(res == LUA_ERRSYNTAX);
	lua_settop(L, 0);

	return TEST_EXIT_SUCCESS;
}

int main(void)
{
	lua_State *L = utils_lua_init();
	const struct test_unit tgroup[] = {
		test_unit_def(bc_loader_with_endmark),
		test_unit_def(bc_loader_with_eof)
	};

	const int test_result = test_run_group(tgroup, L);
	utils_lua_close(L);
	return test_result;
}
