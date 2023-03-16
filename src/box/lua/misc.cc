/*
 * Copyright 2010-2016, Tarantool AUTHORS, please see AUTHORS file.
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
#include "box/lua/misc.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "box/authentication.h"
#include "box/box.h"
#include "box/errcode.h"
#include "box/lua/tuple.h"
#include "box/port.h"
#include "box/read_view.h"
#include "box/tuple.h"
#include "box/tuple_format.h"
#include "box/txn.h"
#include "box/xrow.h"
#include "core/diag.h"
#include "core/fiber.h"
#include "core/tt_static.h"
#include "lua/utils.h"
#include "lua/msgpack.h"
#include "mpstream/mpstream.h"
#include "small/region.h"
#include "small/rlist.h"

static uint32_t CTID_STRUCT_TUPLE_FORMAT_PTR;

/** {{{ Miscellaneous utils **/

char *
lbox_encode_tuple_on_gc(lua_State *L, int idx, size_t *p_len)
{
	struct region *gc = &fiber()->gc;
	size_t used = region_used(gc);
	struct mpstream stream;
	mpstream_init(&stream, gc, region_reserve_cb, region_alloc_cb,
			luamp_error, L);
	luamp_encode_tuple(L, luaL_msgpack_default, &stream, idx);
	mpstream_flush(&stream);
	*p_len = region_used(gc) - used;
	return (char *) region_join_xc(gc, *p_len);
}

int
lbox_normalize_position(lua_State *L, int idx, int space_id, int index_id,
			const char **packed_pos, const char **packed_pos_end)
{
	if (lua_isnil(L, idx)) {
		*packed_pos = NULL;
		*packed_pos_end = NULL;
	} else if (lua_isstring(L, idx)) {
		size_t size;
		*packed_pos = lua_tolstring(L, idx, &size);
		*packed_pos_end = *packed_pos + size;
	} else if (lua_istable(L, idx) || luaT_istuple(L, idx) != NULL) {
		size_t size;
		const char *tuple = lbox_encode_tuple_on_gc(L, idx, &size);
		if (box_index_tuple_position(space_id, index_id, tuple,
					     tuple + size, packed_pos,
					     packed_pos_end) != 0)
			return -1;
	} else {
		diag_set(ClientError, ER_ITERATOR_POSITION);
		return -1;
	}
	return 0;
}

/**
 * __index metamethod for the formatted array table that lookups field by name.
 * Metatable of the table is expected to have `field_map` that provides
 * name->index dictionary.
 */
static int
lua_formatted_array_index(lua_State *L)
{
	/* L stack: table, field_name. */

	assert(lua_gettop(L) == 2);

	if (lua_getmetatable(L, 1) == 0) {
		lua_settop(L, 0);
		return 0;
	}

	/* L stack: table, field_name, metatable. */

	lua_getfield(L, 3, "field_map");
	if (lua_type(L, 4) != LUA_TTABLE) {
		lua_settop(L, 0);
		return 0;
	}
	lua_remove(L, 3);

	/* L stack: table, field_name, field_map. */

	lua_pushvalue(L, 2);
	lua_remove(L, 2);

	/* L stack: table, field_map, field_name. */

	lua_gettable(L, 2);
	if (lua_type(L, 3) != LUA_TNUMBER) {
		lua_settop(L, 0);
		return 0;
	}
	lua_remove(L, 2);

	/* L stack: table, field_index. */

	lua_gettable(L, 1);
	lua_remove(L, 1);

	return 1;
}

/**
 * Set metatable for lua table on the top of lua stack @a L that would provide
 * access by names in it according to given @a format.
 * Lua table (that is on the top of L) is expected to be array-like.
 */
static void
lua_wrap_formatted_array(struct lua_State *L, struct tuple_format *format)
{
	assert(format != NULL);
	assert(lua_type(L, -1) == LUA_TTABLE);
	if (format->dict->name_count == 0)
		/* No names - no reason to wrap. */
		return;

	lua_newtable(L); /* metatable */
	lua_newtable(L); /* metatable.field_map */

	for (size_t i = 0; i < format->dict->name_count; i++) {
		lua_pushnumber(L, i + 1);
		lua_setfield(L, -2, format->dict->names[i]);
	}

	lua_setfield(L, -2, "field_map");

	lua_pushcfunction(L, lua_formatted_array_index);
	lua_setfield(L, -2, "__index");
	lua_setmetatable(L, -2);
}

