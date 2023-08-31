/*
 * Copyright 2010-2015, Tarantool AUTHORS, please see AUTHORS file.
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
#include "lua/utils.h"

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "core/datetime.h"
#include "core/diag.h"
#include "core/fiber.h"
#include "core/say.h"
#include "core/tt_uuid.h"
#include "lj_trace.h"
#include "lua/serializer.h"
#include "trivia/util.h"
#include "vclock/vclock.h"

int luaL_nil_ref = LUA_REFNIL;

static int luaT_newthread_ref = LUA_NOREF;

static uint32_t CTID_STRUCT_IBUF;
static uint32_t CTID_STRUCT_IBUF_PTR;
uint32_t CTID_CHAR_PTR;
uint32_t CTID_CONST_CHAR_PTR;
uint32_t CTID_VARBINARY;
uint32_t CTID_UUID;
uint32_t CTID_DATETIME = 0;
uint32_t CTID_INTERVAL = 0;

/** A copy of index2adr() from luajit/src/lj_api.c. */
static TValue *
index2adr(lua_State *L, int idx)
{
	if (idx > 0) {
		TValue *o = L->base + (idx - 1);
		return o < L->top ? o : niltv(L);
	} else if (idx > LUA_REGISTRYINDEX) {
		assert(idx != 0 && -idx <= L->top - L->base);
		return L->top + idx;
	} else if (idx == LUA_GLOBALSINDEX) {
		TValue *o = &G(L)->tmptv;
		settabV(L, o, tabref(L->env));
		return o;
	} else if (idx == LUA_REGISTRYINDEX) {
		return registry(L);
	} else {
		GCfunc *fn = curr_func(L);
		assert(fn->c.gct == ~LJ_TFUNC && !isluafunc(fn));
		if (idx == LUA_ENVIRONINDEX) {
			TValue *o = &G(L)->tmptv;
			settabV(L, o, tabref(fn->c.env));
			return o;
		} else {
			idx = LUA_GLOBALSINDEX - idx;
			return idx <= fn->c.nupvalues ?
			       &fn->c.upvalue[idx - 1] : niltv(L);
		}
	}
}

void *
luaL_pushcdata(struct lua_State *L, uint32_t ctypeid)
{
	/*
	 * ctypeid is actually has CTypeID type.
	 * CTypeId is defined somewhere inside luajit's internal
	 * headers.
	 */
	assert(sizeof(ctypeid) == sizeof(CTypeID));

	/* Code below is based on ffi_new() from luajit/src/lib_ffi.c */

	/* Get information about ctype */
	CTSize size;
	CTState *cts = ctype_cts(L);
	CTInfo info = lj_ctype_info(cts, ctypeid, &size);
	assert(size != CTSIZE_INVALID);

	/* Allocate a new cdata */
	GCcdata *cd = lj_cdata_new(cts, ctypeid, size);

	/* Anchor the uninitialized cdata with the stack. */
	TValue *o = L->top;
	setcdataV(L, o, cd);
	incr_top(L);

	/*
	 * lj_cconv_ct_init is omitted for non-structs because it actually
	 * does memset()
	 * Caveats: cdata memory is returned uninitialized
	 */
	if (ctype_isstruct(info)) {
		/* Initialize cdata. */
		CType *ct = ctype_raw(cts, ctypeid);
		lj_cconv_ct_init(cts, ct, size, cdataptr(cd), o,
				 (MSize)(L->top - o));
		/* Handle ctype __gc metamethod. Use the fast lookup here. */
		cTValue *tv = lj_tab_getinth(cts->miscmap, -(int32_t)ctypeid);
		if (tv && tvistab(tv) && (tv = lj_meta_fast(L, tabV(tv), MM_gc))) {
			GCtab *t = cts->finalizer;
			if (gcref(t->metatable)) {
				/* Add to finalizer table, if still enabled. */
				copyTV(L, lj_tab_set(L, t, o), tv);
				lj_gc_anybarriert(L, t);
				cd->marked |= LJ_GC_CDATA_FIN;
			}
		}
	}

	lj_gc_check(L);
	return cdataptr(cd);
}

void
luaT_pushvclock(struct lua_State *L, const struct vclock *vclock)
{
	lua_createtable(L, 0, vclock_size(vclock));
	struct vclock_iterator it;
	vclock_iterator_init(&it, vclock);
	vclock_foreach(&it, replica) {
		lua_pushinteger(L, replica.id);
		luaL_pushuint64(L, replica.lsn);
		lua_settable(L, -3);
	}
	luaL_setmaphint(L, -1); /* compact flow */
}

