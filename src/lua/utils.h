#ifndef TARANTOOL_LUA_UTILS_H_INCLUDED
#define TARANTOOL_LUA_UTILS_H_INCLUDED
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

#include <stdint.h>

#include "msgpuck/msgpuck.h" /* enum mp_type */

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

#include <lua.h>
#include <lauxlib.h> /* luaL_error */

/* TODO: add autodetection */
#if !defined(LUAJIT)
#define LUAJIT 1
#endif /* defined(LUAJIT) */

struct lua_State;

#if defined(LUAJIT)

/**
 * @brief Allocate a new block of memory with the given size, push onto the
 * stack a new cdata of type ctypeid with the block address, and return
 * this address. Allocated memory is a subject of GC.
 * CTypeID must be used from FFI at least once.
 * @param L Lua State
 * @param ctypeid FFI's CTypeID of this cdata
 * @param size size to allocate
 * @sa luaL_checkcdata
 * @return memory associated with this cdata
 */
void *
luaL_pushcdata(struct lua_State *L, uint32_t ctypeid, uint32_t size);

/**
 * @brief Checks whether the function argument idx is a cdata
 * @param L Lua State
 * @param idx stack index
 * @param ctypeid FFI's CTypeID of this cdata
 * @sa luaL_pushcdata
 * @return memory associated with this cdata
 */
void *
luaL_checkcdata(struct lua_State *L, int idx, uint32_t *ctypeid);

#endif /* defined(LUAJIT) */

/** A single value on the Lua stack. */
struct luaL_field {
	union {
		struct {
			const char *data;
			uint32_t len;
		} sval;
		int64_t ival;
		double dval;
		float fval;
		bool bval;
		/* Array or map. */
		uint32_t size;
	};
	enum mp_type type;
	bool compact;                /* a flag used by YAML serializer */
};

/**
 * @brief Convert a value from the Lua stack to a lua_field structure.
 * This function is designed for use with Lua bindings and data
 * serialization functions (YAML, MsgPack, JSON, etc.).
 *
 * Conversion rules:
 * - LUA_TNUMBER when is integer and >= 0 -> UINT
 * - LUA_TNUMBER when is integer and < 0 -> INT
 * - LUA_TNUMBER when is not integer -> DOUBLE
 * - LUA_TBOOLEAN -> BOOL
 * - LUA_TSTRING -> STRING
 * - LUA_TNIL -> NIL
 * - LUA_TTABLE when is array -> ARRAY
 * - LUA_TTABLE when is not array -> MAP
 * - LUA_TUSERDATA, LUA_TLIGHTUSERDATA, CTID_P_VOID when == NULL -> NIL
 * - CTID_INT*, CTID_CCHAR when >= 0 -> UINT
 * - CTID_INT*, CTID_CCHAR when < 0 -> INT
 * - CTID_FLOAT -> FLOAT
 * - CTID_DOUBLE -> DOUBLE
 * - CTID_BOOL -> BOOL
 * - otherwise -> EXT
 *
 * By default all LUA_TTABLEs (including empty ones) are handled as ARRAYs.
 * If a table has other than numeric indexes or is too
 * sparse, then it is handled as a map. The user can force MAP or
 *
 * YAML also supports a special "_serializer_compact" = true | false
 * flag that controls block vs flow output ([1,2,3] vs - 1\n - 2\n - 3\n).
 * It can be set in the table's metatable.
 *
 * MAP and ARRAY members are not saved to lua_field structure and should be
 * processed manually.
 *
 * @param L stack
 * @param index stack index
 * @param field conversion result
 */
void
luaL_tofield(struct lua_State *L, int index, struct luaL_field *field);

/**
 * @brief A wrapper for luaL_tofield that raises an error if Lua type can not
 * be converted to luaL_field structure
 * @param L stack
 * @param index stack index
 * @param field conversion result
 * @sa lua_tofield()
 */
static inline void
luaL_checkfield(lua_State *L, int i, struct luaL_field *field)
{
	luaL_tofield(L, i, field);
	if (field->type == MP_EXT)
		luaL_error(L, "unsupported Lua type '%s'",
			   lua_typename(L, lua_type(L, i)));
}

void
luaL_register_type(struct lua_State *L, const char *type_name,
		   const struct luaL_Reg *methods);

/**
 * Convert Lua string, number or cdata (u64) to 64bit value
 */
uint64_t
luaL_tointeger64(struct lua_State *L, int idx);

/**
 * push uint64_t to Lua stack
 *
 * @param L is a Lua State
 * @param val is a value to push
 *
 */
int luaL_pushnumber64(struct lua_State *L, uint64_t val);

#if defined(__cplusplus)
} /* extern "C" */

#include "exception.h"

/**
 * A wrapper around lua_call() which converts Lua error(...)
 * to ER_PROC_LUA
 */
static inline void
lbox_call(struct lua_State *L, int nargs, int nreturns)
{
	try {
		lua_call(L, nargs, nreturns);
	} catch (Exception *e) {
		/* Let all well-behaved exceptions pass through. */
		throw;
	} catch (...) {
		/* Convert Lua error to a Tarantool exception. */
		const char *msg = lua_tostring(L, -1);
		tnt_raise(ClientError, ER_PROC_LUA, msg ? msg : "");
	}
}

/**
 * Single global lua_State shared by core and modules.
 * Created with tarantool_lua_init().
 */
extern struct lua_State *tarantool_L;

/**
 * Make a reference to an object on top of the Lua stack and
 * release it at the end of the scope.
 */
struct LuarefGuard
{
	int ref;
	bool is_active;

	explicit LuarefGuard(int ref_arg) { ref = ref_arg; is_active = true; }
	explicit LuarefGuard(struct lua_State *L) { ref = luaL_ref(L, LUA_REGISTRYINDEX); is_active = true; }
	~LuarefGuard() { if (is_active) luaL_unref(tarantool_L, LUA_REGISTRYINDEX, ref); }
};

#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_LUA_UTILS_H_INCLUDED */
