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
#include <inttypes.h>

struct lua_State;
struct luaL_Reg;
struct tarantool_cfg;
struct tbuf;

/*
 * Single global lua_State shared by core and modules.
 * Created with tarantool_lua_init().
 */
extern struct lua_State *tarantool_L;

/**
 * This is a callback used by tarantool_lua_init() to open
 * module-specific libraries into given Lua state.
 *
 * @return  L on success, 0 if out of memory
 */
struct lua_State *
mod_lua_init(struct lua_State *L);

void
tarantool_lua_register_type(struct lua_State *L, const char *type_name,
			    const struct luaL_Reg *methods);

/**
 * Create an instance of Lua interpreter and load it with
 * Tarantool modules.  Creates a Lua state, imports global
 * Tarantool modules, then calls mod_lua_init(), which performs
 * module-specific imports. The created state can be freed as any
 * other, with lua_close().
 *
 * @return  L on success, 0 if out of memory
 */
struct lua_State *
tarantool_lua_init();

void
tarantool_lua_close(struct lua_State *L);

/**
 * This function exists because lua_tostring does not use
 * __tostring metamethod, and this metamethod has to be used
 * if we want to print Lua userdata correctly.
 */
const char *
tarantool_lua_tostring(struct lua_State *L, int index);

/**
 * Convert Lua string, number or cdata (u64) to 64bit value
 */
uint64_t
tarantool_lua_tointeger64(struct lua_State *L, int idx);

/**
 * Make a new configuration available in Lua
 */
void
tarantool_lua_load_cfg(struct lua_State *L,
		       struct tarantool_cfg *cfg);

/**
 * Load and execute start-up file
 *
 * @param L is a Lua State.
 */
void
tarantool_lua_load_init_script(struct lua_State *L);

void
tarantool_lua(struct lua_State *L,
	      struct tbuf *out, const char *str);

#endif /* INCLUDES_TARANTOOL_LUA_H */
