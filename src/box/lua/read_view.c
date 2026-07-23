/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2026, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "box/lua/read_view.h"

#include <assert.h>
#include <lua.h>
#include <lauxlib.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "fiber.h"
#include "port.h"
#include "box/index.h"
#include "box/read_view.h"
#include "box/read_view_util.h"
#include "say.h"
#include "space.h"
#include "small/region.h"
#include "trivia/util.h"
#include "tuple.h"
#include "tweaks.h"

#include "box/lua/misc.h"
#include "box/lua/tuple.h"
#include "lua/utils.h"

/** Whether to use FFI for read view methods. */
static bool box_read_view_ffi = true;
TWEAK_BOOL(box_read_view_ffi);

/** Lua ctype ID of struct space_read_view_handle pointer. */
static uint32_t CTID_STRUCT_SPACE_READ_VIEW_HANDLE;

/**
 * Wrapper around a database read view object pushed to Lua as user data.
 */
struct lbox_read_view {
	/** Wrapped database read view object. */
	struct read_view obj;
	/** Read view handle. */
	struct read_view_handle *handle;
	/**
	 * Set to true if the read view was closed.
	 * If set, the wrapped object is invalid and must not be used.
	 */
	bool is_closed;
};

/** Type name of lbox_read_view Lua user data. */
static const char lbox_read_view_typename[] = "box.read_view";

/** Get a database read view from Lua stack. */
static struct lbox_read_view *
lbox_check_read_view(struct lua_State *L, int idx)
{
	return luaL_checkudata(L, idx, lbox_read_view_typename);
}

/**
 * Wrapper around an index read view object pushed to Lua as user data.
 */
struct lbox_index_read_view {
	/** Space which this index belongs to. */
	struct space_read_view_handle *space;
	/* Ordinal number of this index in the space. */
	uint32_t index_id;
	/**
	 * Pointer to the read view that owns this index.
	 * The wrapped object is valid if and only if it hasn't been closed.
	 */
	struct lbox_read_view *read_view;
	/**
	 * Reference to the read view that owns this index.
	 * It protects the Lua read view object from garbage collection.
	 */
	int read_view_ref;
};

/** Type name of lbox_index_read_view Lua user data. */
static const char lbox_index_read_view_typename[] = "box.read_view.index";

/** Get an index read view from Lua stack. */
static inline struct lbox_index_read_view *
lbox_check_index_read_view(struct lua_State *L, int idx)
{
	return luaL_checkudata(L, idx, lbox_index_read_view_typename);
}

/** Get an index read view from Lua stack. Raise if it's closed. */
static struct lbox_index_read_view *
lbox_check_open_index_read_view_at(struct lua_State *L, int idx, int level)
{
	struct lbox_index_read_view *rv = lbox_check_index_read_view(L, idx);
	if (rv->read_view->is_closed) {
		diag_set(ClientError, ER_READ_VIEW_CLOSED);
		luaT_error_at(L, level);
	}
	return rv;
}

static inline struct lbox_index_read_view *
lbox_check_open_index_read_view(struct lua_State *L, int idx)
{
	return lbox_check_open_index_read_view_at(L, idx, 1);
}

/**
 * Wrapper around an index read view iterator object pushed to Lua as user
 * data.
 */
struct lbox_index_read_view_iterator {
	/** Wrapped index read view iterator object. */
	struct index_read_view_iterator obj;
	/** Space which the iterated index belongs to. */
	struct space_read_view_handle *space;
	/**
	 * Pointer to the read view that owns this iterator.
	 * The wrapped object is valid if and only if it hasn't been closed.
	 */
	struct lbox_read_view *read_view;
	/**
	 * Reference to the read view that owns this iterator.
	 * It protects the Lua read view object from garbage collection.
	 */
	int read_view_ref;
	/**
	 * Reference to the iterator key.
	 * An iterator assumes that the key stays valid throughout its lifetime
	 * so we copy the encoded key to Lua memory.
	 */
	int key_ref;
	/** Number of tuples returned so far. */
	uint64_t count;
};