extern "C" void
port_c_dump_lua(struct port *base, struct lua_State *L, bool is_flat)
{
	struct port_c *port = (struct port_c *)base;
	if (!is_flat)
		lua_createtable(L, port->size, 0);
	struct port_c_entry *pe = port->first;
	const char *mp;
	for (int i = 0; pe != NULL; pe = pe->next) {
		if (pe->mp_size == 0) {
			luaT_pushtuple(L, pe->tuple);
		} else {
			mp = pe->mp;
			luamp_decode(L, luaL_msgpack_default, &mp);

			if (pe->mp_format != NULL) {
				assert(mp_typeof(*pe->mp) == MP_ARRAY);
				lua_wrap_formatted_array(L, pe->mp_format);
			}
		}
		if (!is_flat)
			lua_rawseti(L, -2, ++i);
	}
}

extern "C" void
port_msgpack_dump_lua(struct port *base, struct lua_State *L, bool is_flat)
{
	(void) is_flat;
	assert(is_flat == true);
	struct port_msgpack *port = (struct port_msgpack *) base;

	const char *args = port->data;
	uint32_t arg_count = mp_decode_array(&args);
	for (uint32_t i = 0; i < arg_count; i++)
		luamp_decode(L, luaL_msgpack_default, &args);
}

/** Generate unique id for non-system space. */
static int
lbox_generate_space_id(lua_State *L)
{
	uint32_t ret = 0;
	if (box_generate_space_id(&ret) != 0)
		return luaT_error(L);
	lua_pushnumber(L, ret);
	return 1;
}

/* }}} */

/** {{{ Helper that generates user auth data. **/

/**
 * Takes authentication method name (e.g. 'chap-sha1') and a password.
 * Returns authentication data that can be stored in the _user space.
 * Raises Lua error if the specified authentication method doesn't exist.
 */
static int
lbox_prepare_auth(lua_State *L)
{
	size_t method_name_len;
	const char *method_name = luaL_checklstring(L, 1, &method_name_len);
	size_t password_len;
	const char *password = luaL_checklstring(L, 2, &password_len);
	const struct auth_method *method = auth_method_by_name(method_name,
							       method_name_len);
	if (method == NULL) {
		diag_set(ClientError, ER_UNKNOWN_AUTH_METHOD,
			 tt_cstr(method_name, method_name_len));
		return luaT_error(L);
	}
	struct region *region = &fiber()->gc;
	size_t region_svp = region_used(region);
	const char *auth_data, *auth_data_end;
	auth_data_prepare(method, password, password_len,
			  &auth_data, &auth_data_end);
	luamp_decode(L, luaL_msgpack_default, &auth_data);
	assert(auth_data == auth_data_end);
	(void)auth_data_end;
	region_truncate(region, region_svp);
	return 1;
}

/* }}} */

/** {{{ Lua/C implementation of index:select(): used only by Vinyl **/

static int
lbox_select(lua_State *L)
{
	if (lua_gettop(L) != 8 || !lua_isnumber(L, 1) || !lua_isnumber(L, 2) ||
	    !lua_isnumber(L, 3) || !lua_isnumber(L, 4) || !lua_isnumber(L, 5) ||
	    !lua_isboolean(L, 8)) {
		return luaL_error(L, "Usage index:select(iterator, offset, "
				  "limit, key, after, fetch_pos)");
	}

	uint32_t svp = region_used(&fiber()->gc);
	int ret_count = 1;
	struct port port;

	uint32_t space_id = lua_tonumber(L, 1);
	uint32_t index_id = lua_tonumber(L, 2);
	int iterator = lua_tonumber(L, 3);
	uint32_t offset = lua_tonumber(L, 4);
	uint32_t limit = lua_tonumber(L, 5);

	size_t key_len;
	const char *key = lbox_encode_tuple_on_gc(L, 6, &key_len);
	const char *packed_pos, *packed_pos_end;
	bool fetch_pos = lua_toboolean(L, 8);
	if (lbox_normalize_position(L, 7, space_id, index_id, &packed_pos,
				    &packed_pos_end) != 0)
		goto fail;

	if (box_select(space_id, index_id, iterator, offset, limit, key,
		       key + key_len, &packed_pos, &packed_pos_end, fetch_pos,
		       &port) != 0)
		goto fail;
	/*
	 * Lua may raise an exception during allocating table or pushing
	 * tuples. In this case `port' definitely will leak. It is possible to
	 * wrap lbox_port_to_table() to pcall(), but it was too expensive
	 * for this binding according to our benchmarks (~5% decrease).
	 * However, we tried to simulate this situation and LuaJIT finalizers
	 * table always crashed the first (can't be fixed with pcall).
	 * https://github.com/tarantool/tarantool/issues/1182
	 */
	port_dump_lua(&port, L, false);
	port_destroy(&port);
	if (fetch_pos && packed_pos != NULL) {
		lua_pushlstring(L, packed_pos, packed_pos_end - packed_pos);
		ret_count++;
	}
	region_truncate(&fiber()->gc, svp);
	return ret_count;
fail:
	region_truncate(&fiber()->gc, svp);
	return luaT_error(L);
}

