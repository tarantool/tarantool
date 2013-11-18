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
#include "lua/admin.h"

extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

#include <string.h>
#include <say.h>
#include <errinj.h>
#include "salloc.h"
#include "tbuf.h"
#include "fiber.h"
#include "tarantool.h"
#include "box/box.h"
#include "lua/init.h"

static int
lbox_save_coredump(struct lua_State *L __attribute__((unused)))
{
	coredump(60);
	lua_pushstring(L, "ok");
	return 1;
}

static int
lbox_save_snapshot(struct lua_State *L)
{
	int ret = snapshot();
	if (ret == 0) {
		lua_pushstring(L, "ok");
		return 1;
	}
	luaL_error(L, "can't save snapshot, errno %d (%s)",
		   ret, strerror(ret));
	return 1;
}

int tarantool_lua_admin_init(struct lua_State *L)
{
	lua_getfield(L, LUA_GLOBALSINDEX, "box");
	lua_pushstring(L, "snapshot");
	lua_pushcfunction(L, lbox_save_snapshot);
	lua_settable(L, -3);

	lua_pushstring(L, "coredump");
	lua_pushcfunction(L, lbox_save_coredump);
	lua_settable(L, -3);

	lua_pop(L, 1);
	return 0;
}