/** Type name of lbox_index_read_view_iterator Lua user data. */
static const char lbox_index_read_view_iterator_typename[] =
					"box.read_view.iterator";

/** Get an index read view iterator from Lua stack. */
static inline struct lbox_index_read_view_iterator *
lbox_check_index_read_view_iterator(struct lua_State *L, int idx)
{
	return luaL_checkudata(L, idx, lbox_index_read_view_iterator_typename);
}

/** Get an index read view iterator from Lua stack. Raise if it's closed. */
static struct lbox_index_read_view_iterator *
lbox_check_open_index_read_view_iterator(struct lua_State *L, int idx)
{
	struct lbox_index_read_view_iterator *it =
		lbox_check_index_read_view_iterator(L, idx);
	if (it->read_view->is_closed) {
		diag_set(ClientError, ER_READ_VIEW_CLOSED);
		luaT_error(L);
	}
	return it;
}

/**
 * Pushes a table that contains information about the given read view to
 * the Lua stack.
 */
static void
lbox_push_read_view(struct lua_State *L, const struct read_view *rv)
{
	lua_newtable(L);
	luaL_pushuint64(L, rv->id);
	lua_setfield(L, -2, "id");
	lua_pushstring(L, rv->name);
	lua_setfield(L, -2, "name");
	lua_pushboolean(L, rv->is_system);
	lua_setfield(L, -2, "is_system");
	lua_pushnumber(L, rv->timestamp);
	lua_setfield(L, -2, "timestamp");
	luaT_pushvclock(L, &rv->vclock);
	lua_setfield(L, -2, "vclock");
	luaL_pushint64(L, vclock_sum(&rv->vclock));
	lua_setfield(L, -2, "signature");
}

/**
 * Helper function for lbox_read_view_open() that pushes a table for the given
 * index read view to the Lua stack.
 *
 * 'rv_idx' is the index of 'rv' in the Lua stack.
 */
static void
lbox_read_view_push_index(struct lua_State *L, int rv_idx,
			  struct lbox_read_view *rv,
			  struct index_read_view *index,
			  struct space_read_view_handle *space)
{
	assert(rv_idx >= 0);
	lua_newtable(L);
	lua_pushinteger(L, index->def->iid);
	lua_setfield(L, -2, "id");
	lua_pushstring(L, index->def->name);
	lua_setfield(L, -2, "name");

	/* Allocate a userdata object for the new index read view. */
	struct lbox_index_read_view *u = lua_newuserdata(L, sizeof(*rv));
	u->space = space;
	u->index_id = index->def->iid;
	u->read_view = rv;
	u->read_view_ref = LUA_NOREF;
	luaL_getmetatable(L, lbox_index_read_view_typename);
	lua_setmetatable(L, -2);

	/* Take a reference to the read view object. */
	lua_pushvalue(L, rv_idx);
	u->read_view_ref = luaL_ref(L, LUA_REGISTRYINDEX);

	lua_setfield(L, -2, "_impl");

	*(struct space_read_view_handle **)luaL_pushcdata(
		L, CTID_STRUCT_SPACE_READ_VIEW_HANDLE) = space;
	lua_setfield(L, -2, "_cspace");

	/*
	 * Disable FFI if the space upgrade is in progress because in this case
	 * a read from the space may involve calling a user-defined Lua
	 * function via Lua C API, which is unsafe to do over FFI.
	 */
	bool ffi = box_read_view_ffi;
	if (space->upgrade != NULL)
		ffi = false;
	lua_pushboolean(L, ffi);
	lua_setfield(L, -2, "_ffi");
}

/**
 * Helper function for lbox_read_view_open() that pushes a table for the given
 * space read view to the Lua stack.
 *
 * 'rv_idx' is the index of 'rv' in the Lua stack.
 */
