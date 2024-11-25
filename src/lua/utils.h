#ifndef TARANTOOL_LUA_UTILS_H_INCLUDED
#define TARANTOOL_LUA_UTILS_H_INCLUDED
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

#include <stdint.h>
#include <string.h>
#include <math.h> /* floor */

#include <msgpuck.h> /* enum mp_type */

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

#include <lua.h>
#include <lauxlib.h> /* luaL_error */

#include <lj_state.h>
#include <lj_obj.h>
#include <lj_ctype.h>
#include <lj_cdata.h>
#include <lj_cconv.h>
#include <lj_lib.h>
#include <lj_tab.h>
#include <lj_meta.h>

#include "lua/error.h"
#include "error.h"
#include "diag.h"

struct lua_State;
struct ibuf;
typedef struct ibuf box_ibuf_t;
struct tt_uuid;
struct datetime;
struct vclock;

/**
 * Single global lua_State shared by core and modules.
 * Created with tarantool_lua_init().
 * const char *msg = lua_tostring(L, -1);
 * snprintf(m_errmsg, sizeof(m_errmsg), "%s", msg ? msg : "");
 */
extern struct lua_State *tarantool_L;

extern uint32_t CTID_CHAR_PTR;
extern uint32_t CTID_CONST_CHAR_PTR;
/** Type ID of struct varbinary. */
extern uint32_t CTID_VARBINARY;
extern uint32_t CTID_UUID;
extern uint32_t CTID_DATETIME;
/** Type ID of struct interval. */
extern uint32_t CTID_INTERVAL;

/**
 * Creates a new Lua state with a custom allocator function.
 * Guarantees the state is not NULL. It panics if lua_State can't
 * be allocated.
 */
struct lua_State *
luaT_newstate(void);

/**
 * Pushes a new varbinary object with the given content to the Lua stack.
 */
void
luaT_pushvarbinary(struct lua_State *L, const char *data, uint32_t len);

/**
 * If the value stored in the Lua stack at the given index is a varbinary
 * object, returns its content, otherwise returns NULL.
 */
const char *
luaT_tovarbinary(struct lua_State *L, int index, uint32_t *len);

/**
 * Push vclock to the Lua stack as a plain Lua table.
 */
void
luaT_pushvclock(struct lua_State *L, const struct vclock *vclock);

/**
 * Allocate a new uuid on the Lua stack and return a pointer to it.
 */
struct tt_uuid *
luaT_newuuid(struct lua_State *L);

/**
 * Allocate a new uuid on the Lua stack with copy of given
 * uuid and return a pointer to it.
 */
struct tt_uuid *
luaT_pushuuid(struct lua_State *L, const struct tt_uuid *uuid);

void
luaT_pushuuidstr(struct lua_State *L, const struct tt_uuid *uuid);

/**
 * @brief Push cdata of a datetime type onto the stack.
 * @param L Lua State
 * @sa luaL_pushcdata
 * @return memory associated with this datetime data
 */
struct datetime *
luaT_newdatetime(struct lua_State *L);

/**
 * @brief Push cdata of a datetime type onto the stack and
 * copy given datetime value into it.
 * @param L Lua State
 * @param dt datetime value to copy from
 * @sa luaL_pushcdata
 * @return memory associated with this datetime data
 */
struct datetime *
luaT_pushdatetime(struct lua_State *L, const struct datetime *dt);

/**
 * Allocate a new time interval on the Lua stack and return
 * a pointer to it.
 */
struct interval *
luaT_newinterval(struct lua_State *L);

/**
 * Allocate a new time interval on the Lua stack with copy of
 * given interval and return a pointer to it.
 */
struct interval *
luaT_pushinterval(struct lua_State *L, const struct interval *itv);

/**
 * Returns a pointer to the cdata payload.
 *
 * @param L Lua state.
 * @param idx Acceptable index on the Lua stack.
 * @param[out] ctypeid FFI's CTypeID of this cdata.
 *
 * @retval Pointer to the memory associated with this cdata,
 * or NULL if the value at the given index is not a cdata.
 */
void *
luaL_tocpointer(lua_State *L, int idx, uint32_t *ctypeid);

/** \cond public */

