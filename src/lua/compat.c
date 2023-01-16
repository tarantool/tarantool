/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2022-2022, Tarantool AUTHORS, please see AUTHORS file.
 */

#include <lua/compat.h>
#include <lib/core/fiber_channel.h>
#include "serializer.h"

/* Toggler for msgpuck escaping change. */
static int
lbox_msgpuck_escape_forward_slash_toggle(struct lua_State *L)
{
	assert(lua_isboolean(L, -1));
	bool esc_slash = lua_toboolean(L, -1);

	/*
	 * The table is changed outright, because runtime check (like one in
	 * json serializer) is slower, more complicated and is not required here
	 * as there is no need to have different behavior for different mp
	 * instances, unlike in json.
	 */
	mp_char2escape['/'] = esc_slash ? "\\/" : NULL;

	return 0;
}

/* Toggler for json.encode escaping change. */
static int
lbox_json_escape_forward_slash_toggle(struct lua_State *L)
{
	assert(lua_isboolean(L, -1));
	bool esc_slash = lua_toboolean(L, -1);

	serializer_set_option_default("encode_escape_forward_slash", esc_slash);

	return 0;
}

static const char double_newline[] = "\n\n";
static const char single_newline[] = "\n";

const char *FORCE_LITERAL_SUBSTRING = double_newline;

/* Toggler for lyaml multiline string encoding change. */
static int
yaml_pretty_multiline_toggle(struct lua_State *L)
{
	assert(lua_isboolean(L, -1));
	bool is_new = lua_toboolean(L, -1);

	FORCE_LITERAL_SUBSTRING = is_new ? single_newline : double_newline;

	return 0;
}

/* Toggler for fiber channel graceful close change. */
static int
fiber_channel_close_mode_toggle(struct lua_State *L)
{
	assert(lua_isboolean(L, -1) && "boolean argument expected");
	bool is_new = lua_toboolean(L, -1);
	enum fiber_channel_close_mode mode = is_new ?
		FIBER_CHANNEL_CLOSE_GRACEFUL :
		FIBER_CHANNEL_CLOSE_FORCEFUL;

	fiber_channel_set_close_mode(mode);

	return 0;
}

static const struct luaL_Reg internal_compat[] = {
	{"msgpuck_escape_forward_slash_toggle",
	 lbox_msgpuck_escape_forward_slash_toggle},
	{"json_escape_forward_slash_toggle",
	 lbox_json_escape_forward_slash_toggle},
	{"yaml_pretty_multiline_toggle",
	 yaml_pretty_multiline_toggle},
	{"fiber_channel_close_mode_toggle",
	 fiber_channel_close_mode_toggle},
	{NULL, NULL},
};

int
tarantool_lua_compat_init(struct lua_State *L)
{
	luaT_newmodule(L, "internal.compat", internal_compat);
	return 1;
}
