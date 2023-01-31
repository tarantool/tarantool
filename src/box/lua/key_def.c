/*
 * Copyright 2010-2019, Tarantool AUTHORS, please see AUTHORS file.
 *
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

#include "box/coll_id_cache.h"
#include "box/lua/key_def.h"
#include "box/tuple.h"
#include "diag.h"
#include "fiber.h"
#include "lua/utils.h"
#include "misc.h"
#include "tuple.h"

static uint32_t CTID_STRUCT_KEY_DEF_REF = 0;

/**
 * Free a key_def from a Lua code.
 */
static int
lbox_key_def_gc(struct lua_State *L)
{
	struct key_def *key_def = luaT_is_key_def(L, 1);
	assert(key_def != NULL);
	key_def_delete(key_def);
	return 0;
}

/**
 * Push key_def as a cdata object to a Lua stack. This function takes ownership
 * of key_def, and sets finalizer lbox_key_def_gc for it.
 */
static void
luaT_push_key_def_nodup(struct lua_State *L, const struct key_def *key_def)
{
	void *ptr = luaL_pushcdata(L, CTID_STRUCT_KEY_DEF_REF);
	*(const struct key_def **)ptr = key_def;
	lua_pushcfunction(L, lbox_key_def_gc);
	luaL_setcdatagc(L, -2);
}

void
luaT_push_key_def(struct lua_State *L, const struct key_def *key_def)
{
	luaT_push_key_def_nodup(L, key_def_dup(key_def));
}

void
luaT_push_key_def_parts(struct lua_State *L, const struct key_def *key_def)
{
	lua_createtable(L, key_def->part_count, 0);
	for (uint32_t i = 0; i < key_def->part_count; ++i) {
		const struct key_part *part = &key_def->parts[i];
		lua_newtable(L);

		lua_pushstring(L, field_type_strs[part->type]);
		lua_setfield(L, -2, "type");

		lua_pushnumber(L, part->fieldno + TUPLE_INDEX_BASE);
		lua_setfield(L, -2, "fieldno");

		if (part->path != NULL) {
			lua_pushlstring(L, part->path, part->path_len);
			lua_setfield(L, -2, "path");
		}

		lua_pushboolean(L, key_part_is_nullable(part));
		lua_setfield(L, -2, "is_nullable");

		if (part->exclude_null) {
			lua_pushboolean(L, true);
			lua_setfield(L, -2, "exclude_null");
		}

		if (part->coll_id != COLL_NONE) {
			struct coll_id *coll_id = coll_by_id(part->coll_id);
			assert(coll_id != NULL);
			lua_pushstring(L, coll_id->name);
			lua_setfield(L, -2, "collation");
		}
		lua_rawseti(L, -2, i + 1);
	}
}

/**
 * Set key_part_def from a table on top of a Lua stack.
 * The region argument is used to allocate a JSON path when
 * required.
 * When successful return 0, otherwise return -1 and set a diag.
 */
static int
luaT_key_def_set_part(struct lua_State *L, struct key_part_def *part,
		      struct region *region)
{
	*part = key_part_def_default;

	/* Set part->fieldno. */
	lua_pushstring(L, "fieldno");
	lua_gettable(L, -2);
	if (lua_isnil(L, -1)) {
		lua_pop(L, 1);
		/*
		 * 'field' is an alias for fieldno to support the
		 * same parts format as is used in
		 * <space_object>.create_index() in Lua.
		 */
		lua_getfield(L, -1, "field");
		if (lua_isnil(L, -1)) {
			diag_set(IllegalParams,
				 "fieldno or field must not be nil");
			return -1;
		}
	} else {
		lua_getfield(L, -2, "field");
		if (! lua_isnil(L, -1)) {
			diag_set(IllegalParams,
				 "Conflicting options: fieldno and field");
			return -1;
		}
		lua_pop(L, 1);
	}
	/*
	 * Transform one-based Lua fieldno to zero-based
	 * fieldno to use in key_def_new().
	 */
	part->fieldno = lua_tointeger(L, -1) - TUPLE_INDEX_BASE;
	lua_pop(L, 1);

	/* Set part->type. */
	lua_pushstring(L, "type");
	lua_gettable(L, -2);
	if (!lua_isstring(L, -1)) {
		diag_set(IllegalParams,
			 "Wrong field type: expected string, got %s",
			 lua_typename(L, lua_type(L, -1)));
		return -1;
	}
	size_t type_len;
	const char *type_name = lua_tolstring(L, -1, &type_len);
	lua_pop(L, 1);
	part->type = field_type_by_name(type_name, type_len);
	if (part->type == field_type_MAX) {
		diag_set(IllegalParams, "Unknown field type: %s", type_name);
		return -1;
	}

