#include <uuid/uuid.h>
#include <string.h>
#include <stdio.h>
#include "box_lua_uuid.h"
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#define UUIDLIB_SOVERSION	"1"
#define LIBNAME			( "libuuid.so." UUIDLIB_SOVERSION )


int lbox_uuid(struct lua_State *L) {

	uuid_t uuid;
	uuid_generate(uuid);
	lua_pushlstring( L, (char *)uuid, sizeof(uuid_t) );

	return 1;
}

int lbox_uuid_hex(struct lua_State *L) {

	unsigned i;
	char uuid_hex[ sizeof(uuid_t) * 2 + 1 ];

	uuid_t uuid;
	uuid_generate(uuid);

	for (i = 0; i < sizeof(uuid_t); i++)
		snprintf( uuid_hex + i * 2, 3, "%02x", (unsigned)uuid[ i ] );

	lua_pushlstring( L, uuid_hex, sizeof(uuid_t) * 2 );
	return 1;
}

