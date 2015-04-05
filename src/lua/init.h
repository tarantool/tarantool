#ifndef INCLUDES_TARANTOOL_LUA_H
#define INCLUDES_TARANTOOL_LUA_H
/*
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
#include <stddef.h>
#include <inttypes.h>

struct lua_State;
struct luaL_Reg;
struct tbuf;
extern bool start_loop;

/**
 * This is a callback used by tarantool_lua_init() to open
 * module-specific libraries into given Lua state.
 *
 * No return value, panics if error.
 */
extern void
box_lua_init(struct lua_State *L);

/**
 * Create an instance of Lua interpreter and load it with
 * Tarantool modules.  Creates a Lua state, imports global
 * Tarantool modules, then calls box_lua_init(), which performs
 * module-specific imports. The created state can be freed as any
 * other, with lua_close().
 *
 * @return  L on success, 0 if out of memory
 */
void
tarantool_lua_init(const char *tarantool_bin, int argc, char **argv);

/** Free Lua subsystem resources. */
void
tarantool_lua_free();

/**
 * This function exists because lua_tostring does not use
 * __tostring metamethod, and this metamethod has to be used
 * if we want to print Lua userdata correctly.
 */
const char *
tarantool_lua_tostring(struct lua_State *L, int index);

/**
 * Load and execute start-up file
 *
 * @param L is a Lua State.
 */
void
tarantool_lua_run_script(char *path, int argc, char **argv);

void
tarantool_lua(struct lua_State *L,
	      struct tbuf *out, const char *str);

extern char *history;

/**
 * Return last exception text
 */
extern "C" const char *
tarantool_error_message(void);

#endif /* INCLUDES_TARANTOOL_LUA_H */