/*
 * Note: varbinary is a VLS object so we can't use luaL_pushcdata and
 * luaL_checkcdata helpers.
 */
void
luaT_pushvarbinary(struct lua_State *L, const char *data, uint32_t len)
{
	assert(CTID_VARBINARY != 0);
	/* Calculate the cdata size. */
	CTState *cts = ctype_cts(L);
	CType *ct = ctype_raw(cts, CTID_VARBINARY);
	CTSize size;
	CTInfo info = lj_ctype_info(cts, CTID_VARBINARY, &size);
	size = lj_ctype_vlsize(cts, ct, (CTSize)len);
	assert(size != CTSIZE_INVALID);
	/* Allocate a new cdata. */
	GCcdata *cd = lj_cdata_newx(cts, CTID_VARBINARY, size, info);
	/* Anchor the uninitialized cdata with the stack. */
	TValue *o = L->top;
	setcdataV(L, o, cd);
	incr_top(L);
	/* Initialize the cdata. */
	memcpy(cdataptr(cd), data, len);
	lj_gc_check(L);
}

const char *
luaT_tovarbinary(struct lua_State *L, int index, uint32_t *len)
{
	assert(CTID_VARBINARY != 0);
	TValue *o = index2adr(L, index);
	if (!tviscdata(o))
		return NULL;
	GCcdata *cd = cdataV(o);
	if (cd->ctypeid != CTID_VARBINARY)
		return NULL;
	CTSize size = cdatavlen(cd);
	assert(size != CTSIZE_INVALID);
	*len = size;
	return cdataptr(cd);
}

struct tt_uuid *
luaT_newuuid(struct lua_State *L)
{
	return luaL_pushcdata(L, CTID_UUID);
}

struct tt_uuid *
luaT_pushuuid(struct lua_State *L, const struct tt_uuid *uuid)
{
	struct tt_uuid *res = luaT_newuuid(L);
	memcpy(res, uuid, sizeof(struct tt_uuid));
	return res;
}

void
luaT_pushuuidstr(struct lua_State *L, const struct tt_uuid *uuid)
{
	/*
	 * Do not use a global buffer. It might be overwritten if GC starts
	 * working.
	 */
	char str[UUID_STR_LEN + 1];
	tt_uuid_to_string(uuid, str);
	lua_pushlstring(L, str, UUID_STR_LEN);
}

struct datetime *
luaT_newdatetime(struct lua_State *L)
{
	return luaL_pushcdata(L, CTID_DATETIME);
}

struct datetime *
luaT_pushdatetime(struct lua_State *L, const struct datetime *dt)
{
	struct datetime *res = luaT_newdatetime(L);
	memcpy(res, dt, sizeof(struct datetime));
	return res;
}

struct interval *
luaT_newinterval(struct lua_State *L)
{
	return luaL_pushcdata(L, CTID_INTERVAL);
}

struct interval *
luaT_pushinterval(struct lua_State *L, const struct interval *itv)
{
	struct interval *res = luaT_newinterval(L);
	memcpy(res, itv, sizeof(struct interval));
	return res;
}

void *
luaL_tocpointer(lua_State *L, int idx, uint32_t *ctypeid)
{
	TValue *o = index2adr(L, idx);
	if (!tviscdata(o)) {
		if (ctypeid != NULL)
			*ctypeid = 0;
		return NULL;
	}
	GCcdata *cd = cdataV(o);
	if (ctypeid != NULL)
		*ctypeid = cd->ctypeid;
	return cdataptr(cd);
}

int
luaL_iscdata(struct lua_State *L, int idx)
{
	return lua_type(L, idx) == LUA_TCDATA;
}

void *
luaL_checkcdata(struct lua_State *L, int idx, uint32_t *ctypeid)
{
	void *cdata = luaL_tocpointer(L, idx, ctypeid);
	if (cdata == NULL)
		luaL_error(L, "expected cdata as %d argument", idx);
	return cdata;
}

