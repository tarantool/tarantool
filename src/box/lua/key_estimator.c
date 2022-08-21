/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2022, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "box/key_estimator.h"
#include "box/lua/key_estimator.h"
#include "lua/utils.h"
#include "box/lua/key_def.h"
#include "box/lua/tuple.h"
#include "box/tuple.h"

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

uint32_t CTID_STRUCT_KEY_ESTIMATOR;

enum {
	/**
	 * Precision value that is used if the precision parameter was not
	 * declared. The standard error is less than 1%.
	 */
	DEFAULT_PRECISION = 14,
};

void
luaT_push_key_estimator(struct lua_State *L, struct key_estimator *estimator)
{
	assert(CTID_STRUCT_KEY_ESTIMATOR != 0);
	struct key_estimator **cdata_ptr =
		luaL_pushcdata(L, CTID_STRUCT_KEY_ESTIMATOR);
	*cdata_ptr = estimator;
}

struct key_estimator *
luaT_is_key_estimator(struct lua_State *L, int idx)
{
	assert(CTID_STRUCT_KEY_ESTIMATOR != 0);
	if (!luaL_iscdata(L, idx))
		return NULL;

	uint32_t ctypeid;
	void *data = luaL_checkcdata(L, idx, &ctypeid);
	if (ctypeid != CTID_STRUCT_KEY_ESTIMATOR)
		return NULL;

	struct key_estimator *estimator = *(struct key_estimator **)data;
	return estimator;
}

/**
 * Check an existent tuple pointer in LUA stack by specified
 * index or attemt to construct it by LUA table.
 * Increase tuple's reference counter.
 * Returns not NULL tuple pointer on success, NULL otherwise.
 */
static struct tuple *
luaT_is_tuple_or_lua_table(struct lua_State *L, int idx)
{
	struct tuple *tuple = luaT_istuple(L, idx);
	if (tuple == NULL)
		tuple = luaT_tuple_new(L, idx, box_tuple_format_default());
	if (tuple == NULL)
		return NULL;
	tuple_ref(tuple);
	return tuple;
}

/**
 * Create a key_estimator object in lua.
 * Push a new cdata object representing a key_estimator on success, otherwise,
 * raise an error.
 */
static int
lbox_key_estimator_new(struct lua_State *L)
{
	const char *usage = "key_estimator.new(format = <key_def>"
			    "[, precision = <integer>), "
			    "representation = key_estimator.(SPARSE|DENSE)]";

	assert(CTID_STRUCT_KEY_ESTIMATOR != 0);
	int argc = lua_gettop(L);
	struct key_def *format = NULL;
	if ((argc < 1) ||
	    (argc > 3 || (format = luaT_check_key_def(L, 1)) == NULL))
		return luaL_error(L, "Usage: %s", usage);

	int precision = argc > 1 ? lua_tonumber(L, 2) : DEFAULT_PRECISION;
	enum HLL_REPRESENTATION representation = argc > 2 ? lua_tonumber(L, 3) :
							    HLL_SPARSE;

	struct key_estimator *estimator = key_estimator_new(format, precision,
							    representation);
	if (estimator == NULL)
		return luaT_error(L);

	luaT_push_key_estimator(L, estimator);
	return 1;
}

/**
 * Add a new tuple to the key_estimator object.
 * Nothing is pushed on success, otherwise, raise an error.
 */
static int
lbox_key_estimator_add(struct lua_State *L)
{
	const char *usage = "key_estimator:add(tuple = <(box.tuple|table)>)";

	assert(CTID_STRUCT_KEY_ESTIMATOR != 0);
	struct key_estimator *estimator = NULL;
	struct tuple *tuple = NULL;
	if ((lua_gettop(L) != 2) ||
	    (estimator = luaT_is_key_estimator(L, 1)) == NULL ||
	    (tuple = luaT_is_tuple_or_lua_table(L, 2)) == NULL)
		return luaL_error(L, "Usage: %s", usage);

	int rc = key_estimator_add(estimator, tuple);
	tuple_unref(tuple);
	if (rc != 0)
		return luaT_error(L);
	return 0;
}

/**
 * Merge the calling key_estimator object with the passed.
 * Nothing is pushed on success, otherwise, raise an error.
 */
static int
lbox_key_estimator_merge(struct lua_State *L)
{
	const char *usage = "key_estimator:merge(estimator = <key_estimator>)";

	assert(CTID_STRUCT_KEY_ESTIMATOR != 0);

	struct key_estimator *dst;
	struct key_estimator *src;
	if ((lua_gettop(L) != 2) ||
	    (dst = luaT_is_key_estimator(L, 1)) == NULL ||
	    (src = luaT_is_key_estimator(L, 2)) == NULL)
		return luaL_error(L, "Usage: %s", usage);

	int rc = key_estimator_merge(dst, src);
	if (rc != 0)
		return luaT_error(L);
	return 0;
}

/**
 * Estimate the cardinality of the set of added tuples.
 * Push the estimation on success, owherwise, raise an error.
 */
static int
lbox_key_estimator_estimate(struct lua_State *L)
{
	const char *usage = "key_estimator:estimate()";

	assert(CTID_STRUCT_KEY_ESTIMATOR != 0);
	struct key_estimator *estimator = NULL;
	if ((lua_gettop(L) != 1) ||
	    (estimator = luaT_is_key_estimator(L, 1)) == NULL)
		return luaL_error(L, "Usage: %s", usage);

	uint64_t estimation = key_estimator_estimate(estimator);
	lua_pushinteger(L, estimation);
	return 1;
}

/**
 * Collect the key_estimator object from the top of the Lua stack.
 */
static int
lbox_key_estimator_gc(struct lua_State *L)
{
	struct key_estimator *estimator = luaT_is_key_estimator(L, 1);
	assert(estimator != NULL);
	key_estimator_delete(estimator);
	return 0;
}

static const luaL_Reg key_estimator_mt[] = {
	{"add", lbox_key_estimator_add},
	{"merge", lbox_key_estimator_merge},
	{"estimate", lbox_key_estimator_estimate},
	{"__gc", lbox_key_estimator_gc},
	{NULL, NULL},
};

static const luaL_Reg key_estimator_lib[] = {
	{"new", lbox_key_estimator_new},
	{"add", lbox_key_estimator_add},
	{"merge", lbox_key_estimator_merge},
	{"estimate", lbox_key_estimator_estimate},
	{NULL, NULL},
};

#define luaL_table_push_integer(name, value)	\
	luaL_tablepush(name, lua_pushinteger, value)

void
luaopen_key_estimator(struct lua_State *L)
{
	luaL_register_module(L, "key_estimator", key_estimator_lib);
	luaL_table_push_integer("MIN_PRECISION", HLL_MIN_PRECISION);
	luaL_table_push_integer("MAX_PRECISION", HLL_MAX_PRECISION);
	luaL_table_push_integer("SPARSE_PRECISION", HLL_SPARSE_PRECISION);
	luaL_table_push_integer("SPARSE", HLL_SPARSE);
	luaL_table_push_integer("DENSE", HLL_DENSE);

	int rc = luaL_cdef(L, "struct key_estimator {"
			      "        struct hll *hll;"
			      "        struct key_def *format;"
			      "};");
	assert(rc == 0);
	(void)rc;
	CTID_STRUCT_KEY_ESTIMATOR =
		luaL_metatype(L, "struct key_estimator", key_estimator_mt);
	assert(CTID_STRUCT_KEY_ESTIMATOR != 0);
}