	/* Set part->is_nullable and part->nullable_action. */
	lua_pushstring(L, "is_nullable");
	lua_gettable(L, -2);
	if (!lua_isnil(L, -1) && lua_toboolean(L, -1) != 0) {
		part->is_nullable = true;
		part->nullable_action = ON_CONFLICT_ACTION_NONE;
	}
	lua_pop(L, 1);

	lua_pushstring(L, "exclude_null");
	lua_gettable(L, -2);
	if (!lua_isnil(L, -1) && lua_toboolean(L, -1) != 0) {
		part->exclude_null = true;
	}
	lua_pop(L, 1);

	/*
	 * Set part->coll_id using collation_id.
	 *
	 * The value will be checked in key_def_new().
	 */
	lua_pushstring(L, "collation_id");
	lua_gettable(L, -2);
	if (!lua_isnil(L, -1))
		part->coll_id = lua_tointeger(L, -1);
	lua_pop(L, 1);

	/* Set part->coll_id using collation. */
	lua_pushstring(L, "collation");
	lua_gettable(L, -2);
	if (!lua_isnil(L, -1)) {
		/* Check for conflicting options. */
		if (part->coll_id != COLL_NONE) {
			diag_set(IllegalParams, "Conflicting options: "
				 "collation_id and collation");
			return -1;
		}

		size_t coll_name_len;
		const char *coll_name = lua_tolstring(L, -1, &coll_name_len);
		struct coll_id *coll_id = coll_by_name(coll_name,
						       coll_name_len);
		if (coll_id == NULL) {
			diag_set(IllegalParams, "Unknown collation: \"%s\"",
				 coll_name);
			return -1;
		}
		part->coll_id = coll_id->id;
	}
	lua_pop(L, 1);

	/* Set part->path (JSON path). */
	lua_pushstring(L, "path");
	lua_gettable(L, -2);
	if (!lua_isnil(L, -1)) {
		size_t path_len;
		const char *path = lua_tolstring(L, -1, &path_len);
		if (json_path_validate(path, path_len, TUPLE_INDEX_BASE) != 0) {
			diag_set(IllegalParams, "invalid path");
			return -1;
		}
		if ((size_t)json_path_multikey_offset(path, path_len,
					      TUPLE_INDEX_BASE) != path_len) {
			diag_set(IllegalParams, "multikey path is unsupported");
			return -1;
		}
		char *tmp = region_alloc(region, path_len + 1);
		if (tmp == NULL) {
			diag_set(OutOfMemory, path_len + 1, "region", "path");
			return -1;
		}
		/*
		 * lua_tolstring() guarantees that a string have
		 * trailing '\0'.
		 */
		memcpy(tmp, path, path_len + 1);
		part->path = tmp;
	} else {
		part->path = NULL;
	}
	lua_pop(L, 1);
	return 0;
}

/**
 * Check an existent tuple pointer  in LUA stack by specified
 * index or attemt to construct it by LUA table.
 * Increase tuple's reference counter.
 * Returns not NULL tuple pointer on success, NULL otherwise.
 */
static struct tuple *
luaT_key_def_check_tuple(struct lua_State *L, struct key_def *key_def, int idx)
{
	struct tuple *tuple = luaT_istuple(L, idx);
	if (tuple == NULL)
		tuple = luaT_tuple_new(L, idx, box_tuple_format_default());
	if (tuple == NULL || tuple_validate_key_parts(key_def, tuple) != 0)
		return NULL;
	tuple_ref(tuple);
	return tuple;
}

struct key_def *
luaT_is_key_def(struct lua_State *L, int idx)
{
	if (lua_type(L, idx) != LUA_TCDATA)
		return NULL;

	uint32_t cdata_type;
	struct key_def **key_def_ptr = luaL_checkcdata(L, idx, &cdata_type);
	if (key_def_ptr == NULL || cdata_type != CTID_STRUCT_KEY_DEF_REF)
		return NULL;
	return *key_def_ptr;
}

