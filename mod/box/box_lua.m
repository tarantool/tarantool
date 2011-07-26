/*
 * Copyright (C) 2011 Yuriy Vostrikov
 * Copyright (C) 2011 Konstantin Osipov
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
#include "tarantool.h"
#include "box.h"
/* use a full path to avoid clashes with system lua */
#include "third_party/luajit/src/lua.h"
#include "third_party/luajit/src/lauxlib.h"
#include "third_party/luajit/src/lualib.h"

/**
 * The main extention provided to Lua by
 * Tarantool -- ability to call INSERT/UPDATE/SELECT/DELETE
 * from within a Lua procedure.
 *
 * This is a low-level API, and it expects
 * all arguments to be packed in accordance
 * with the binary protocol format (iproto
 * header excluded).
 */
static int lbox_process(lua_State *L)
{
	u32 op = luaL_checkinteger(L, 1);
	struct tbuf req;
	size_t sz;
	req.data = (char *) luaL_checklstring(L, 2, &sz);
	req.size = req.len = sz;
	@try {
		rw_callback(op, &req);
	} @catch (ClientError *e) {
		return luaL_error(L, "%d:%s", e->errcode, e->errmsg);
	}
	return 0; /* nothing is added to the stack */
}

static const struct luaL_reg boxlib[] = {
	{"process", lbox_process},
	{NULL, NULL}
};

struct lua_State *
mod_lua_init(struct lua_State *L)
{
	luaL_register(L, "box", boxlib);
	return L;
}