/**
 * Checks whether a value on the Lua stack is a cdata.
 *
 * Unlike luaL_checkcdata() this function does not raise an
 * error. It is useful to raise a domain specific error.
 *
 * Lua API and module API don't expose LUA_TCDATA constant.
 * We have no guarantee that this constant will remain the same in
 * future LuaJIT versions. So this function should be used in
 * modules instead of `lua_type(L, idx) == LUA_TCDATA`.
 *
 * @param L    Lua state.
 * @param idx  Acceptable index on the Lua stack.
 *
 * @retval 1   If the value at the given index is a cdata.
 * @retval 0   Otherwise.
 */
LUA_API int
luaL_iscdata(struct lua_State *L, int idx);

/**
 * @brief Push cdata of given \a ctypeid onto the stack.
 * CTypeID must be used from FFI at least once. Allocated memory returned
 * uninitialized. Only numbers and pointers are supported.
 * @param L Lua State
 * @param ctypeid FFI's CTypeID of this cdata
 * @sa luaL_checkcdata
 * @return memory associated with this cdata
 */
LUA_API void *
luaL_pushcdata(struct lua_State *L, uint32_t ctypeid);

/**
 * @brief Checks whether the function argument idx is a cdata
 * @param L Lua State
 * @param idx stack index
 * @param ctypeid FFI's CTypeID of this cdata
 * @sa luaL_pushcdata
 * @return memory associated with this cdata
 */
LUA_API void *
luaL_checkcdata(struct lua_State *L, int idx, uint32_t *ctypeid);

/**
 * @brief Sets finalizer function on a cdata object.
 * Equivalent to call ffi.gc(obj, function).
 * Finalizer function must be on the top of the stack.
 * @param L Lua State
 * @param idx object
 */
LUA_API void
luaL_setcdatagc(struct lua_State *L, int idx);

/**
 * @brief Return size of currently allocated memory.
 * @param L Lua State
 */
size_t
luaL_getgctotal(struct lua_State *L);

/**
* @brief Return CTypeID (FFI) of given СDATA type
* @param L Lua State
* @param ctypename С type name as string (e.g. "struct request" or "uint32_t")
* @sa luaL_pushcdata
* @sa luaL_checkcdata
* @return CTypeID
*/
LUA_API uint32_t
luaL_ctypeid(struct lua_State *L, const char *ctypename);

/**
* @brief Declare symbols for FFI
* @param L Lua State
* @param ctypename C definitions, e.g "struct stat"
* @sa ffi.cdef(def)
* @retval 0 on success
* @retval LUA_ERRRUN, LUA_ERRMEM, LUA_ERRERR otherwise
*/
LUA_API int
luaL_cdef(struct lua_State *L, const char *ctypename);

/** \endcond public */

/**
 * @brief Return CTypeID (FFI) of given CDATA type,
 * register a metatable with \a methods to be
 * associated with every value of the given
 * type on its creation iva FFI.
 * @sa luaL_register_type
 * @sa luaL_ctypeid
 * @return CTypeID
 */
uint32_t
luaL_metatype(struct lua_State *L, const char *ctypename,
	      const struct luaL_Reg *methods);

static inline lua_Integer
luaL_arrlen(struct lua_State *L, int idx)
{
	lua_Integer max = 0;
	lua_pushnil(L);
	while (lua_next(L, idx)) {
		lua_pop(L, 1); /* pop the value */
		if (lua_type(L, -1) != LUA_TNUMBER)
			continue;
		lua_Number k = lua_tonumber(L, -1);
		if (k <= max || floor(k) != k)
			continue;
		max = k;
	}
	return max;
}

static inline lua_Integer
luaL_maplen(struct lua_State *L, int idx)
{
	lua_Integer size = 0;
	lua_pushnil(L);
	while (lua_next(L, idx)) {
		lua_pop(L, 1); /* pop the value */
		size++;
	}
	return size;
}

extern int luaL_nil_ref;

void
luaL_register_type(struct lua_State *L, const char *type_name,
		   const struct luaL_Reg *methods);

