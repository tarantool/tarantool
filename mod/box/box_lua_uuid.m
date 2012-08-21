#include <stdio.h>
#include "box_lua_uuid.h"
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#ifdef HAVE_UUID_GEN


#ifdef HAVE_UUID_H
	#include <uuid/uuid.h>
#else
	typedef unsigned char uuid_t[16];
	void uuid_generate(uuid_t out);
#endif


int lbox_uuid(struct lua_State *L) {

	uuid_t uuid;
	uuid_generate(uuid);
	lua_pushlstring(L, (char *)uuid, sizeof(uuid_t));

	return 1;
}

int lbox_uuid_hex(struct lua_State *L) {

	unsigned i;
	char uuid_hex[ sizeof(uuid_t) * 2 + 1 ];

	uuid_t uuid;
	uuid_generate(uuid);

	for (i = 0; i < sizeof(uuid_t); i++)
		snprintf(uuid_hex + i * 2, 3, "%02x", (unsigned)uuid[ i ]);

	lua_pushlstring(L, uuid_hex, sizeof(uuid_t) * 2);
	return 1;
}

#else /* HAVE_UUID_GEN */

#define LIB_IS_ABSENT_MESSAGE "libuuid was not linked with tarantool_box"

int lbox_uuid(struct lua_State *L) {
	luaL_error(L, "box.uuid(): %s", LIB_IS_ABSENT_MESSAGE);
	return 0;
}

int lbox_uuid_hex(struct lua_State *L) {
	luaL_error(L, "box.uuid_hex(): %s", LIB_IS_ABSENT_MESSAGE);
	return 0;
}

#endif /* HAVE_UUID_GEN */