int
luaT_key_def_extract_key(struct lua_State *L, int idx)
{
	struct key_def *key_def = luaT_is_key_def(L, idx);
	assert(key_def != NULL);

	if (key_def->is_multikey)
		return luaL_error(L, "multikey path is unsupported");
	struct tuple *tuple = luaT_key_def_check_tuple(L, key_def, -1);
	if (tuple == NULL)
		return luaT_error(L);

	struct region *region = &fiber()->gc;
	size_t region_svp = region_used(region);
	uint32_t key_size;
	char *key = tuple_extract_key(tuple, key_def, MULTIKEY_NONE, &key_size);
	tuple_unref(tuple);
	if (key == NULL)
		return luaT_error(L);

	struct tuple *ret =
		tuple_new(tuple_format_runtime, key, key + key_size);
	region_truncate(region, region_svp);
	if (ret == NULL)
		return luaT_error(L);
	luaT_pushtuple(L, ret);
	return 1;
}

int
luaT_key_def_compare(struct lua_State *L, int idx)
{
	struct key_def *key_def = luaT_is_key_def(L, idx);
	assert(key_def != NULL);

	if (key_def->is_multikey)
		return luaL_error(L, "multikey path is unsupported");
	if (key_def->tuple_compare == NULL) {
		enum field_type type = key_def_incomparable_type(key_def);
		assert(type != field_type_MAX);
		diag_set(IllegalParams, "Unsupported field type: %s",
			 field_type_strs[type]);
		return luaT_error(L);
	}

	struct tuple *tuple_a = luaT_key_def_check_tuple(L, key_def, -2);
	if (tuple_a == NULL)
		return luaT_error(L);
	struct tuple *tuple_b = luaT_key_def_check_tuple(L, key_def, -1);
	if (tuple_b == NULL) {
		tuple_unref(tuple_a);
		return luaT_error(L);
	}

	int rc = tuple_compare(tuple_a, HINT_NONE, tuple_b, HINT_NONE, key_def);
	tuple_unref(tuple_a);
	tuple_unref(tuple_b);
	lua_pushinteger(L, rc);
	return 1;
}

int
luaT_key_def_compare_with_key(struct lua_State *L, int idx)
{
	struct key_def *key_def = luaT_is_key_def(L, idx);
	assert(key_def != NULL);

	if (key_def->is_multikey)
		return luaL_error(L, "multikey path is unsupported");
	if (key_def->tuple_compare_with_key == NULL) {
		enum field_type type = key_def_incomparable_type(key_def);
		assert(type != field_type_MAX);
		diag_set(IllegalParams, "Unsupported field type: %s",
			 field_type_strs[type]);
		return luaT_error(L);
	}

	struct tuple *tuple = luaT_key_def_check_tuple(L, key_def, -2);
	if (tuple == NULL)
		return luaT_error(L);

	struct region *region = &fiber()->gc;
	size_t region_svp = region_used(region);
	const char *key = luaT_tuple_encode(L, -1, NULL);
	if (key == NULL || box_key_def_validate_key(key_def, key, NULL) != 0) {
		region_truncate(region, region_svp);
		tuple_unref(tuple);
		return luaT_error(L);
	}

	int rc = box_tuple_compare_with_key(tuple, key, key_def);
	region_truncate(region, region_svp);
	tuple_unref(tuple);
	lua_pushinteger(L, rc);
	return 1;
}

int
luaT_key_def_merge(struct lua_State *L, int idx_a, int idx_b)
{
	struct key_def *key_def_a = luaT_is_key_def(L, idx_a);
	struct key_def *key_def_b = luaT_is_key_def(L, idx_b);
	assert(key_def_a != NULL);
	assert(key_def_b != NULL);

	if (key_def_a->is_multikey)
		return luaL_error(L, "multikey path is unsupported");
	assert(!key_def_b->is_multikey);
	struct key_def *new_key_def = key_def_merge(key_def_a, key_def_b);
	if (new_key_def == NULL)
		return luaT_error(L);

	luaT_push_key_def(L, new_key_def);
	return 1;
}

/**
 * key_def:extract_key(tuple)
 * Stack: [1] key_def; [2] tuple.
 */
static int
lbox_key_def_extract_key(struct lua_State *L)
{
	if (lua_gettop(L) != 2 || luaT_is_key_def(L, 1) == NULL)
		return luaL_error(L, "Usage: key_def:extract_key(tuple)");
	return luaT_key_def_extract_key(L, 1);
}

/**
 * key_def:compare(tuple_a, tuple_b)
 * Stack: [1] key_def; [2] tuple_a; [3] tuple_b.
 */