static void
lbox_read_view_push_space(struct lua_State *L, int rv_idx,
			  struct lbox_read_view *rv,
			  struct space_read_view_handle *space)
{
	lua_newtable(L);
	lua_pushinteger(L, space->ptr->id);
	lua_setfield(L, -2, "id");
	lua_pushstring(L, space->ptr->name);
	lua_setfield(L, -2, "name");
	lua_newtable(L);
	for (uint32_t i = 0; i <= space->ptr->index_id_max; i++) {
		struct index_read_view *index =
			space_read_view_index(space->ptr, i);
		if (index == NULL)
			continue;
		lbox_read_view_push_index(L, rv_idx, rv, index, space);
		lua_pushvalue(L, -1);
		lua_rawseti(L, -3, i);
		lua_setfield(L, -2, index->def->name);
	}
	lua_setfield(L, -2, "index");
}

/**
 * Opens a database read view.
 * Takes the new read view name (string).
 * On success, returns a table that has the following structure:
 * {
 *     -- Read view implementation.
 *     -- Represented by struct lbox_read_view.
 *     _impl = <userdata:box.read_view>,
 *
 *     id = <number>,                            -- read view id
 *     name = <string>,                          -- read view name
 *     is_system = <bool>,                       -- system?
 *     timestamp = <number>,                     -- fiber.clock()
 *     vclock = <table>,                         -- box.info.vclock
 *     signature = <number>,                     -- box.info.signature
 *
 *     -- Table of read view spaces, keyed by space id and name.
 *     space = {
 *         [id] = {
 *             id = <number>,                    -- space id
 *             name = <string>,                  -- space name
 *
 *             -- Table of space indexes, keyed by space id and name.
 *             index = {
 *                 [id] = {
 *                     -- Index read view implementation.
 *                     -- Represented by struct lbox_index_read_view.
 *                     _impl = <userdata:box.read_view.index>,
 *
 *                     -- lbox_index_read_view->space exported for FFI.
 *                     _cspace = <cdata:struct space_read_view_handle *>,
 *
 *                     -- Set if FFI should be used for this index.
 *                     _ffi = <bool>,
 *
 *                     id = <number>,            -- index id
 *                     name = <string>,          -- index name
 *                 },
 *                 [name] = [id],
 *             },
 *         },
 *         [name] = [id],
 *     }
 * }
 *
 * On error, raises a Lua exception.
 */
static int
lbox_read_view_open(struct lua_State *L)
{
	const char *name = luaL_checkstring(L, 1);

	/* Allocate a userdata object for the new read view. */
	struct lbox_read_view *rv = lua_newuserdata(L, sizeof(*rv));
	rv->is_closed = true;
	luaL_getmetatable(L, lbox_read_view_typename);
	lua_setmetatable(L, -2);
	int rv_idx = lua_gettop(L);

	/* Open a read view. */
	struct read_view_opts opts;
	read_view_opts_create(&opts);
	opts.name = name;
	opts.filter_space = box_read_view_filter_space_cb;
	opts.filter_index = box_read_view_filter_index_cb;
	opts.enable_field_names = true;
	opts.enable_space_upgrade = true;
	opts.enable_data_temporary_spaces = true;
	if (read_view_open(&rv->obj, &opts) != 0)
		return luaT_error_at(L, 2);
	rv->handle = read_view_handle_new(&rv->obj);
	if (rv->handle == NULL) {
		read_view_close(&rv->obj);
		return luaT_error_at(L, 2);
	}
	rv->is_closed = false;

	/* Create a space table. */
	lua_newtable(L);
	struct space_read_view_handle *space;
	read_view_foreach_space(space, rv->handle) {
		lbox_read_view_push_space(L, rv_idx, rv, space);
		lua_pushvalue(L, -1);
		lua_rawseti(L, -3, space->ptr->id);
		lua_setfield(L, -2, space->ptr->name);
	}

	/* Push the read view table and set rv._impl and rv.space. */
	lbox_push_read_view(L, &rv->obj);
	lua_replace(L, 1);
	lua_setfield(L, 1, "space");
	lua_setfield(L, 1, "_impl");
	lua_settop(L, 1);
	return 1;
}

