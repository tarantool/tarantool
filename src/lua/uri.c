/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2021, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "lua/uri.h"

#include "lua/utils.h"
#include "uri/uri.h"
#include "diag.h"

/**
 * Add or overwrite (depends on @a overwrite) URI parameter to @a uri.
 * Parameter value is located at the top of the lua stack, parameter name is
 * in the position next to it. Allowed types for URI parameter values
 * are LUA_TSTRING, LUA_TNUMBER and LUA_TTABLE. URI parameter name should
 * be a string.
 */
static int
uri_add_param_from_lua(struct uri *uri, struct lua_State *L, bool overwrite)
{
	if (lua_type(L, -2) != LUA_TSTRING) {
		diag_set(IllegalParams, "Incorrect type for URI "
			 "parameter name: should be a string");
		return -1;
	}
	const char *name = lua_tostring(L, -2);
	if (overwrite) {
		uri_remove_param(uri, name);
	} else if (uri_param_count(uri, name) != 0) {
		return 0;
	}
	int rc = 0;
	switch (lua_type(L, -1)) {
	case LUA_TSTRING:
	case LUA_TNUMBER:
		uri_add_param(uri, name, lua_tostring(L, -1));
		break;
	case LUA_TTABLE:
		for (unsigned i = 0; i < lua_objlen(L, -1) && rc == 0; i++) {
			lua_rawgeti(L, -1, i + 1);
			const char *value = lua_tostring(L, -1);
			if (value != NULL) {
				uri_add_param(uri, name, value);
			} else {
				diag_set(IllegalParams, "Incorrect type for "
					 "URI parameter value: should "
					 "be string or number");
				rc = -1;
			}
			lua_pop(L, 1);
		}
		break;
	default:
		diag_set(IllegalParams, "Incorrect type for URI "
			 "parameter value: should be string, number or table");
		rc = -1;
	}
	return rc;
}

/**
 * Add or overwrite (depends on @a overwrite) URI parameters in @a uri.
 * Table with parameters or nil value should be located at the top of the lua
 * stack.
 */
static int
uri_add_params_from_lua(struct uri *uri, struct lua_State *L, bool overwrite)
{
	if (lua_type(L, -1) == LUA_TNIL) {
		return 0;
	} else if (lua_type(L, -1) != LUA_TTABLE) {
		diag_set(IllegalParams, "Incorrect type for URI "
			 "parameters: should be a table");
		return -1;
	}
	int rc = 0;
	lua_pushnil(L);
	while (lua_next(L, -2) != 0 && rc == 0) {
		rc = uri_add_param_from_lua(uri, L, overwrite);
		assert(rc == 0 || !diag_is_empty(diag_get()));
		lua_pop(L, 1);
	}
	return rc;
}

/**
 * Returns the type of the field, which located at the given valid
 * @a index in the table which located at the given valid @a table_idx.
 */
static int
field_type(struct lua_State *L, int table_idx, int index)
{
	assert(lua_type(L, table_idx) == LUA_TTABLE);
	lua_rawgeti(L, table_idx, index);
	int rc = lua_type(L, -1);
	lua_pop(L, 1);
	return rc;
}

/**
 * Check if there is a field with the name @a name in the table,
 * which located at the given valid @a idx, which should be a
 * positive value.
 */
static bool
is_field_present(struct lua_State *L, int idx, const char *name)
{
	assert(idx > 0);
	assert(lua_type(L, idx) == LUA_TTABLE);
	lua_pushstring(L, name);
	lua_rawget(L, idx);
	bool field_is_present = (lua_type(L, -1) != LUA_TNIL);
	lua_pop(L, 1);
	return field_is_present;
}

/**
 * Create @a uri from the table, which located at the given valid @a idx,
 * which should be a positive value.
 */
static int
uri_create_from_lua_table(struct lua_State *L, int idx, struct uri *uri)
{
	assert(idx > 0);
	assert(lua_type(L, idx) == LUA_TTABLE);
	/* There should be exactly one URI in the table */
	int size = lua_objlen(L, idx);
	int uri_count = size + is_field_present(L, idx, "uri");
	if (uri_count != 1) {
		diag_set(IllegalParams, "Invalid URI table: "
			 "expected {uri = string, params = table} "
			 "or {string, params = table}");
		return -1;
	}
	/* Table "default_params" is not allowed for single URI */
	if (is_field_present(L, idx, "default_params")) {
		diag_set(IllegalParams, "Default URI parameters are "
			 "not allowed for single URI");
		return -1;
	}
	int rc = 0;
	if (size == 1) {
		lua_rawgeti(L, idx, 1);
	} else {
		lua_pushstring(L, "uri");
		lua_rawget(L, idx);
	}
	const char *uristr = lua_tostring(L, -1);
	if (uristr != NULL) {
		rc = uri_create(uri, uristr);
		if (rc != 0) {
			diag_set(IllegalParams, "Incorrect URI: expected "
				 "host:service or /unix.socket");
		}
	} else {
		diag_set(IllegalParams, "Incorrect type for URI in nested "
			 "table: should be string, number");
		rc = -1;
	}
	lua_pop(L, 1);
	if (rc != 0)
		return rc;
	lua_pushstring(L, "params");
	lua_rawget(L, idx);
	rc = uri_add_params_from_lua(uri, L, true);
	lua_pop(L, 1);
	if (rc != 0)
		uri_destroy(uri);
	return rc;

}

