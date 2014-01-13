/*
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
#include "lua/utils.h"

#include <math.h> /* modf */
#include <assert.h>

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#if defined(LUAJIT)
#include <lj_state.h>
#include <lj_obj.h>
#include <lj_ctype.h>
#include <lj_cdata.h>
#include <lj_cconv.h>

void *
luaL_pushcdata(struct lua_State *L, uint32_t ctypeid, uint32_t size)
{
	/*
	 * ctypeid is actually has CTypeID type.
	 * CTypeId is defined somewhere inside luajit's internal
	 * headers.
	 */
	assert(sizeof(ctypeid) == sizeof(CTypeID));
	CTState *cts = ctype_cts(L);
	CType *ct = ctype_raw(cts, ctypeid);
	CTSize sz;
	lj_ctype_info(cts, ctypeid, &sz);
	GCcdata *cd = lj_cdata_new(cts, ctypeid, size);
	TValue *o = L->top;
	setcdataV(L, o, cd);
	lj_cconv_ct_init(cts, ct, sz, (uint8_t *) cdataptr(cd), o, 0);
	incr_top(L);
	return cdataptr(cd);
}

void *
luaL_checkcdata(struct lua_State *L, int idx, uint32_t *ctypeid)
{
	/* Calculate absolute value in the stack. */
	if (idx < 0)
		idx = lua_gettop(L) + idx + 1;

	if (lua_type(L, idx) != LUA_TCDATA) {
		luaL_error(L, "expected cdata as %d argument", idx);
		return NULL;
	}

	GCcdata *cd = cdataV(L->base + idx - 1);
	*ctypeid = cd->ctypeid;
	return (void *)cdataptr(cd);
}
#endif /* defined(LUAJIT) */

static void
lua_field_inspect_table(struct lua_State *L, int idx, struct luaL_field *field)
{
	assert(lua_istable(L, idx));

	/* Calculate absolute value in the stack. */
	if (idx < 0)
		idx = lua_gettop(L) + idx + 1;

	uint32_t size = 0;
	field->type = MP_ARRAY;

	/* Calculate size and check that table can represent an array */
	lua_pushnil(L);
	while (lua_next(L, idx)) {
		size++;
		lua_pop(L, 1); /* pop the value */
		if (lua_type(L, -1) != LUA_TNUMBER ||
		    lua_tointeger(L, -1) != size) {
			field->type = MP_MAP;
			/* Finish size calculation */
			while (lua_next(L, idx)) {
				size++;
				lua_pop(L, 1);
			}
			break;
		}
	}

	/* Complete the calculation of the compound size. */

	field->size = size;

	if (lua_getmetatable(L, idx)) {
		lua_pushliteral(L, "_serializer_compact");
		lua_rawget(L, -2);
		if (lua_isboolean(L, -1) && lua_toboolean(L, -1))
			field->compact = true;
		lua_pop(L, 2); /* pop value and metatable */
	}
}

void
luaL_tofield(struct lua_State *L, int index, struct luaL_field *field)
{
	if (index < 0)
		index = lua_gettop(L) + index + 1;

	double num;
	double intpart;
	size_t size;

	switch (lua_type(L, index)) {
	case LUA_TNUMBER:
		num = lua_tonumber(L, index);
		if (modf(num, &intpart) != 0.0) {
			field->type = MP_DOUBLE;
			field->dval = num;
		} else if (num >= 0 && num <= UINT64_MAX) {
			field->type = MP_UINT;
			field->ival = (uint64_t) num;
		} else if (num >= INT64_MIN && num <= INT64_MAX) {
			field->type = MP_INT;
			field->ival = (int64_t) num;
		} else {
			field->type = MP_DOUBLE;
			field->dval = num;
		}
		return;
#if defined(LUAJIT)
	case LUA_TCDATA:
	{
		uint32_t ctypeid = 0;
		void *cdata = luaL_checkcdata(L, index, &ctypeid);
		int64_t ival;
		switch (ctypeid) {
		case CTID_BOOL:
			field->type = MP_BOOL;
			field->bval = *(bool*) cdata;
			return;
		case CTID_CCHAR:
		case CTID_INT8:
			ival = *(int8_t *) cdata;
			field->type = (ival > 0) ? MP_UINT : MP_INT;
			field->ival = ival;
			return;
		case CTID_INT16:
			ival = *(int16_t *) cdata;
			field->type = (ival > 0) ? MP_UINT : MP_INT;
			field->ival = ival;
			return;
		case CTID_INT32:
			ival = *(int32_t *) cdata;
			field->type = (ival > 0) ? MP_UINT : MP_INT;
			field->ival = ival;
			return;
		case CTID_INT64:
			ival = *(int64_t *) cdata;
			field->type = (ival > 0) ? MP_UINT : MP_INT;
			field->ival = ival;
			return;
		case CTID_UINT8:
			field->type = MP_UINT;
			field->ival = *(uint8_t *) cdata;
			return;
		case CTID_UINT16:
			field->type = MP_UINT;
			field->ival = *(uint16_t *) cdata;
			return;
		case CTID_UINT32:
			field->type = MP_UINT;
			field->ival = *(uint32_t *) cdata;
			return;
		case CTID_UINT64:
			field->type = MP_UINT;
			field->ival = *(uint64_t *) cdata;
			return;
		case CTID_FLOAT:
			field->type = MP_FLOAT;
			field->fval = *(float *) cdata;
			return;
		case CTID_DOUBLE:
			field->type = MP_DOUBLE;
			field->dval = *(double *) cdata;
			return;
		case CTID_P_VOID:
			if (*(void **) cdata == NULL) {
				field->type = MP_NIL;
				return;
			}
			/* Fall through */
		default:
			field->type = MP_EXT;
			return;
		}
		return;
	}
#endif /* defined(LUAJIT) */
	case LUA_TBOOLEAN:
		field->type = MP_BOOL;
		field->bval = lua_toboolean(L, index);
		return;
	case LUA_TNIL:
		field->type = MP_NIL;
		return;
	case LUA_TSTRING:
		field->sval.data = lua_tolstring(L, index, &size);
		field->sval.len = (uint32_t) size;
		field->type = MP_STR;
		return;
	case LUA_TTABLE:
	{
		field->compact = false;
		lua_field_inspect_table(L, index, field);
		return;
	}
	case LUA_TLIGHTUSERDATA:
	case LUA_TUSERDATA:
		field->sval.data = NULL;
		field->sval.len = 0;
		if (lua_touserdata(L, index) == NULL) {
			field->type = MP_NIL;
			return;
		}
		/* Fall through */
	default:
		field->type = MP_EXT;
		return;
	}
}

/**
 * A helper to register a single type metatable.
 */
void
luaL_register_type(struct lua_State *L, const char *type_name,
		   const struct luaL_Reg *methods)
{
	luaL_newmetatable(L, type_name);
	/*
	 * Conventionally, make the metatable point to itself
	 * in __index. If 'methods' contain a field for __index,
	 * this is a no-op.
	 */
	lua_pushvalue(L, -1);
	lua_setfield(L, -2, "__index");
	lua_pushstring(L, type_name);
	lua_setfield(L, -2, "__metatable");
	luaL_register(L, NULL, methods);
	lua_pop(L, 1);
}

int
luaL_pushnumber64(struct lua_State *L, uint64_t val)
{
	void *cdata = luaL_pushcdata(L, CTID_UINT64, sizeof(uint64_t));
	*(uint64_t*) cdata = val;
	return 1;
}