static bool
lbox_read_view_list_cb(struct read_view *rv, void *arg)
{
	struct lua_State *L = (struct lua_State *)arg;
	assert(lua_gettop(L) >= 1);
	assert(lua_type(L, -1) == LUA_TTABLE);
	lbox_push_read_view(L, rv);
	lua_rawseti(L, -2, lua_objlen(L, -2) + 1);
	return true;
}

/**
 * Pushes an unsored array of all open read views to the Lua stack.
 * Each read view is represented by a plain Lua table.
 */
static int
lbox_read_view_list(struct lua_State *L)
{
	lua_newtable(L);
	read_view_foreach(lbox_read_view_list_cb, L);
	return 1;
}

/**
 * Given a read view object (a table that has the 'id' field), pushes
 * the read view status string ('open' or 'closed') to the Lua stack.
 */
static int
lbox_read_view_status(struct lua_State *L)
{
	lua_getfield(L, 1, "id");
	uint64_t id = luaL_checkuint64(L, -1);
	struct read_view *rv = read_view_by_id(id);
	if (rv == NULL)
		lua_pushliteral(L, "closed");
	else
		lua_pushliteral(L, "open");
	return 1;
}

/**
 * Frees a database read view.
 * If the read view is still open, closes it and logs a warning.
 */
static int
lbox_read_view_gc(struct lua_State *L)
{
	struct lbox_read_view *rv = lbox_check_read_view(L, 1);
	if (!rv->is_closed) {
		say_warn("read view %llu ('%s') was not properly closed",
			 (unsigned long long)rv->obj.id, rv->obj.name);
		read_view_handle_delete(rv->handle);
		read_view_close(&rv->obj);
	}
	TRASH(rv);
	return 0;
}

/**
 * Closes a database read view unless it's already closed.
 */
static int
lbox_read_view_close(struct lua_State *L)
{
	struct lbox_read_view *rv = lbox_check_read_view(L, 1);
	if (!rv->is_closed) {
		read_view_handle_delete(rv->handle);
		read_view_close(&rv->obj);
		rv->is_closed = true;
	}
	return 0;
}

/**
 * Frees an index read view.
 * Drops a reference to the database read view.
 */
static int
lbox_index_read_view_gc(struct lua_State *L)
{
	struct lbox_index_read_view *rv = lbox_check_index_read_view(L, 1);
	luaL_unref(L, LUA_REGISTRYINDEX, rv->read_view_ref);
	TRASH(rv);
	return 0;
}

/**
 * Gets a tuple by key from an index read view.
 * Expects the key (tuple or Lua array) as the second argument.
 * Returns a tuple or nil on success. On error, raises a Lua exception.
 */
static int
lbox_index_read_view_get(struct lua_State *L)
{
	struct lbox_index_read_view *rv = lbox_check_open_index_read_view(L, 1);
	struct region *region = &fiber()->gc;
	size_t region_svp = region_used(region);
	size_t key_len;
	const char *key = lbox_encode_tuple_on_gc(L, 2, &key_len);
	struct tuple *tuple;
	int rc = box_index_read_view_get(rv->space, rv->index_id,
					 key, key + key_len, &tuple);
	region_truncate(region, region_svp);
	if (rc != 0)
		return luaT_error(L);
	return luaT_pushtupleornil(L, tuple);
}

/**
 * Count tuples in an index read view. Takes iterator type and key.
 * Returns a number. On error, raises a Lua exception.
 */
static int
lbox_index_read_view_count(struct lua_State *L)
{
	struct lbox_index_read_view *rv = lbox_check_open_index_read_view(L, 1);
	int iterator = luaL_checkinteger(L, 2);
	struct region *region = &fiber()->gc;
	size_t region_svp = region_used(region);
	size_t key_len;
	const char *key = lbox_encode_tuple_on_gc(L, 3, &key_len);
	ssize_t count = box_index_read_view_count(rv->space, rv->index_id,
						  iterator, key, key + key_len);
	region_truncate(region, region_svp);
	if (count < 0)
		return luaT_error(L);
	luaL_pushuint64(L, count);
	return 1;
}