uint32_t
luaL_ctypeid(struct lua_State *L, const char *ctypename)
{
	int idx = lua_gettop(L);
	/* This function calls ffi.typeof to determine CDataType */

	/* Get ffi.typeof function */
	luaL_loadstring(L, "return require('ffi').typeof");
	lua_call(L, 0, 1);
	/* FFI must exist */
	assert(lua_gettop(L) == idx + 1 && lua_isfunction(L, idx + 1));
	/* Push the first argument to ffi.typeof */
	lua_pushstring(L, ctypename);
	/* Call ffi.typeof() */
	lua_call(L, 1, 1);
	/* Returned type must be LUA_TCDATA with CTID_CTYPEID */
	uint32_t ctypetypeid;
	CTypeID ctypeid = *(CTypeID *)luaL_checkcdata(L, idx + 1, &ctypetypeid);
	assert(ctypetypeid == CTID_CTYPEID);

	lua_settop(L, idx);
	return ctypeid;
}

uint32_t
luaL_metatype(struct lua_State *L, const char *ctypename,
	      const struct luaL_Reg *methods)
{
	/* Create a metatable for our ffi metatype. */
	luaL_register_type(L, ctypename, methods);
	int idx = lua_gettop(L);
	/*
	 * Get ffi.metatype function. It is like typeof with
	 * an additional effect of registering a metatable for
	 * all the cdata objects of the type.
	 */
	luaL_loadstring(L, "return require('ffi').metatype");
	lua_call(L, 0, 1);
	assert(lua_gettop(L) == idx + 1 && lua_isfunction(L, idx + 1));
	lua_pushstring(L, ctypename);
	/* Push the freshly created metatable as the second parameter. */
	luaL_getmetatable(L, ctypename);
	assert(lua_gettop(L) == idx + 3 && lua_istable(L, idx + 3));
	lua_call(L, 2, 1);
	uint32_t ctypetypeid;
	CTypeID ctypeid = *(CTypeID *)luaL_checkcdata(L, idx + 1, &ctypetypeid);
	assert(ctypetypeid == CTID_CTYPEID);

	lua_settop(L, idx);
	return ctypeid;
}

int
luaL_cdef(struct lua_State *L, const char *what)
{
	int idx = lua_gettop(L);
	(void) idx;
	/* This function calls ffi.cdef  */

	/* Get ffi.typeof function */
	luaL_loadstring(L, "return require('ffi').cdef");
	lua_call(L, 0, 1);
	/* FFI must exist */
	assert(lua_gettop(L) == idx + 1 && lua_isfunction(L, idx + 1));
	/* Push the argument to ffi.cdef */
	lua_pushstring(L, what);
	/* Call ffi.cdef() */
	return lua_pcall(L, 1, 0, 0);
}

/** Based on ffi_gc() from luajit/src/lib_ffi.c. */
void
luaL_setcdatagc(struct lua_State *L, int idx)
{
	/* Get cdata from the stack */
	assert(luaL_iscdata(L, idx));
	TValue *o = index2adr(L, idx);
	GCcdata *cd = cdataV(o);

	/* Get finalizer from the stack */
	TValue *fin = lj_lib_checkany(L, lua_gettop(L));

#if !defined(NDEBUG)
	CTState *cts = ctype_cts(L);
	CType *ct = ctype_raw(cts, cd->ctypeid);
	(void) ct;
	assert(ctype_isptr(ct->info) || ctype_isstruct(ct->info) ||
	       ctype_isrefarray(ct->info));
#endif /* !defined(NDEBUG) */

	/* Set finalizer */
	lj_cdata_setfin(L, cd, gcval(fin), itype(fin));

	/* Pop finalizer */
	lua_pop(L, 1);
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
	luaL_setfuncs(L, methods, 0);
	lua_pop(L, 1);
}

int
luaT_newmodule(struct lua_State *L, const char *modname,
	       const struct luaL_Reg *funcs)
{
	say_debug("%s(%s)", __func__, modname);
	assert(modname != NULL);

	/* Get loaders.builtin. */
	lua_getfield(L, LUA_REGISTRYINDEX, "_TARANTOOL_BUILTIN");

	/* Verify that the module is not already registered. */
	lua_getfield(L, -1, modname);
	if (!lua_isnil(L, -1))
		panic("%s(%s): the module is already registered",
		      __func__, modname);
	lua_pop(L, 1);

	/* Create a module table. */
	lua_newtable(L);

	/* Fill the module table with functions. */
	if (funcs != NULL)
		luaL_setfuncs(L, funcs, 0);

	/* Copy the module table. */
	lua_pushvalue(L, -1);

	/* loaders.builtin[modname] = <module table> */
	lua_setfield(L, -3, modname);

	/* Stack: loaders.builtin, module table. */

	/*
	 * Drop loaders.builtin to leave the stack in its original
	 * state.
	 */
	lua_remove(L, -2);

	say_debug("%s(%s): success", __func__, modname);
	return 1;
}