/**
 * Create a table with functions and register it as a built-in
 * tarantool module.
 *
 * Panic if the module is already registered.
 *
 * Leave the table on top of the stack.
 *
 * Pseudocode:
 *
 *  | local function newmodule(modname, funcs)
 *  |     assert(modname ~= nil and funcs ~= nil)
 *  |     assert(loaders.builtin[modname] == nil)
 *  |     local mod = {}
 *  |     setfuncs(mod, funcs)
 *  |     loaders.builtin[modname] = mod
 *  |     return mod
 *  | end
 *
 * Unlike luaL_register() it is very straightforward: no recursive
 * search, no _G pollution, no branching around using a stack
 * top/find a table/create a new table.
 */
int
luaT_newmodule(struct lua_State *L, const char *modname,
	       const struct luaL_Reg *funcs);

/**
 * Register a table on top of the stack as a built-in tarantool
 * module.
 *
 * Can be called several times with the same value, but panics
 * if called with different values.
 *
 * Can be used after luaT_newmodule() if, again, the table of the
 * module is the same.
 *
 * Pops the table.
 *
 * Pseudocode:
 *
 *  | local function setmodule(modname, mod)
 *  |     assert(modname ~= nil)
 *  |     if mod == nil then
 *  |         return
 *  |     end
 *  |     if loaders.builtin[modname] == mod then
 *  |         return
 *  |     end
 *  |     assert(loaders.builtin[modname] == nil)
 *  |     loaders.builtin[modname] = mod
 *  | end
 */
int
luaT_setmodule(struct lua_State *L, const char *modname);

/**
 * Extract a string from the Lua stack.
 *
 * Return (const char *) for a string, otherwise return NULL.
 *
 * Unlike luaL_tolstring() it accepts only a string and does not
 * accept a number.
 */
const char *
luaL_tolstring_strict(struct lua_State *L, int idx, size_t *len_ptr);

/**
 * Extract an integer number from the Lua stack.
 *
 * Return true for an integer number and store its value in @a value.
 *
 * Unlike lua_tointeger() it accepts only an integer number and
 * does not accept a string.
 */
bool
luaL_tointeger_strict(struct lua_State *L, int idx, int *value);

/** \cond public */

/**
 * Push uint64_t onto the stack.
 *
 * @param L is a Lua State
 * @param val is a value to push
 */
LUA_API void
luaL_pushuint64(struct lua_State *L, uint64_t val);

/**
 * Push int64_t onto the stack.
 *
 * @param L is a Lua State
 * @param val is a value to push
 */
LUA_API void
luaL_pushint64(struct lua_State *L, int64_t val);

/**
 * Checks whether the argument idx is a uint64 or a convertible string and
 * returns this number.
 * \throws error if the argument can't be converted.
 */
LUA_API uint64_t
luaL_checkuint64(struct lua_State *L, int idx);

/**
 * Checks whether the argument idx is a int64 or a convertible string and
 * returns this number.
 * \throws error if the argument can't be converted.
 */
LUA_API int64_t
luaL_checkint64(struct lua_State *L, int idx);

/**
 * Checks whether the argument idx is a uint64 or a convertible string and
 * returns this number.
 * \return the converted number or 0 of argument can't be converted.
 */
LUA_API uint64_t
luaL_touint64(struct lua_State *L, int idx);

/**
 * Checks whether the argument idx is a int64 or a convertible string and
 * returns this number.
 * \return the converted number or 0 of argument can't be converted.
 */
LUA_API int64_t
luaL_toint64(struct lua_State *L, int idx);

/**
 * Like lua_call(), but with the proper support of Tarantool errors.
 * \sa lua_call()
 */
LUA_API int
luaT_call(lua_State *L, int nargs, int nreturns);

/**
 * Like luaL_dostring(), but in case of error sets fiber diag instead
 * of putting error on stack.
 *
 * @param L Lua state
 * @param str string with Lua code to load and run
 */
int
luaT_dostring(struct lua_State *L, const char *str);

/**
 * Like lua_cpcall(), but with the proper support of Tarantool errors.
 * \sa lua_cpcall()
 */
LUA_API int
luaT_cpcall(lua_State *L, lua_CFunction func, void *ud);

/**
 * Get global Lua state used by Tarantool
 */
LUA_API lua_State *
luaT_state(void);

/**
 * Like lua_tolstring, but supports metatables, booleans and nil properly.
 */