/** Specialization of lbox_normalize_position for read views. */
static int
lbox_read_view_normalize_position(lua_State *L, int idx,
				  struct lbox_index_read_view *rv,
				  const char **packed_pos,
				  const char **packed_pos_end)
{
	struct index_read_view *index = space_read_view_index(rv->space->ptr,
							      rv->index_id);
	return lbox_normalize_position(L, idx, index->def->cmp_def, packed_pos,
				       packed_pos_end);
}

/**
 * Selects tuples from an index read view.
 * Takes iterator type, offset, limit, key, position and fetch_pos.
 * Returns an array of tuples and string with packed position if fetch_pos is
 * true on success. On error, raises a Lua exception.
 */
static int
lbox_index_read_view_select(struct lua_State *L)
{
	struct lbox_index_read_view *rv = lbox_check_open_index_read_view(L, 1);
	int iterator = luaL_checkinteger(L, 2);
	uint32_t offset = luaL_checkinteger(L, 3);
	uint32_t limit = luaL_checkinteger(L, 4);
	struct region *region = &fiber()->gc;
	size_t region_svp = region_used(region);
	size_t key_len;
	const char *key = lbox_encode_tuple_on_gc(L, 5, &key_len);
	const char *packed_pos, *packed_pos_end;
	bool fetch_pos = lua_toboolean(L, 7);
	if (lbox_read_view_normalize_position(L, 6, rv, &packed_pos,
					      &packed_pos_end) != 0)
		goto fail;
	struct port port;
	int rc = box_index_read_view_select(rv->space, rv->index_id, iterator,
					    offset, limit, key, key + key_len,
					    &packed_pos, &packed_pos_end,
					    fetch_pos, &port);
	if (rc != 0)
		goto fail;
	port_dump_lua(&port, L, PORT_DUMP_LUA_MODE_TABLE);
	port_destroy(&port);
	int ret_count = 1;
	if (fetch_pos && packed_pos != NULL) {
		lua_pushlstring(L, packed_pos, packed_pos_end - packed_pos);
		ret_count++;
	}
	region_truncate(region, region_svp);
	return ret_count;
fail:
	region_truncate(region, region_svp);
	return luaT_error(L);
}

/**
 * Frees an index read view iterator.
 * Drops a reference to the database read view and the iterator key.
 */
static int
lbox_index_read_view_iterator_gc(struct lua_State *L)
{
	struct lbox_index_read_view_iterator *it =
		lbox_check_index_read_view_iterator(L, 1);
	if (it->read_view != NULL)
		box_index_read_view_iterator_destroy(&it->obj);
	luaL_unref(L, LUA_REGISTRYINDEX, it->read_view_ref);
	luaL_unref(L, LUA_REGISTRYINDEX, it->key_ref);
	return 0;
}

/**
 * Retrieves the next tuple from an index read view iterator.
 * Returns ordinal number (1-base) and tuple on success. On EOF, returns nil.
 * On error, raises a Lua exception.
 */
static int
lbox_index_read_view_iterator_next(struct lua_State *L)
{
	struct lbox_index_read_view_iterator *it =
		lbox_check_open_index_read_view_iterator(L, 1);
	struct tuple *tuple;
	if (box_index_read_view_iterator_next(&it->obj, it->space, &tuple) != 0)
		return luaT_error(L);
	if (tuple == NULL)
		return 0;
	luaL_pushuint64(L, ++it->count);
	luaT_pushtuple(L, tuple);
	return 2;
}

/**
 * Creates an iterator over an index read view.
 * Takes iterator type, key and position.
 * Returns an iterator object on success. On error, raises a Lua exception.
 */