int
luaT_setmodule(struct lua_State *L, const char *modname)
{
	assert(modname != NULL);
	say_debug("%s(%s)", __func__, modname);
	assert(lua_gettop(L) >= 1);

	if (lua_isnil(L, -1)) {
		say_debug("%s(%s): skip nil module value", __func__, modname);
		lua_pop(L, 1);
		return 0;
	}

	/* Get loaders.builtin[modname]. */
	lua_getfield(L, LUA_REGISTRYINDEX, "_TARANTOOL_BUILTIN");
	lua_getfield(L, -1, modname);

	/*
	 * If the same module value is already assigned, do
	 * nothing.
	 */
	if (lua_rawequal(L, -1, -3)) {
		say_debug("%s(%s): skip the same module value", __func__,
			  modname);
		lua_pop(L, 3);
		return 0;
	}

	/*
	 * Verify that the module is not already registered with
	 * another value.
	 */
	if (!lua_isnil(L, -1))
		panic("%s(%s): the module is already registered "
		      "with another value", __func__, modname);
	lua_pop(L, 1);

	/* Stack: module table, loaders.builtin. */

	/* loaders.builtin[modname] = <module table> */
	lua_pushvalue(L, -2);
	lua_setfield(L, -2, modname);

	/*
	 * Drop all the temporary values and the module table from
	 * the stack.
	 */
	lua_pop(L, 2);

	say_debug("%s(%s): success", __func__, modname);
	return 0;
}

const char *
luaL_tolstring_strict(struct lua_State *L, int idx, size_t *len_ptr)
{
	if (lua_type(L, idx) != LUA_TSTRING)
		return NULL;

	const char *res = lua_tolstring(L, idx, len_ptr);
	assert(res != NULL);
	return res;
}

bool
luaL_tointeger_strict(struct lua_State *L, int idx, int *value)
{
	if (lua_type(L, idx) != LUA_TNUMBER)
		return false;
	double num = lua_tonumber(L, idx);
	if (num < INT_MIN || num > INT_MAX)
		return false;
	*value = num;
	if (*value != num)
		return false;
	return true;
}

/*
 * Maximum integer that doesn't lose precision on tostring() conversion.
 * Lua uses sprintf("%.14g") to format its numbers, see gh-1279.
 */
#define DBL_INT_MAX (1e14 - 1)
#define DBL_INT_MIN (-1e14 + 1)

void
luaL_pushuint64(struct lua_State *L, uint64_t val)
{
#if defined(LJ_DUALNUM) /* see setint64V() */
	if (val <= INT32_MAX) {
		/* push int32_t */
		lua_pushinteger(L, (int32_t) val);
	} else
#endif /* defined(LJ_DUALNUM) */
	if (val <= DBL_INT_MAX) {
		/* push double */
		lua_pushnumber(L, (double) val);
	} else {
		/* push uint64_t */
		*(uint64_t *) luaL_pushcdata(L, CTID_UINT64) = val;
	}
}

void
luaL_pushint64(struct lua_State *L, int64_t val)
{
#if defined(LJ_DUALNUM) /* see setint64V() */
	if (val >= INT32_MIN && val <= INT32_MAX) {
		/* push int32_t */
		lua_pushinteger(L, (int32_t) val);
	} else
#endif /* defined(LJ_DUALNUM) */
	if (val >= DBL_INT_MIN && val <= DBL_INT_MAX) {
		/* push double */
		lua_pushnumber(L, (double) val);
	} else {
		/* push int64_t */
		*(int64_t *) luaL_pushcdata(L, CTID_INT64) = val;
	}
}

