#include <stdio.h>
#include <string.h>
#include <dlfcn.h>
#include <alloca.h>
#include "box_lua_uuid.h"
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"


/* libuuid API */
typedef unsigned char uuid_t[16];
static void (*uuid_generate)(uuid_t uuid);


/* box functions */
typedef int(*box_foo)(struct lua_State *L);


static int lbox_uuid_load_and_call(lua_State *L);
static int lbox_uuid_hex_load_and_call(lua_State *L);

/* functions that are called after library is loaded */
static int lbox_uuid_loaded(struct lua_State *L);
static int lbox_uuid_hex_loaded(struct lua_State *L);

static box_foo
	_lbox_uuid = lbox_uuid_load_and_call,
	_lbox_uuid_hex = lbox_uuid_hex_load_and_call;

static int loaddl_and_call(struct lua_State *L, box_foo f) {
	
	void *libuuid = dlopen("libuuid.so.1", RTLD_LAZY);
	if (!libuuid)
		luaL_error(L, "box.uuid(): %s", dlerror());

	uuid_generate = dlsym(libuuid, "uuid_generate");
	if (uuid_generate) {
		_lbox_uuid = lbox_uuid_loaded;
		_lbox_uuid_hex = lbox_uuid_hex_loaded;
		return f(L);
	}

	/* dlclose can destroy errorstring, so keep copy */
	char *dl_error = dlerror();
	char *sdl_error = alloca(strlen(dl_error) + 1);
	strcpy(sdl_error, dl_error);
	dlclose(libuuid);
	return luaL_error(L, "box.uuid(): %s", sdl_error);
}


static int lbox_uuid_loaded(struct lua_State *L) {
	uuid_t uuid;

	uuid_generate(uuid);
	lua_pushlstring(L, (char *)uuid, sizeof(uuid_t));

	return 1;
}

static int lbox_uuid_hex_loaded(struct lua_State *L) {
	unsigned i;
	char uuid_hex[ sizeof(uuid_t) * 2 + 1 ];

	uuid_t uuid;
	
	uuid_generate(uuid);

	for (i = 0; i < sizeof(uuid_t); i++)
		snprintf(uuid_hex + i * 2, 3, "%02x", (unsigned)uuid[ i ]);

	lua_pushlstring(L, uuid_hex, sizeof(uuid_t) * 2);
	return 1;
}


static int lbox_uuid_load_and_call(lua_State *L) {
	return loaddl_and_call(L, lbox_uuid_loaded);
}

static int lbox_uuid_hex_load_and_call(lua_State *L) {
	return loaddl_and_call(L, lbox_uuid_hex_loaded);
}

int lbox_uuid(struct lua_State *L) {
	return _lbox_uuid(L);
}

int lbox_uuid_hex(struct lua_State *L) {
	return _lbox_uuid_hex(L);
}