static int
lbox_index_read_view_iterator(struct lua_State *L)
{
	struct lbox_index_read_view *rv =
		lbox_check_open_index_read_view_at(L, 1, 2);
	int iterator = luaL_checkinteger(L, 2);

	/*
	 * Store the key on Lua memory, because the iterator implementation
	 * assumes that it stays valid during the iterator lifetime.
	 */
	struct region *region = &fiber()->gc;
	size_t region_svp = region_used(region);
	size_t key_len;
	const char *key = lbox_encode_tuple_on_gc(L, 3, &key_len);
	lua_pushlstring(L, key, key_len);
	key = lua_tostring(L, -1);
	const char *packed_pos, *packed_pos_end;
	if (lbox_read_view_normalize_position(L, 4, rv, &packed_pos,
					      &packed_pos_end) != 0)
		goto error;
	uint32_t offset = luaL_checkinteger(L, 5);

	/* Allocate a userdata object for the new iterator. */
	struct lbox_index_read_view_iterator *it =
			lua_newuserdata(L, sizeof(*it));
	it->read_view = NULL;
	it->read_view_ref = LUA_NOREF;
	it->key_ref = LUA_NOREF;
	it->count = 0;
	luaL_getmetatable(L, lbox_index_read_view_iterator_typename);
	lua_setmetatable(L, -2);

	/* Initialize the userdata object. */
	if (box_index_read_view_create_iterator_with_offset(
			rv->space, rv->index_id, iterator, key, key + key_len,
			packed_pos, packed_pos_end, offset, &it->obj) != 0)
		goto error;
	it->space = rv->space;
	it->read_view = rv->read_view;
	lua_rawgeti(L, LUA_REGISTRYINDEX, rv->read_view_ref);
	it->read_view_ref = luaL_ref(L, LUA_REGISTRYINDEX);
	lua_pushvalue(L, -2);
	it->key_ref = luaL_ref(L, LUA_REGISTRYINDEX);
	region_truncate(region, region_svp);
	return 1;
error:
	region_truncate(region, region_svp);
	return luaT_error_at(L, 2);
}

void
box_lua_read_view_init(struct lua_State *L)
{
	const struct luaL_Reg module_methods[] = {
		{"open", lbox_read_view_open },
		{"list", lbox_read_view_list},
		{"status", lbox_read_view_status},
		{ NULL, NULL }
	};
	luaL_findtable(L, LUA_GLOBALSINDEX, "box.internal.read_view", 0);
	luaL_setfuncs(L, module_methods, 0);
	lua_pop(L, 1);

	const struct luaL_Reg read_view_methods[] = {
		{"__gc", lbox_read_view_gc },
		{"close", lbox_read_view_close },
		{ NULL, NULL }
	};
	luaL_register_type(L, lbox_read_view_typename, read_view_methods);

	const struct luaL_Reg index_read_view_methods[] = {
		{"__gc", lbox_index_read_view_gc },
		{"get", lbox_index_read_view_get },
		{"count", lbox_index_read_view_count },
		{"select", lbox_index_read_view_select },
		{"iterator", lbox_index_read_view_iterator },
		{ NULL, NULL }
	};
	luaL_register_type(L, lbox_index_read_view_typename,
			   index_read_view_methods);

	const struct luaL_Reg index_read_view_iterator_methods[] = {
		{"__gc", lbox_index_read_view_iterator_gc },
		{"next", lbox_index_read_view_iterator_next },
		{ NULL, NULL }
	};
	luaL_register_type(L, lbox_index_read_view_iterator_typename,
			   index_read_view_iterator_methods);

	int rc = luaL_cdef(L, "struct space_read_view_handle;");
	assert(rc == 0);
	(void)rc;
	CTID_STRUCT_SPACE_READ_VIEW_HANDLE = luaL_ctypeid(
		L, "struct space_read_view_handle *");
	assert(CTID_STRUCT_SPACE_READ_VIEW_HANDLE != 0);
}