static inline int
luaL_convertint64(lua_State *L, int idx, bool unsignd, int64_t *result)
{
	uint32_t ctypeid;
	void *cdata;
	/*
	 * This code looks mostly like luaL_tofield(), but has less
	 * cases and optimized for numbers.
	 */
	switch (lua_type(L, idx)) {
	case LUA_TNUMBER:
		*result = lua_tonumber(L, idx);
		return 0;
	case LUA_TCDATA:
		cdata = luaL_checkcdata(L, idx, &ctypeid);
		switch (ctypeid) {
		case CTID_CCHAR:
		case CTID_INT8:
			*result = *(int8_t *) cdata;
			return 0;
		case CTID_INT16:
			*result = *(int16_t *) cdata;
			return 0;
		case CTID_INT32:
			*result = *(int32_t *) cdata;
			return 0;
		case CTID_INT64:
			*result = *(int64_t *) cdata;
			return 0;
		case CTID_UINT8:
			*result = *(uint8_t *) cdata;
			return 0;
		case CTID_UINT16:
			*result = *(uint16_t *) cdata;
			return 0;
		case CTID_UINT32:
			*result = *(uint32_t *) cdata;
			return 0;
		case CTID_UINT64:
			*result = *(uint64_t *) cdata;
			return 0;
		}
		*result = 0;
		return -1;
	case LUA_TSTRING:
	{
		const char *arg = luaL_checkstring(L, idx);
		char *arge;
		errno = 0;
		*result = (unsignd ? (long long) strtoull(arg, &arge, 10) :
			strtoll(arg, &arge, 10));
		if (errno == 0 && arge != arg)
			return 0;
		return 1;
	}
	}
	*result = 0;
	return -1;
}

uint64_t
luaL_checkuint64(struct lua_State *L, int idx)
{
	int64_t result;
	if (luaL_convertint64(L, idx, true, &result) != 0) {
		lua_pushfstring(L, "expected uint64_t as %d argument", idx);
		lua_error(L);
		return 0;
	}
	return result;
}

int64_t
luaL_checkint64(struct lua_State *L, int idx)
{
	int64_t result;
	if (luaL_convertint64(L, idx, false, &result) != 0) {
		lua_pushfstring(L, "expected int64_t as %d argument", idx);
		lua_error(L);
		return 0;
	}
	return result;
}

uint64_t
luaL_touint64(struct lua_State *L, int idx)
{
	int64_t result;
	if (luaL_convertint64(L, idx, true, &result) == 0)
		return result;
	return 0;
}

int64_t
luaL_toint64(struct lua_State *L, int idx)
{
	int64_t result;
	if (luaL_convertint64(L, idx, false, &result) == 0)
		return result;
	return 0;
}


int
luaT_toerror(lua_State *L)
{
	struct error *e = luaL_iserror(L, -1);
	if (e != NULL) {
		/* Re-throw original error */
		diag_set_error(&fiber()->diag, e);
	} else {
		/* Convert Lua error to a Tarantool exception. */
		diag_set(LuajitError, luaT_tolstring(L, -1, NULL));
	}
	return 1;
}

int
luaT_call(struct lua_State *L, int nargs, int nreturns)
{
	if (lua_pcall(L, nargs, nreturns, 0))
		return luaT_toerror(L);
	return 0;
}

int
luaT_dostring(struct lua_State *L, const char *str)
{
	int top = lua_gettop(L);
	if (luaL_loadstring(L, str) != 0) {
		diag_set(LuajitError, lua_tostring(L, -1));
		lua_settop(L, top);
		return -1;
	}
	if (luaT_call(L, 0, LUA_MULTRET) != 0) {
		lua_settop(L, top);
		return -1;
	}
	return 0;
}

int
luaT_cpcall(lua_State *L, lua_CFunction func, void *ud)
{
	if (lua_cpcall(L, func, ud))
		return luaT_toerror(L);
	return 0;
}

/**
 * This function exists because lua_tostring does not use
 * __tostring metamethod, and this metamethod has to be used
 * if we want to print Lua userdata correctly.
 */
const char *
luaT_tolstring(lua_State *L, int idx, size_t *len)
{
	if (!luaL_callmeta(L, idx, "__tostring")) {
		switch (lua_type(L, idx)) {
		case LUA_TNUMBER:
		case LUA_TSTRING:
			lua_pushvalue(L, idx);
			break;
		case LUA_TBOOLEAN: {
			int val = lua_toboolean(L, idx);
			lua_pushstring(L, val ? "true" : "false");
			break;
		}
		case LUA_TNIL:
			lua_pushliteral(L, "nil");
			break;
		default:
			lua_pushfstring(L, "%s: %p", luaL_typename(L, idx),
						     lua_topointer(L, idx));
		}
	}

	return lua_tolstring(L, -1, len);
}