static int
lbox_key_def_compare(struct lua_State *L)
{
	if (lua_gettop(L) != 3 || luaT_is_key_def(L, 1) == NULL) {
		return luaL_error(L, "Usage: key_def:compare("
				     "tuple_a, tuple_b)");
	}
	return luaT_key_def_compare(L, 1);
}

/**
 * key_def:compare_with_key(tuple, key)
 * Stack: [1] key_def; [2] tuple; [3] key.
 */
static int
lbox_key_def_compare_with_key(struct lua_State *L)
{
	if (lua_gettop(L) != 3 || luaT_is_key_def(L, 1) == NULL) {
		return luaL_error(L, "Usage: key_def:compare_with_key("
				     "tuple, key)");
	}
	return luaT_key_def_compare_with_key(L, 1);
}

/**
 * key_def:merge(second_key_def)
 * Stack: [1] key_def; [2] second_key_def.
 */
static int
lbox_key_def_merge(struct lua_State *L)
{
	int idx_a = 1;
	int idx_b = 2;
	if (lua_gettop(L) != 2 || luaT_is_key_def(L, idx_a) == NULL ||
	    luaT_is_key_def(L, idx_b) == NULL)
		return luaL_error(L, "Usage: key_def:merge(second_key_def)");
	return luaT_key_def_merge(L, idx_a, idx_b);
}

/**
 * Push a new table representing a key_def to a Lua stack.
 */
static int
lbox_key_def_to_table(struct lua_State *L)
{
	struct key_def *key_def = luaT_is_key_def(L, 1);
	if (lua_gettop(L) != 1 || key_def == NULL)
		return luaL_error(L, "Usage: key_def:totable()");
	luaT_push_key_def_parts(L, key_def);
	return 1;
}

int
lbox_key_def_new(struct lua_State *L)
{
	if (lua_gettop(L) != 1 || lua_istable(L, 1) != 1)
		return luaL_error(L, "Bad params, use: key_def.new({"
				  "{fieldno = fieldno, type = type"
				  "[, is_nullable = <boolean>]"
				  "[, exclude_null = <boolean>]"
				  "[, path = <string>]"
				  "[, collation_id = <number>]"
				  "[, collation = <string>]}, ...}");

	uint32_t part_count = lua_objlen(L, 1);

	struct region *region = &fiber()->gc;
	size_t region_svp = region_used(region);
	size_t size;
	struct key_part_def *parts =
		region_alloc_array(region, typeof(parts[0]), part_count, &size);
	if (parts == NULL) {
		diag_set(OutOfMemory, size, "region_alloc_array", "parts");
		return luaT_error(L);
	}
	if (part_count == 0) {
		diag_set(IllegalParams, "Key definition can only be constructed"
					" by using at least 1 key_part");
		return luaT_error(L);
	}

	for (uint32_t i = 0; i < part_count; ++i) {
		lua_pushinteger(L, i + 1);
		lua_gettable(L, 1);
		if (luaT_key_def_set_part(L, &parts[i], region) != 0) {
			region_truncate(region, region_svp);
			return luaT_error(L);
		}
		lua_pop(L, 1);
	}

	struct key_def *key_def = key_def_new(parts, part_count, false);
	region_truncate(region, region_svp);
	if (key_def == NULL)
		return luaT_error(L);

	/*
	 * Compare and extract key_def methods must work even with
	 * tuples with omitted (optional) fields. As there is no
	 * space format which would guarantee certain minimal
	 * field_count, pass min_field_count = 0 to ensure that
	 * functions will work correctly in such case.
	 */
	key_def_update_optionality(key_def, 0);

	luaT_push_key_def(L, key_def);
	return 1;
}

LUA_API int
luaopen_key_def(struct lua_State *L)
{
	luaL_cdef(L, "struct key_def;");
	CTID_STRUCT_KEY_DEF_REF = luaL_ctypeid(L, "struct key_def&");

	/* Export C functions to Lua. */
	static const struct luaL_Reg meta[] = {
		{"new", lbox_key_def_new},
		{"extract_key", lbox_key_def_extract_key},
		{"compare", lbox_key_def_compare},
		{"compare_with_key", lbox_key_def_compare_with_key},
		{"merge", lbox_key_def_merge},
		{"totable", lbox_key_def_to_table},
		{NULL, NULL}
	};
	luaT_newmodule(L, "key_def", meta);
	return 1;
}
