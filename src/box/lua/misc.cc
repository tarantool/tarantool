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

#include "fiber.h" /* fiber->gc() */
#include <small/region.h>
#include "lua/utils.h"
#include "lua/msgpack.h"

#include "box/box.h"
#include "box/port.h"
#include "box/tuple.h"
#include "box/tuple_format.h"
#include "box/lua/tuple.h"
#include "box/xrow.h"
#include "mpstream/mpstream.h"

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

/* }}} */

/** {{{ Lua/C implementation of index:select(): used only by Vinyl **/

static int
lbox_select(lua_State *L)
{
	if (lua_gettop(L) != 6 || !lua_isnumber(L, 1) || !lua_isnumber(L, 2) ||
		!lua_isnumber(L, 3) || !lua_isnumber(L, 4) || !lua_isnumber(L, 5)) {
		return luaL_error(L, "Usage index:select(iterator, offset, "
				  "limit, key)");
	}

	uint32_t space_id = lua_tonumber(L, 1);
	uint32_t index_id = lua_tonumber(L, 2);
	int iterator = lua_tonumber(L, 3);
	uint32_t offset = lua_tonumber(L, 4);
	uint32_t limit = lua_tonumber(L, 5);

	size_t key_len;
	const char *key = lbox_encode_tuple_on_gc(L, 6, &key_len);

	struct port port;
	if (box_select(space_id, index_id, iterator, offset, limit,
		       key, key + key_len, &port) != 0) {
		return luaT_error(L);
	}

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
	return 1; /* lua table with tuples */
}

/* }}} */

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
	struct field_def *fields = region_alloc_array(region, typeof(fields[0]),
						      count, &size);
	if (fields == NULL) {
		diag_set(OutOfMemory, size, "region_alloc_array", "fields");
		return luaT_error(L);
	}
	for (uint32_t i = 0; i < count; ++i) {
		size_t len;

		fields[i] = field_def_default;

		lua_pushinteger(L, i + 1);
		lua_gettable(L, 1);

		lua_pushstring(L, "type");
		lua_gettable(L, -2);
		if (! lua_isnil(L, -1)) {
			const char *type_name = lua_tolstring(L, -1, &len);
			fields[i].type = field_type_by_name(type_name, len);
			assert(fields[i].type != field_type_MAX);
		}
		lua_pop(L, 1);

		lua_pushstring(L, "name");
		lua_gettable(L, -2);
		assert(! lua_isnil(L, -1));
		const char *name = lua_tolstring(L, -1, &len);
		fields[i].name = (char *)region_alloc(region, len + 1);
		if (fields == NULL) {
			diag_set(OutOfMemory, size, "region_alloc",
				 "fields[i].name");
			region_truncate(region, region_svp);
			return luaT_error(L);
		}
		memcpy(fields[i].name, name, len);
		fields[i].name[len] = '\0';
		lua_pop(L, 1);
		lua_pop(L, 1);
	}
	struct tuple_dictionary *dict = tuple_dictionary_new(fields, count);
	region_truncate(region, region_svp);
	if (dict == NULL)
		return luaT_error(L);
	struct tuple_format *format =
		tuple_format_new(&tuple_format_runtime->vtab, NULL, NULL, 0,
				 NULL, 0, 0, dict, false, true);
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

void
box_lua_misc_init(struct lua_State *L)
{
	static const struct luaL_Reg boxlib_internal[] = {
		{"select", lbox_select},
		{"new_tuple_format", lbox_tuple_format_new},
		{NULL, NULL}
	};

	luaL_register(L, "box.internal", boxlib_internal);
	lua_pop(L, 1);

	int rc = luaL_cdef(L, "struct tuple_format;");
	assert(rc == 0);
	(void) rc;
	CTID_STRUCT_TUPLE_FORMAT_PTR = luaL_ctypeid(L, "struct tuple_format *");
	assert(CTID_STRUCT_TUPLE_FORMAT_PTR != 0);
}
