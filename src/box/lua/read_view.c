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
#include <stdio.h>
#include <string.h>

#include "diag.h"
#include "error.h"
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

/** Lua ctype ID of struct read_view_handle pointer. */
static __thread uint32_t CTID_STRUCT_READ_VIEW_HANDLE;

/** Get a read view handle from Lua stack. */
static inline struct read_view_handle *
lbox_check_read_view_handle(struct lua_State *L, int idx)
{
	assert(CTID_STRUCT_READ_VIEW_HANDLE != 0);
	uint32_t ctypeid;
	void *cdata = luaL_checkcdata(L, idx, &ctypeid);
	assert(ctypeid == CTID_STRUCT_READ_VIEW_HANDLE);
	return *(struct read_view_handle **)cdata;
}

/** Lua ctype ID of struct space_read_view_handle pointer. */
static __thread uint32_t CTID_STRUCT_SPACE_READ_VIEW_HANDLE;

/** Get a space read view handle from Lua stack. */
static inline struct space_read_view_handle *
lbox_check_space_read_view_handle(struct lua_State *L, int idx)
{
	assert(CTID_STRUCT_SPACE_READ_VIEW_HANDLE != 0);
	uint32_t ctypeid;
	void *cdata = luaL_checkcdata(L, idx, &ctypeid);
	assert(ctypeid == CTID_STRUCT_SPACE_READ_VIEW_HANDLE);
	return *(struct space_read_view_handle **)cdata;
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

/**
 * Pushes a table that contains information about the given read view to
 * the Lua stack.
 */
static void
lbox_push_read_view_info(struct lua_State *L, const struct read_view *rv)
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
 * Helper function for lbox_new_read_view() that pushes a table for the given
 * index read view to the Lua stack.
 */
static void
lbox_push_index_read_view(struct lua_State *L,
			  struct index_read_view *index,
			  struct space_read_view_handle *space)
{
	lua_newtable(L);
	lua_pushinteger(L, index->def->iid);
	lua_setfield(L, -2, "id");
	lua_pushstring(L, index->def->name);
	lua_setfield(L, -2, "name");

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
 * Helper function for lbox_new_read_view() that pushes a table for the given
 * space read view to the Lua stack.
 */
static void
lbox_push_space_read_view(struct lua_State *L,
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
		lbox_push_index_read_view(L, index, space);
		lua_pushvalue(L, -1);
		lua_rawseti(L, -3, i);
		lua_setfield(L, -2, index->def->name);
	}
	lua_setfield(L, -2, "index");
}

/**
 * Creates a read view handle and pushes a table with the following structure
 * to the Lua stack:
 *
 * {
 *     -- Read view handle (cdata).
 *     _crv = <cdata:struct read_view_handle *>,
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
 *                     -- Space read view handle (cdata).
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
 * Returns 0 on success. On failure, sets diag and returns -1.
 */
static int
lbox_new_read_view(struct lua_State *L, struct read_view *rv)
{
	struct read_view_handle *rvh = read_view_handle_new(rv);
	if (rvh == NULL)
		return -1;

	/* Create a read view table. */
	lbox_push_read_view_info(L, rv);

	*(struct read_view_handle **)luaL_pushcdata(
		L, CTID_STRUCT_READ_VIEW_HANDLE) = rvh;
	lua_setfield(L, -2, "_crv");

	/* Create a space table. */
	lua_newtable(L);
	struct space_read_view_handle *space;
	read_view_foreach_space(space, rvh) {
		lbox_push_space_read_view(L, space);
		lua_pushvalue(L, -1);
		lua_rawseti(L, -3, space->ptr->id);
		lua_setfield(L, -2, space->ptr->name);
	}
	lua_setfield(L, -2, "space");

	return 0;
}

/**
 * Opens a database read view.
 * Takes the new read view name (string).
 * On error, raises a Lua exception.
 */
static int
lbox_read_view_open(struct lua_State *L)
{
	assert(cord_is_main());
	const char *name = luaL_checkstring(L, 1);
	struct read_view_opts opts;
	read_view_opts_create(&opts);
	opts.name = name;
	opts.filter_space = box_read_view_filter_space_cb;
	opts.filter_index = box_read_view_filter_index_cb;
	opts.enable_field_names = true;
	opts.enable_space_upgrade = true;
	opts.enable_data_temporary_spaces = true;
	struct read_view *rv = read_view_new(&opts);
	if (rv == NULL)
		return luaT_error_at(L, 2);
	if (lbox_new_read_view(L, rv) != 0) {
		read_view_delete(rv);
		return luaT_error_at(L, 2);
	}
	return 1;
}

/**
 * Pushes a pointer to the given read view encoded as a %p-formatted string
 * to the Lua stack. See also lbox_check_read_view_ptr_str.
 */
static void
lbox_push_read_view_ptr_str(struct lua_State *L, struct read_view *rv)
{
	lua_pushfstring(L, "%p", rv);
}

/**
 * Returns a pointer to a read view encoded as a %p-formatted string at
 * the given index in the Lua stack. See also lbox_push_read_view_ptr_str.
 */
static struct read_view *
lbox_check_read_view_ptr_str(struct lua_State *L, int idx)
{
	struct read_view *rv = NULL;
	VERIFY(sscanf(luaL_checkstring(L, idx), "%p", &rv) == 1);
	return rv;
}

/**
 * Increments the usage counter of a read view.
 * Takes a read view id.
 * Returns a pointer to the read view encoded as a %p-formatted string.
 * On error, raises a Lua exception.
 */
static int
lbox_read_view_acquire(struct lua_State *L)
{
	assert(cord_is_main());
	uint64_t id = luaL_checkuint64(L, 1);
	struct read_view *rv = read_view_by_id(id);
	if (rv == NULL || rv->is_close_pending) {
		diag_set(ClientError, ER_NO_SUCH_READ_VIEW);
		return luaT_error(L);
	}
	if (rv->is_system) {
		diag_set(ClientError, ER_READ_VIEW_BUSY);
		return luaT_error(L);
	}
	read_view_pin(rv);
	lbox_push_read_view_ptr_str(L, rv);
	return 1;
}

/**
 * Decrements the usage counter of a read view and deletes the read view
 * if it was closed and has no more users.
 * Takes a pointer to a read view encoded as a %p-formatted string.
 */
static int
lbox_read_view_release(struct lua_State *L)
{
	assert(cord_is_main());
	struct read_view *rv = lbox_check_read_view_ptr_str(L, 1);
	read_view_unpin(rv);
	if (rv->pin_count == 0 && rv->is_close_pending)
		read_view_delete(rv);
	return 0;
}

/**
 * Reuses an existing database read view.
 * Takes a pointer to a read view encoded as a %p-formatted string.
 * On error, raises a Lua exception.
 */
static int
lbox_read_view_reuse(struct lua_State *L)
{
	struct read_view *rv = lbox_check_read_view_ptr_str(L, 1);
	if (lbox_new_read_view(L, rv) != 0)
		return luaT_error_at(L, 2);
	return 1;
}

static bool
lbox_read_view_list_cb(struct read_view *rv, void *arg)
{
	struct lua_State *L = (struct lua_State *)arg;
	assert(lua_gettop(L) >= 1);
	assert(lua_type(L, -1) == LUA_TTABLE);
	lbox_push_read_view_info(L, rv);
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
	assert(cord_is_main());
	lua_newtable(L);
	read_view_foreach(lbox_read_view_list_cb, L);
	return 1;
}

/**
 * Given a read view object (a table that has the 'id' field), pushes
 * the read view status string ('open', 'closed', 'close_pending') to
 * the Lua stack.
 */
static int
lbox_read_view_status(struct lua_State *L)
{
	assert(cord_is_main());
	lua_getfield(L, 1, "id");
	uint64_t id = luaL_checkuint64(L, -1);
	struct read_view *rv = read_view_by_id(id);
	if (rv == NULL) {
		lua_pushliteral(L, "closed");
	} else if (rv->is_close_pending) {
		lua_pushliteral(L, "close_pending");
	} else {
		lua_pushliteral(L, "open");
	}
	return 1;
}

/**
 * Closes a database read view by the given handle cdata.
 * Logs a warning if the second argument is true.
 *
 * The behavior of this function differs between the main thread and
 * application threads:
 *  - The main thread closes the core read view if it has no users,
 *    otherwise it marks the read view as "close pending" to be closed
 *    as soon as the last user is gone.
 *  - An application thread returns a pointer to the core read view
 *    to be passed to the main thread for release.
 */
static int
lbox_read_view_close(struct lua_State *L)
{
	struct read_view_handle *rvh = lbox_check_read_view_handle(L, 1);
	struct read_view *rv = rvh->ptr;
	bool warn = lua_toboolean(L, 2);
	if (warn) {
		say_warn("read view %llu ('%s') was not properly closed",
			 (unsigned long long)rv->id, rv->name);
	}
	read_view_handle_delete(rvh);
	if (cord_is_main()) {
		assert(!rv->is_close_pending);
		if (rv->pin_count == 0) {
			read_view_delete(rv);
		} else {
			rv->is_close_pending = true;
		}
		return 0;
	} else {
		lbox_push_read_view_ptr_str(L, rv);
		return 1;
	}
}

/**
 * Gets a tuple by key from an index read view.
 * Takes space read view handle, index id, and key (tuple or Lua array).
 * Returns a tuple or nil on success. On error, raises a Lua exception.
 */
static int
lbox_index_read_view_get(struct lua_State *L)
{
	struct space_read_view_handle *space =
		lbox_check_space_read_view_handle(L, 1);
	uint32_t index_id = luaL_checkinteger(L, 2);
	struct region *region = &fiber()->gc;
	size_t region_svp = region_used(region);
	size_t key_len;
	const char *key = lbox_encode_tuple_on_gc(L, 3, &key_len);
	struct tuple *tuple;
	int rc = box_index_read_view_get(space, index_id,
					 key, key + key_len, &tuple);
	region_truncate(region, region_svp);
	if (rc != 0)
		return luaT_error(L);
	return luaT_pushtupleornil(L, tuple);
}

/**
 * Counts tuples in an index read view.
 * Takes space read view handle, index id, iterator type, and key.
 * Returns a number. On error, raises a Lua exception.
 */
static int
lbox_index_read_view_count(struct lua_State *L)
{
	struct space_read_view_handle *space =
		lbox_check_space_read_view_handle(L, 1);
	uint32_t index_id = luaL_checkinteger(L, 2);
	int iterator = luaL_checkinteger(L, 3);
	struct region *region = &fiber()->gc;
	size_t region_svp = region_used(region);
	size_t key_len;
	const char *key = lbox_encode_tuple_on_gc(L, 4, &key_len);
	ssize_t count = box_index_read_view_count(space, index_id,
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
				  struct space_read_view_handle *space,
				  uint32_t index_id,
				  const char **packed_pos,
				  const char **packed_pos_end)
{
	struct index_read_view *index = space_read_view_index(space->ptr,
							      index_id);
	return lbox_normalize_position(L, idx, index->def->cmp_def, packed_pos,
				       packed_pos_end);
}

/**
 * Selects tuples from an index read view.
 * Takes space read view handle, index id, iterator type, offset, limit,
 * key, position, and fetch_pos.
 * Returns an array of tuples and string with packed position if fetch_pos is
 * true on success. On error, raises a Lua exception.
 */
static int
lbox_index_read_view_select(struct lua_State *L)
{
	struct space_read_view_handle *space =
		lbox_check_space_read_view_handle(L, 1);
	uint32_t index_id = luaL_checkinteger(L, 2);
	int iterator = luaL_checkinteger(L, 3);
	uint32_t offset = luaL_checkinteger(L, 4);
	uint32_t limit = luaL_checkinteger(L, 5);
	struct region *region = &fiber()->gc;
	size_t region_svp = region_used(region);
	size_t key_len;
	const char *key = lbox_encode_tuple_on_gc(L, 6, &key_len);
	const char *packed_pos, *packed_pos_end;
	bool fetch_pos = lua_toboolean(L, 8);
	if (lbox_read_view_normalize_position(L, 7, space, index_id,
					      &packed_pos,
					      &packed_pos_end) != 0)
		goto fail;
	struct port port;
	int rc = box_index_read_view_select(space, index_id, iterator,
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
	if (it->space != NULL)
		box_index_read_view_iterator_destroy(&it->obj);
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
		lbox_check_index_read_view_iterator(L, 1);
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
 * Takes space read view handle, index id, iterator type, key, position,
 * and offset.
 * Returns an iterator object on success. On error, raises a Lua exception.
 */
static int
lbox_index_read_view_iterator(struct lua_State *L)
{
	struct space_read_view_handle *space =
		lbox_check_space_read_view_handle(L, 1);
	uint32_t index_id = luaL_checkinteger(L, 2);
	int iterator = luaL_checkinteger(L, 3);

	/*
	 * Store the key on Lua memory, because the iterator implementation
	 * assumes that it stays valid during the iterator lifetime.
	 */
	struct region *region = &fiber()->gc;
	size_t region_svp = region_used(region);
	size_t key_len;
	const char *key = lbox_encode_tuple_on_gc(L, 4, &key_len);
	lua_pushlstring(L, key, key_len);
	key = lua_tostring(L, -1);
	const char *packed_pos, *packed_pos_end;
	if (lbox_read_view_normalize_position(L, 5, space, index_id,
					      &packed_pos,
					      &packed_pos_end) != 0)
		goto error;
	uint32_t offset = luaL_checkinteger(L, 6);

	/* Allocate a userdata object for the new iterator. */
	struct lbox_index_read_view_iterator *it =
			lua_newuserdata(L, sizeof(*it));
	it->space = NULL;
	it->count = 0;
	luaL_getmetatable(L, lbox_index_read_view_iterator_typename);
	lua_setmetatable(L, -2);

	/* Initialize the userdata object. */
	if (box_index_read_view_create_iterator_with_offset(
			space, index_id, iterator, key, key + key_len,
			packed_pos, packed_pos_end, offset, &it->obj) != 0)
		goto error;
	it->space = space;
	region_truncate(region, region_svp);
	return 1;
error:
	region_truncate(region, region_svp);
	return luaT_error_at(L, 2);
}

void
box_lua_read_view_init(struct lua_State *L)
{
	int rc;
	const struct luaL_Reg module_methods[] = {
		{"open", lbox_read_view_open},
		{"acquire", lbox_read_view_acquire},
		{"release", lbox_read_view_release},
		{"reuse", lbox_read_view_reuse},
		{"list", lbox_read_view_list},
		{"status", lbox_read_view_status},
		{"close", lbox_read_view_close},
		{"index_get", lbox_index_read_view_get},
		{"index_count", lbox_index_read_view_count},
		{"index_select", lbox_index_read_view_select},
		{"index_iterator", lbox_index_read_view_iterator},
		{ NULL, NULL}
	};
	luaL_findtable(L, LUA_GLOBALSINDEX, "box.internal.read_view", 0);
	luaL_setfuncs(L, module_methods, 0);
	lua_pop(L, 1);

	const struct luaL_Reg index_read_view_iterator_methods[] = {
		{"__gc", lbox_index_read_view_iterator_gc },
		{"next", lbox_index_read_view_iterator_next },
		{ NULL, NULL }
	};
	luaL_register_type(L, lbox_index_read_view_iterator_typename,
			   index_read_view_iterator_methods);

	rc = luaL_cdef(L, "struct read_view_handle;");
	assert(rc == 0);
	(void)rc;
	CTID_STRUCT_READ_VIEW_HANDLE = luaL_ctypeid(
		L, "struct read_view_handle *");
	assert(CTID_STRUCT_READ_VIEW_HANDLE != 0);

	rc = luaL_cdef(L, "struct space_read_view_handle;");
	assert(rc == 0);
	(void)rc;
	CTID_STRUCT_SPACE_READ_VIEW_HANDLE = luaL_ctypeid(
		L, "struct space_read_view_handle *");
	assert(CTID_STRUCT_SPACE_READ_VIEW_HANDLE != 0);
}