/** Based on ffi_meta___call() from luajit/src/lib_ffi.c. */
static int
luaL_cdata_iscallable(lua_State *L, int idx)
{
	/* Get cdata from the stack. */
	CTypeID id;
	assert(luaL_iscdata(L, idx));
	luaL_tocpointer(L, idx, &id);

	CTState *cts = ctype_cts(L);
	CType *ct = ctype_raw(cts, id);
	if (ctype_isptr(ct->info))
		id = ctype_cid(ct->info);

	/* Get ctype metamethod. */
	cTValue *tv = lj_ctype_meta(cts, id, MM_call);

	return tv != NULL;
}

int
luaL_iscallable(lua_State *L, int idx)
{
	/* Whether it is function. */
	int res = lua_isfunction(L, idx);
	if (res == 1)
		return 1;

	/* Whether it is cdata with metatype with __call field. */
	if (lua_type(L, idx) == LUA_TCDATA)
		return luaL_cdata_iscallable(L, idx);

	/* Whether it has metatable with __call field. */
	res = luaL_getmetafield(L, idx, "__call");
	if (res == 1)
		lua_pop(L, 1); /* Pop __call value. */
	return res;
}

box_ibuf_t *
luaT_toibuf(struct lua_State *L, int idx)
{
	if (lua_type(L, idx) != LUA_TCDATA)
		return NULL;
	uint32_t cdata_type;
	void *cdata = luaL_checkcdata(L, idx, &cdata_type);
	if (cdata_type == CTID_STRUCT_IBUF)
		return (box_ibuf_t *) cdata;
	if (cdata_type == CTID_STRUCT_IBUF_PTR && cdata != NULL)
		return *(box_ibuf_t **) cdata;
	return NULL;
}

int
luaL_checkconstchar(struct lua_State *L, int idx, const char **res,
		    uint32_t *cdata_type_p)
{
	if (lua_type(L, idx) != LUA_TCDATA)
		return -1;
	uint32_t cdata_type;
	void *cdata = luaL_checkcdata(L, idx, &cdata_type);
	if (cdata_type != CTID_CHAR_PTR && cdata_type != CTID_CONST_CHAR_PTR)
		return -1;
	*res = cdata != NULL ? *(const char **) cdata : NULL;
	*cdata_type_p = cdata_type;
	return 0;
}

lua_State *
luaT_state(void)
{
	return tarantool_L;
}

/* {{{ Helper functions to interact with a Lua iterator from C */

struct luaL_iterator {
	int gen;
	int param;
	int state;
};

struct luaL_iterator *
luaL_iterator_new(lua_State *L, int idx)
{
	struct luaL_iterator *it = malloc(sizeof(struct luaL_iterator));
	if (it == NULL) {
		diag_set(OutOfMemory, sizeof(struct luaL_iterator),
			 "malloc", "luaL_iterator");
		return NULL;
	}

	if (idx == 0) {
		/* gen, param, state are on top of a Lua stack. */
		lua_pushvalue(L, -3); /* Popped by luaL_ref(). */
		it->gen = luaL_ref(L, LUA_REGISTRYINDEX);
		lua_pushvalue(L, -2); /* Popped by luaL_ref(). */
		it->param = luaL_ref(L, LUA_REGISTRYINDEX);
		lua_pushvalue(L, -1); /* Popped by luaL_ref(). */
		it->state = luaL_ref(L, LUA_REGISTRYINDEX);
	} else {
		/*
		 * {gen, param, state} table is at idx in a Lua
		 * stack.
		 */
		lua_rawgeti(L, idx, 1); /* Popped by luaL_ref(). */
		it->gen = luaL_ref(L, LUA_REGISTRYINDEX);
		lua_rawgeti(L, idx, 2); /* Popped by luaL_ref(). */
		it->param = luaL_ref(L, LUA_REGISTRYINDEX);
		lua_rawgeti(L, idx, 3); /* Popped by luaL_ref(). */
		it->state = luaL_ref(L, LUA_REGISTRYINDEX);
	}

	return it;
}