/* }}} */

/**
 * Lua/C wrapper over box_txn_set_isolation. Is used in lua sources instead of
 * a ffi call because box_txn_set_isolation yields occasionally.
 */
static int
lbox_txn_set_isolation(struct lua_State *L)
{
	if (lua_gettop(L) != 1 || !lua_isnumber(L, 1))
		return luaL_error(L, "Usage txn_set_isolation(level)");
	uint32_t level = lua_tonumber(L, 1);
	int rc = box_txn_set_isolation(level);
	lua_pushnumber(L, rc);
	return 1;
}

/** {{{ Utils to work with tuple_format. **/

struct tuple_format *
lbox_check_tuple_format(struct lua_State *L, int narg)
{
	uint32_t ctypeid;
	struct tuple_format *format =
		*(struct tuple_format **)luaL_checkcdata(L, narg, &ctypeid);
	if (ctypeid != CTID_STRUCT_TUPLE_FORMAT_PTR) {
		luaL_error(L, "Invalid argument: 'struct tuple_format *' "
			   "expected, got %s)",
			   lua_typename(L, lua_type(L, narg)));
	}
	return format;
}

static int
lbox_tuple_format_gc(struct lua_State *L)
{
	struct tuple_format *format =  lbox_check_tuple_format(L, 1);
	tuple_format_unref(format);
	return 0;
}

static int
lbox_push_tuple_format(struct lua_State *L, struct tuple_format *format)
{
	struct tuple_format **ptr = (struct tuple_format **)
		luaL_pushcdata(L, CTID_STRUCT_TUPLE_FORMAT_PTR);
	*ptr = format;
	tuple_format_ref(format);
	lua_pushcfunction(L, lbox_tuple_format_gc);
	luaL_setcdatagc(L, -2);
	return 1;
}

static int
lbox_tuple_format_new(struct lua_State *L)
{
	assert(CTID_STRUCT_TUPLE_FORMAT_PTR != 0);
	int top = lua_gettop(L);
	if (top == 0)
		return lbox_push_tuple_format(L, tuple_format_runtime);
	assert(top == 1 && lua_istable(L, 1));
	uint32_t count = lua_objlen(L, 1);
	if (count == 0)
		return lbox_push_tuple_format(L, tuple_format_runtime);
	size_t size;
	struct region *region = &fiber()->gc;
	size_t region_svp = region_used(region);
	struct field_def *fields =
		(struct field_def *)xregion_alloc_array(region,
							struct field_def, count,
							&size);
	for (uint32_t i = 0; i < count; ++i) {
		size_t len;
		fields[i] = field_def_default;
		lua_pushinteger(L, i + 1);
		lua_gettable(L, 1);
		lua_pushstring(L, "name");
		lua_gettable(L, -2);
		assert(! lua_isnil(L, -1));
		const char *name = lua_tolstring(L, -1, &len);
		fields[i].name = (char *)xregion_alloc(region, len + 1);
		memcpy(fields[i].name, name, len);
		fields[i].name[len] = '\0';
		lua_pop(L, 1);
		lua_pop(L, 1);
	}
	struct tuple_dictionary *dict = tuple_dictionary_new(fields, count);
	region_truncate(region, region_svp);
	if (dict == NULL)
		return luaT_error(L);
	struct tuple_format *format = runtime_tuple_format_new(dict);
	/*
	 * Since dictionary reference counter is 1 from the
	 * beginning and after creation of the tuple_format
	 * increases by one, we must decrease it once.
	 */
	tuple_dictionary_unref(dict);
	if (format == NULL)
		return luaT_error(L);
	return lbox_push_tuple_format(L, format);
}

/* }}} */

/** {{{ Read view utils. **/

void
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

/* }}} */

void
box_lua_misc_init(struct lua_State *L)
{
	static const struct luaL_Reg boxlib_internal[] = {
		{"prepare_auth", lbox_prepare_auth},
		{"select", lbox_select},
		{"new_tuple_format", lbox_tuple_format_new},
		{"txn_set_isolation", lbox_txn_set_isolation},
		{"read_view_list", lbox_read_view_list},
		{"read_view_status", lbox_read_view_status},
		{"generate_space_id", lbox_generate_space_id},
		{NULL, NULL}
	};

	luaL_findtable(L, LUA_GLOBALSINDEX, "box.internal", 0);
	luaL_setfuncs(L, boxlib_internal, 0);
	lua_pop(L, 1);

	int rc = luaL_cdef(L, "struct tuple_format;");
	assert(rc == 0);
	(void) rc;
	CTID_STRUCT_TUPLE_FORMAT_PTR = luaL_ctypeid(L, "struct tuple_format *");
	assert(CTID_STRUCT_TUPLE_FORMAT_PTR != 0);
}
