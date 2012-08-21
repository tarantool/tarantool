#include <dlfcn.h>
#include <string.h>
#include <stdio.h>
#include "box_lua_uuid.h"
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"


#define UUIDLIB_SOVERSION	"1"
#define LIBNAME			( "libuuid.so." UUIDLIB_SOVERSION )

typedef unsigned char uuid_t[16];

static void (*uuid_generate)(uuid_t out);
static char *error;

static void check_lib(struct lua_State *L) {
	
	if (uuid_generate)
		return;
	
	if (error)
		luaL_error( L, "box.uuid() error: %s", error );

	void *dl = dlopen(LIBNAME, RTLD_LAZY);
	if (!dl) {
		error = strdup( dlerror() );
		luaL_error( L, "box.uuid() error:  %s", error );
	}

	uuid_generate = dlsym(dl, "uuid_generate");
	if (!uuid_generate) {
		error = strdup( dlerror() );
		luaL_error( L, "box.uuid() error: %s", error );
	}
}


int lbox_uuid(struct lua_State *L) {
	check_lib(L);

	uuid_t uuid;
	uuid_generate(uuid);
	lua_pushlstring( L, (char *)uuid, sizeof(uuid_t) );

	return 1;
}

int lbox_uuid_hex(struct lua_State *L) {

	unsigned i;
	char uuid_hex[ sizeof(uuid_t) * 2 + 1 ];


	check_lib(L);
	
	uuid_t uuid;
	uuid_generate(uuid);

	for (i = 0; i < sizeof(uuid_t); i++)
		snprintf( uuid_hex + i * 2, 3, "%02x", (unsigned)uuid[ i ] );

	lua_pushlstring( L, uuid_hex, sizeof(uuid_t) * 2 );
	return 1;
}