int
luaL_iterator_next(lua_State *L, struct luaL_iterator *it)
{
	int frame_start = lua_gettop(L);

	/* Call gen(param, state). */
	lua_rawgeti(L, LUA_REGISTRYINDEX, it->gen);
	lua_rawgeti(L, LUA_REGISTRYINDEX, it->param);
	lua_rawgeti(L, LUA_REGISTRYINDEX, it->state);
	if (luaT_call(L, 2, LUA_MULTRET) != 0) {
		/*
		 * Pop garbage from the call (a gen function
		 * likely will not leave the stack even when raise
		 * an error), pop a returned error.
		 */
		lua_settop(L, frame_start);
		return -1;
	}
	int nresults = lua_gettop(L) - frame_start;

	/*
	 * gen() function can either return nil when the iterator
	 * ends or return zero count of values.
	 *
	 * In LuaJIT pairs() returns nil, but ipairs() returns
	 * nothing when ends.
	 */
	if (nresults == 0 || lua_isnil(L, frame_start + 1)) {
		lua_settop(L, frame_start);
		return 0;
	}

	/* Save the first result to it->state. */
	luaL_unref(L, LUA_REGISTRYINDEX, it->state);
	lua_pushvalue(L, frame_start + 1); /* Popped by luaL_ref(). */
	it->state = luaL_ref(L, LUA_REGISTRYINDEX);

	return nresults;
}

void luaL_iterator_delete(struct luaL_iterator *it)
{
	luaL_unref(tarantool_L, LUA_REGISTRYINDEX, it->gen);
	luaL_unref(tarantool_L, LUA_REGISTRYINDEX, it->param);
	luaL_unref(tarantool_L, LUA_REGISTRYINDEX, it->state);
	free(it);
}

/* }}} */

/**
 * @brief A wrapper for <lua_newthread> to be called via luaT_call
 * in luaT_newthread. Whether new Lua coroutine is created it is
 * returned on the top of the guest stack.
 * @param L is a Lua state
 * @sa <lua_newthread>
 */
static int
luaT_newthread_wrapper(struct lua_State *L)
{
	(void)lua_newthread(L);
	return 1;
}

struct lua_State *
luaT_newthread(struct lua_State *L)
{
	assert(luaT_newthread_ref != LUA_NOREF);
	lua_rawgeti(L, LUA_REGISTRYINDEX, luaT_newthread_ref);
	assert(lua_isfunction(L, -1));
	if (luaT_call(L, 0, 1) != 0)
		return NULL;
	struct lua_State *L1 = lua_tothread(L, -1);
	assert(L1 != NULL);
	return L1;
}

/**
 * A safer os.getenv() variant. The difference is that it returns the value only
 * if it has sane length (see src/lib/core/utils.c for details).
 */
static int
luaT_getenv(struct lua_State *L)
{
	const char *name = lua_tostring(L, 1);
	if (name == NULL)
		return luaL_error(L, "usage: os.getenv(name)");
	char *envvar = getenv_safe(name, NULL, 0);
	if (envvar != NULL) {
		lua_pushstring(L, envvar);
		free(envvar);
	} else {
		/*
		 * Original getenv pushes nil onto the stack when no value is
		 * found.
		 */
		lua_pushnil(L);
	}
	return 1;
}

int
tarantool_lua_utils_init(struct lua_State *L)
{
	/* Create NULL constant */
	*(void **) luaL_pushcdata(L, CTID_P_VOID) = NULL;
	luaL_nil_ref = luaL_ref(L, LUA_REGISTRYINDEX);

	int rc = luaL_cdef(L, "struct ibuf;");
	assert(rc == 0);
	CTID_STRUCT_IBUF = luaL_ctypeid(L, "struct ibuf");
	assert(CTID_STRUCT_IBUF != 0);
	CTID_STRUCT_IBUF_PTR = luaL_ctypeid(L, "struct ibuf *");
	assert(CTID_STRUCT_IBUF_PTR != 0);
	CTID_CHAR_PTR = luaL_ctypeid(L, "char *");
	assert(CTID_CHAR_PTR != 0);
	CTID_CONST_CHAR_PTR = luaL_ctypeid(L, "const char *");
	assert(CTID_CONST_CHAR_PTR != 0);
	rc = luaL_cdef(L, "struct varbinary { char data[?]; };");
	assert(rc == 0);
	CTID_VARBINARY = luaL_ctypeid(L, "struct varbinary");
	assert(CTID_VARBINARY != 0);
	rc = luaL_cdef(L, "struct tt_uuid {"
				  "uint32_t time_low;"
				  "uint16_t time_mid;"
				  "uint16_t time_hi_and_version;"
				  "uint8_t clock_seq_hi_and_reserved;"
				  "uint8_t clock_seq_low;"
				  "uint8_t node[6];"
			  "};");
	assert(rc == 0);
	(void) rc;
	CTID_UUID = luaL_ctypeid(L, "struct tt_uuid");
	assert(CTID_UUID != 0);

	rc = luaL_cdef(L, "struct datetime {"
				  "double epoch;"
				  "int32_t nsec;"
				  "int16_t tzoffset;"
				  "int16_t tzindex;"
			  "};");
	assert(rc == 0);
	(void) rc;
	CTID_DATETIME = luaL_ctypeid(L, "struct datetime");
	assert(CTID_DATETIME != 0);

	rc = luaL_cdef(L, "typedef enum {"
				  "DT_EXCESS,"
				  "DT_LIMIT,"
				  "DT_SNAP"
			  "} dt_adjust_t;");
	assert(rc == 0);
	(void)rc;

	rc = luaL_cdef(L, "struct interval {"
				  "double sec;"
				  "double min;"
				  "double hour;"
				  "double day;"
				  "int32_t week;"
				  "int32_t month;"
				  "int32_t year;"
				  "int32_t nsec;"
				  "dt_adjust_t adjust;"
			  "};");
	assert(rc == 0);
	(void)rc;
	CTID_INTERVAL = luaL_ctypeid(L, "struct interval");
	assert(CTID_INTERVAL != 0);

	/* Overload os.getenv() with our safe variant. */
	lua_getglobal(L, "os");
	lua_pushcfunction(L, luaT_getenv);
	lua_setfield(L, -2, "getenv");
	lua_pop(L, 1);

	lua_pushcfunction(L, luaT_newthread_wrapper);
	luaT_newthread_ref = luaL_ref(L, LUA_REGISTRYINDEX);
	return 0;
}

