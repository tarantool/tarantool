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
#include "lua/uuid.h"

#include <stdio.h>
#include <string.h>
#include <dlfcn.h>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
} /* extern "C" */

/** libuuid API */
typedef unsigned char uuid_t[16];
static void (*uuid_generate)(uuid_t uuid);

/** box functions */
typedef int(*box_function)(struct lua_State *L);

static int
lbox_uuid_load_and_call(lua_State *L);
static int
lbox_uuid_hex_load_and_call(lua_State *L);

/* functions that are called after library is loaded */
static int
lbox_uuid_loaded(struct lua_State *L);
static int
lbox_uuid_hex_loaded(struct lua_State *L);

/**
 * We load libuuid on first call to a box.uuid*
 * function. Once the library is loaded,
 * we reset function pointers and do not attempt
 * to load it again.
 */
static box_function _lbox_uuid = lbox_uuid_load_and_call;
static box_function _lbox_uuid_hex = lbox_uuid_hex_load_and_call;

static int
loaddl_and_call(struct lua_State *L, box_function f)
{

	void *libuuid = dlopen("libuuid.so.1", RTLD_LAZY);
	if (!libuuid)
		return luaL_error(L, "box.uuid(): %s", dlerror());

	uuid_generate = (decltype(uuid_generate)) dlsym(libuuid, "uuid_generate");
	if (!uuid_generate) {
		lua_pushfstring(L, "box.uuid(): %s", dlerror());
		dlclose(libuuid);
		return lua_error(L);
	}

	_lbox_uuid = lbox_uuid_loaded;
	_lbox_uuid_hex = lbox_uuid_hex_loaded;
	return f(L);
}

/** Generate uuid (libuuid is loaded). */
static int
lbox_uuid_loaded(struct lua_State *L)
{
	uuid_t uuid;

	uuid_generate(uuid);
	lua_pushlstring(L, (char *)uuid, sizeof(uuid_t));
	return 1;
}

/** Generate uuid hex (libuuid is loaded). */
static int
lbox_uuid_hex_loaded(struct lua_State *L)
{
	unsigned i;
	char uuid_hex[ sizeof(uuid_t) * 2 + 1 ];

	uuid_t uuid;

	uuid_generate(uuid);

	for (i = 0; i < sizeof(uuid_t); i++)
		snprintf(uuid_hex + i * 2, 3, "%02x", (unsigned)uuid[ i ]);

	lua_pushlstring(L, uuid_hex, sizeof(uuid_t) * 2);
	return 1;
}

static int
lbox_uuid_load_and_call(lua_State *L)
{
	return loaddl_and_call(L, lbox_uuid_loaded);
}

static int
lbox_uuid_hex_load_and_call(lua_State *L)
{
	return loaddl_and_call(L, lbox_uuid_hex_loaded);
}

int
lbox_uuid(struct lua_State *L)
{
	return _lbox_uuid(L);
}

int lbox_uuid_hex(struct lua_State *L)
{
	return _lbox_uuid_hex(L);
}

static const struct luaL_reg lbox_uuid_meta[] = {
	{"uuid", lbox_uuid},
	{"uuid_hex",  lbox_uuid_hex},
	{NULL, NULL}
};

/** Initialize box.uuid and box.uuid_hex. */
void
tarantool_lua_uuid_init(struct lua_State *L)
{
	luaL_register(L, "box", lbox_uuid_meta);
	lua_pop(L, 1);
}

