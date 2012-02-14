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

/*
 * Find out LuaJIT behavior on the current platform.
 *
 * LuaJIT uses different stack unwinding mechanisms on 32-bit x86
 * and 64-bit x86-64 hardware: on a  32-bit system it can use
 * its own * longjmp-style "internal stack unwinding".
 * Among other things, this mechanism doesn't support exception
 * propagation from Lua panic function (lua_atpanic()), and
 * this is exactly what Tarantool does: throws an exception
 * in lua_atpanic().
 *
 * Which mechanism to use is determined at library
 * compile time, by a set of flags
 * (-fexceptions -funwind-tables -DLUAJIT_UNWIND_EXTERNAL),
 * hence, when configuring, we can't just check the library file
 * to find out whether or not it will work.
 * Instead, we compile and run this test.
 *
 * http://lua-users.org/lists/lua-l/2010-04/msg00470.html
 */

#include <cstdlib>
#include <lua.hpp>

static int panic = 0;

static int lua_panic_cb(lua_State *L) {
	if (!panic++)
		throw 0;
	abort();
	return 0;
}

int
main(int argc, char * argv[])
{
	lua_State *L = luaL_newstate();
	if (L == NULL)
		return 1;
	lua_atpanic(L, lua_panic_cb);
	try {
		lua_pushstring(L, "uncallable");
		lua_call(L, 0, LUA_MULTRET);
	} catch (...) {
		/* If we're lucky, we should get here. */
	}
	lua_close(L);
	return 0;
}