/*
 * XXX: There is already defined <panic> macro in say.h header
 * (included in diag.h). As a result the call below is misexpanded
 * and compilation fails with the corresponding error. To avoid
 * this error the macro is undefined since it's not used anymore
 * in scope of this translation unit.
 */
#undef panic

/**
 * This routine encloses the checks and actions to be done when
 * the running fiber yields the execution.
 * Since Tarantool fibers don't switch-over the way Lua coroutines
 * do the platform ought to notify JIT engine when one lua_State
 * substitutes another one. Furthermore fiber switch is forbidden
 * when GC hook (i.e. __gc metamethod) is running.
 */
void cord_on_yield(void)
{
	struct global_State *g = G(tarantool_L);
	/*
	 * XXX: Switching fibers while running the trace leads to
	 * code misbehaviour and failures, so stop its execution.
	 */
	if (unlikely(tvref(g->jit_base))) {
		char buf[256];
		/*
		 * XXX: mcode is executed only in scope of Lua
		 * world and one can obtain the corresponding Lua
		 * coroutine from the fiber storage.
		 */
		struct lua_State *L = gco2th(gcref(g->cur_L));
		assert(L != NULL);
		snprintf(buf, sizeof(buf),
			 "fiber %llu is switched while running the"
			 " compiled code (it's likely a function with"
			 " a yield underneath called via LuaJIT FFI)",
			 (long long)fiber()->fid);
		lua_pushstring(L, buf);
		if (g->panic)
			g->panic(L);
		exit(EXIT_FAILURE);
	}
	/*
	 * Unconditionally abort trace recording whether fibers
	 * switch each other. Otherwise, further compilation may
	 * lead to a failure on any next compiler phase.
	 */
	lj_trace_abort(g);

	/*
	 * XXX: While running GC hook (i.e. __gc  metamethod)
	 * garbage collector is formally "stopped" since the
	 * memory penalty threshold is set to its maximum value,
	 * ergo incremental GC step is not triggered. Thereby,
	 * yielding the execution at this point leads to further
	 * running platform with disabled LuaJIT GC. The fiber
	 * doesn't get the execution back until it's ready, so
	 * in pessimistic scenario LuaJIT OOM might occur
	 * earlier. As a result fiber switch is prohibited when
	 * GC hook is active and the platform is forced to stop.
	 */
	if (unlikely(g->hookmask & HOOK_GC)) {
		char buf[128];
		struct lua_State *L = gco2th(gcref(g->cur_L));
		assert(L != NULL);
		snprintf(buf, sizeof(buf),
			 "fiber %llu is switched while running GC"
			 " finalizer (i.e. __gc metamethod)",
			 (long long)fiber()->fid);
		lua_pushstring(L, buf);
		if (g->panic)
			g->panic(L);
		exit(EXIT_FAILURE);
	}
}