LUA_API const char *
luaT_tolstring(lua_State *L, int idx, size_t *ssize);

/**
 * Check whether a Lua object is a function or has
 * metatable/metatype with a __call field.
 *
 * Note: It does not check type of __call metatable/metatype
 * field.
 */
LUA_API int
luaL_iscallable(lua_State *L, int idx);

/**
 * Check if a value on @a L stack by index @a idx is an ibuf
 * object. Both 'struct ibuf' and 'struct ibuf *' are accepted.
 * Returns NULL, if can't convert - not an ibuf object.
 */
LUA_API box_ibuf_t *
luaT_toibuf(struct lua_State *L, int idx);

/**
 * Push ffi's NULL (cdata<void *>: NULL) onto the stack.
 * Can be used as replacement of nil in Lua tables.
 * @param L stack
 */
LUA_API void
luaL_pushnull(struct lua_State *L);

/**
 * Return true if the value at Lua stack is ffi's NULL (cdata<void *>: NULL).
 * @param L stack
 * @param idx stack index
 */
LUA_API bool
luaL_isnull(struct lua_State *L, int idx);

/** \endcond public */

/**
 * Convert the last value on the stack into Tarantool error and
 * set diagnostics.
 */
int
luaT_toerror(lua_State *L);

/**
 * @brief Creates a new Lua coroutine in a protected frame. If
 * <lua_newthread> call underneath succeeds, the created Lua state
 * is on the top of the guest stack and a pointer to this state is
 * returned. Otherwise LUA_ERRMEM error is handled and the result
 * is NULL.
 * @param L is a Lua state
 * @sa <lua_newthread>
 */
struct lua_State *
luaT_newthread(struct lua_State *L);

/**
 * Check if a value on @a L stack by index @a idx is pointer at
 * char or const char. '(char *)NULL' is also considered a valid
 * char pointer.
 */
int
luaL_checkconstchar(struct lua_State *L, int idx, const char **res,
		    uint32_t *cdata_type_p);

/**
 * Whether the object at the given valid index is in the table at
 * the given valid index.
 */
bool
luaT_hasfield(struct lua_State *L, int obj_index, int table_index);

/* {{{ Helper functions to interact with a Lua iterator from C */

/**
 * Holds iterator state (references to Lua objects).
 */
struct luaL_iterator;

/**
 * Create a Lua iterator from a gen, param, state triplet.
 *
 * If idx == 0, then three top stack values are used as the
 * triplet. Note: they are not popped.
 *
 * Otherwise idx is index on Lua stack points to a
 * {gen, param, state} table.
 */
struct luaL_iterator *
luaL_iterator_new(lua_State *L, int idx);

/**
 * Move iterator to the next value. Push values returned by
 * gen(param, state).
 *
 * Return count of pushed values. Zero means no more results
 * available. In case of a Lua error in a gen function return -1
 * and set a diag.
 */
int
luaL_iterator_next(lua_State *L, struct luaL_iterator *it);

/**
 * Free all resources held by the iterator.
 */
void luaL_iterator_delete(struct luaL_iterator *it);

/* }}} */

int
tarantool_lua_utils_init(struct lua_State *L);

/** Same as luaL_checkstring but raises box error. **/
const char *
luaT_checkstring(struct lua_State *L, int index);

/** Same as luaL_checklstring but raises box error. **/
const char *
luaT_checklstring(struct lua_State *L, int index, size_t *len);

/** Same as luaL_checkint but raises box error. **/
int
luaT_checkint(struct lua_State *L, int index);

/** Same as luaL_checknumber but raises box error. **/
double
luaT_checknumber(struct lua_State *L, int index);

/** Same as luaL_checkudata but raises box error. **/
void *
luaT_checkudata(struct lua_State *L, int index, const char *name);

/** Same as luaL_checktype but raises box error. **/
void
luaT_checktype(struct lua_State *L, int index, int expected);

/** Same as luaL_checkint64 but raises box error. **/
int64_t
luaT_checkint64(struct lua_State *L, int idx);

/** Same as luaL_optint but raises box error. **/
int
luaT_optint(struct lua_State *L, int index, int deflt);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_LUA_UTILS_H_INCLUDED */