int
luaT_uri_create(struct lua_State *L, int idx, struct uri *uri)
{
	int rc = 0;
	if (idx < 0)
		idx = lua_gettop(L) + idx + 1;
	assert(idx > 0);
	if (lua_isstring(L, idx)) {
		rc = uri_create(uri, lua_tostring(L, idx));
		if (rc != 0) {
			diag_set(IllegalParams, "Incorrect URI: "
				 "expected host:service or "
				 "/unix.socket");
		}
	} else if (lua_istable(L, idx)) {
		rc = uri_create_from_lua_table(L, idx, uri);
	} else if (lua_isnil(L, idx)) {
		uri_create(uri, NULL);
	} else {
		diag_set(IllegalParams, "Incorrect type for URI: "
			 "should be string, number or table");
		rc = -1;
	}
	assert(rc == 0 || !diag_is_empty(diag_get()));
	return rc;
}

/**
 * Create @a uri_set from the table, which located at the given valid @a idx,
 * which should be a positive value.
 */
static int
uri_set_create_from_lua_table(struct lua_State *L, int idx,
			      struct uri_set *uri_set)
{
	int rc = 0;
	assert(idx > 0);
	assert(lua_type(L, idx) == LUA_TTABLE);
	int size = lua_objlen(L, idx);
	struct uri uri;

	uri_set_create(uri_set, NULL);
	if (is_field_present(L, idx, "uri") ||
	    (size == 1 && field_type(L, idx, 1) != LUA_TTABLE)) {
		rc = luaT_uri_create(L, idx, &uri);
		if (rc == 0) {
			uri_set_add(uri_set, &uri);
			uri_destroy(&uri);
		}
		return rc;
	} else if (size == 0) {
		return 0;
	}

	/*
	 * All numeric keys corresponds to URIs in string or table
	 * format.
	 */
	for (int i = 0; i < size && rc == 0; i++) {
		lua_rawgeti(L, idx, i + 1);
		rc = luaT_uri_create(L, -1, &uri);
		if (rc == 0) {
			uri_set_add(uri_set, &uri);
			uri_destroy(&uri);
		}
		lua_pop(L, 1);
	}
	if (rc != 0)
		goto fail;

	/*
	 * Here we are only in case when it is an URI array, so it
	 * shouldn't be "params" field here.
	 */
	if (is_field_present(L, idx, "params")) {
		diag_set(IllegalParams, "URI parameters are "
			 "not allowed for multiple URIs");
		goto fail;
	}

	lua_pushstring(L, "default_params");
	lua_rawget(L, idx);
	if (!lua_isnil(L, -1)) {
		for (int i = 0; i < uri_set->uri_count && rc == 0; i++) {
			struct uri *uri = &uri_set->uris[i];
			rc = uri_add_params_from_lua(uri, L, false);
			assert(rc == 0 || !diag_is_empty(diag_get()));
		}
	}
	lua_pop(L, 1);
	if (rc != 0)
		goto fail;
	return 0;
fail:
	uri_set_destroy(uri_set);
	return -1;
}

int
luaT_uri_set_create(struct lua_State *L, int idx, struct uri_set *uri_set)
{
	int rc = 0;
	if (idx < 0)
		idx = lua_gettop(L) + idx + 1;
	assert(idx > 0);
	if (lua_isstring(L, idx)) {
		rc = uri_set_create(uri_set, lua_tostring(L, idx));
		if (rc != 0) {
			diag_set(IllegalParams, "Incorrect URI: "
				 "expected host:service or "
				 "/unix.socket");
		}
	} else if (lua_istable(L, idx)) {
		rc = uri_set_create_from_lua_table(L, idx, uri_set);
	} else if (lua_isnil(L, idx)) {
		uri_set_create(uri_set, NULL);
	} else {
		diag_set(IllegalParams, "Incorrect type for URI: "
			 "should be string, number or table");
		rc = -1;
	}
	assert(rc == 0 || !diag_is_empty(diag_get()));
	return rc;
}

static int
luaT_uri_create_internal(lua_State *L)
{
	struct uri *uri = (struct uri *)lua_topointer(L, 1);
	if (uri == NULL)
		luaL_error(L, "Usage: uri_lib.uri_create(string|table)");
	if (luaT_uri_create(L, 2, uri) != 0)
		luaT_error(L);
	return 0;
}

static int
luaT_uri_set_create_internal(lua_State *L)
{
	struct uri_set *uri_set = (struct uri_set *)lua_topointer(L, 1);
	if (uri_set == NULL)
		luaL_error(L, "Usage: uri_lib.uri_set_create(string|table)");
	if (luaT_uri_set_create(L, 2, uri_set) != 0)
		luaT_error(L);
	return 0;
}

void
tarantool_lua_uri_init(struct lua_State *L)
{
	/* internal table */
	static const struct luaL_Reg uri_methods[] = {
		{"uri_create", luaT_uri_create_internal},
		{"uri_set_create", luaT_uri_set_create_internal},
		{NULL, NULL}
	};
	luaT_newmodule(L, "uri.lib", uri_methods);
	lua_pop(L, 1);
};
