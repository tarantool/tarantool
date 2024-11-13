#pragma once
/*
 * Copyright 2010-2021, Tarantool AUTHORS, please see AUTHORS file.
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

/**
 * A set of helpers for converting Lua values into another
 * representation.
 *
 * <struct luaL_serializer> is the serializer object: options and
 * options inheritance.
 *
 * <struct luaL_field> is a Lua value descriptor, which
 * characterizes the value.
 */

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

#include <assert.h>
#include <stdbool.h>
#include <math.h> /* isfinite */
#include <lua.h>
#include <lauxlib.h>

#include "trigger.h"
#include "lib/core/datetime.h"
#include "lib/core/decimal.h" /* decimal_t */
#include "lib/core/mp_extension_types.h"
#include "lua/error.h"

struct lua_State;
struct tt_uuid;
struct interval;

#define LUAL_SERIALIZER "serializer"
#define LUAL_SERIALIZE "__serialize"

extern int luaL_map_metatable_ref;
extern int luaL_array_metatable_ref;

/* {{{ luaL_serializer manipulations */

/**
 * Common configuration options for Lua serializers (MsgPack, YAML, JSON)
 */
struct luaL_serializer {
	/**
	 * luaL_tofield tries to classify table into one of four kinds
	 * during encoding:
	 *
	 *  + map - at least one table index is not unsigned integer.
	 *  + regular array - all array indexes are available.
	 *  + sparse array - at least one array index is missing.
	 *  + excessively sparse arrat - the number of values missing
	 * exceeds the configured ratio.
	 *
	 * An array is excessively sparse when **all** the following
	 * conditions are met:
	 *
	 *  + encode_sparse_ratio > 0.
	 *  + max(table) > encode_sparse_safe.
	 *  + max(table) > count(table) * encode_sparse_ratio.
	 *
	 * luaL_tofield will never consider an array to be excessively sparse
	 * when encode_sparse_ratio = 0. The encode_sparse_safe limit ensures
	 * that small Lua arrays are always encoded as sparse arrays.
	 * By default, attempting to encode an excessively sparse array will
	 * generate an error. If encode_sparse_convert is set to true,
	 * excessively sparse arrays will be handled as maps.
	 *
	 * This conversion logic is modeled after Mark Pulford's CJSON module.
	 * @sa http://www.kyne.com.au/~mark/software/lua-cjson-manual.html
	 */
	int encode_sparse_convert;
	/** @see encode_sparse_convert */
	int encode_sparse_ratio;
	/** @see encode_sparse_convert */
	int encode_sparse_safe;
	/** Max recursion depth for encoding (MsgPack, CJSON only) */
	int encode_max_depth;
	/**
	 * A flag whether a table with too high nest level should
	 * be cropped. The not-encoded fields are replaced with
	 * one null. If not set, too high nesting is considered an
	 * error.
	 */
	int encode_deep_as_nil;
	/** Enables encoding of NaN and Inf numbers */
	int encode_invalid_numbers;
	/** Floating point numbers precision (YAML, CJSON only) */
	int encode_number_precision;

	/**
	 * Enables __serialize meta-value checking:
	 *
	 *  + 'seq', 'sequence', 'array' - table encoded as an array
	 *  + 'map', 'mappping' - table encoded as a map.
	 *    'seq' or 'map' also enable flow (compact) mode for YAML serializer
	 *    (flow="[1,2,3]" vs block=" - 1\n - 2\n - 3\n").
	 *  + function - the meta-method is called to unpack serializable
	 *    representation of table, cdata or userdata objects.
	 */
	int encode_load_metatables;
	/** Enables tostring() usage for unknown types */
	int encode_use_tostring;
	/** Use NULL for all unrecognizable types */
	int encode_invalid_as_nil;
	/** Encode error object as MP_ERROR extension (MsgPack only). */
	int encode_error_as_ext;
	/** Array of keys to sort map serialization. */
	char **encode_key_order;

	/** Enables decoding NaN and Inf numbers */
	int decode_invalid_numbers;
	/** Save __serialize meta-value for decoded arrays and maps */
	int decode_save_metatables;
	/** Max recursion depts for decoding (CJSON only) */
	int decode_max_depth;

	/** Enable support for compact represenation (internal, YAML-only). */
	int has_compact;
	/**
	 * Border where copyable fields end. Is used to copy
	 * serializer options into an existing serializer without
	 * erasure of its non-option fields like triggers.
	 */
	char end_of_options[0];
	/**
	 * Trigger object to subscribe on updates of a more
	 * general serializer. For example, tuple serializer
	 * subscribes on msgpack.
	 */
	struct trigger update_trigger;
	/**
	 * List of triggers on update of this serializer. To push
	 * updates down to dependent serializers.
	 */
	struct rlist on_update;
};

/**
 * @brief Create a new serializer configuration and push it onto the Lua stack.
 * @param Lua stack.
 * @return New serializer configuration.
 */
struct luaL_serializer *
luaL_newserializer_config(struct lua_State *L);

/**
 * @brief serializer.new() Lua binding.
 * @param L stack
 * @param reg methods to register
 * @param parent parent serializer to inherit configuration
 * @return new serializer
 */
struct luaL_serializer *
luaL_newserializer(struct lua_State *L, const char *modname,
		   const luaL_Reg *reg);

/**
 * @brief Copy all option fields of @a src into @a dst.
 * Other fields, such as triggers, are not touched.
 *
 * This will try to free memory allocated for the options in @a dst. If @a dst
 * has been just allocated, it should be initialized with 0s.
 *
 * This may allocate memory on heap for some of the options, your @a dst
 * is created in C code, don't forget to call luaL_serializer_options_delete
 * when the serializer object is not needed anymore to prevent memory leaks.
 * Or you can use luaL_newserializer_config function to create a new serializer
 * configuration on Lua stack. It will be garbage collected automatically.
 *
 * If the serializer object was created in lua, this will be done during garbage
 * collection automatically.
 *
 * @param dst Destination serializer to copy options into.
 * @param src Source serializer to copy options from.
 */
void
luaL_serializer_copy_options(struct luaL_serializer *dst,
			     const struct luaL_serializer *src);

/**
 * Delete the serializer options and free the memory allocated for them.
 * @param cfg Serializer.
 */
void
luaL_serializer_options_delete(struct luaL_serializer *cfg);

static inline struct luaL_serializer *
luaL_checkserializer(struct lua_State *L)
{
	return (struct luaL_serializer *)
		luaL_checkudata(L, lua_upvalueindex(1), LUAL_SERIALIZER);
}

static inline void
luaL_pushserializer(struct lua_State *L)
{
	lua_pushvalue(L, lua_upvalueindex(1));
}

/**
 * Initialize serializer with default parameters.
 * @param cfg Serializer to inherit configuration.
 */
void
luaL_serializer_create(struct luaL_serializer *cfg);

/**
 * Parse configuration table into @a cfg.
 * @param L Lua stack.
 * @param cfg Serializer to inherit configuration.
 */
void
luaL_serializer_parse_options(struct lua_State *l,
			      struct luaL_serializer *cfg);

/* }}} luaL_serializer manipulations */

/* {{{ Fill luaL_field */

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
		decimal_t *decval;
		struct tt_uuid *uuidval;
		struct error *errorval;
		struct datetime *dateval;
		/** Interval value. */
		struct interval *interval;
	};
	enum mp_type type;
	/* subtypes of MP_EXT */
	enum mp_extension_type ext_type;
	bool compact;                /* a flag used by YAML serializer */
};

/**
 * @brief Get the next field from the table in sorted order.
 *
 * Pops a key and a 'key index' from the stack, and pushes a key, 'key-index',
 * value for the next element in a table provided as a third argument on stack.
 *
 * If cfg->encode_key_order is set, this function iterates over the table in the
 * order defined by this array. During this process the 'key-index' is the
 * index of the current key in cfg->encode_key_order array. If there are no more
 * elements in the cfg->encode_key_order array this function iterates over the
 * unvisited elements of the table in an arbitrary order.
 *
 * During the iterating this function may push a copy (which may be modified by
 * the iterator) of the provided table on the stack which will be popped when
 * there are no more elements left in the table.
 *
 * When the iterator reaches the last element of the table this function returns
 * 0 and doesn't push anything on the stack, leaving the original table.
 *
 * A typical traversal looks like this:
 *
 * // cfg->encode_key_order is an array with the order of the keys
 * // table is on the top of the stack
 * lua_pushnil(L);  // first key index
 * lua_pushnil(L);  // first key
 * while (luaL_next_field(l, cfg) != 0) {
 *     printf("%s - %s\n",
 *            lua_typename(L, lua_type(L, -2)),  // key
 *            lua_typename(L, lua_type(L, -1))); // value
 *     lua_pop(L, 1); // remove value, keep key and key index for next iteration
 * }
 *
 * @param L Lua stack.
 * @param cfg Serializer configuration.
 * @retval 0 if there are no more fields in the table.
 */
int
luaL_next_field(struct lua_State *L, struct luaL_serializer *cfg);

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
 * ARRAY vs MAP recognition works based on encode_sparse_convert,
 * encode_sparse_ratio, encode_sparse_safe and encode_load_metatables config
 * parameters (see above). Tables are not saved to lua_field structure and
 * should be processed manually, according to returned type and size value.
 *
 * This function doesn't try to unpack unknown types and simple returns MP_EXT.
 * The caller can use luaL_tofield() for basic conversion, then invoke internal
 * hooks(if available) and then call luaL_checkfield(), which will try to
 * unpack cdata/userdata objects or raise and error.
 *
 * @param L stack
 * @param cfg configuration
 * @param index stack index
 * @param field conversion result
 *
 * @retval  0 Success.
 * @retval -1 Error.
 */
int
luaL_tofield(struct lua_State *L, struct luaL_serializer *cfg, int index,
	     struct luaL_field *field);

/**
 * @brief Try to convert userdata/cdata values using defined conversion logic.
 * Must be used only after lua_tofield().
 *
 * @param L stack
 * @param cfg configuration
 * @param idx stack index
 * @param field conversion result
 *
 * @retval  0 Success.
 * @retval -1 Error.
 */
int
luaL_convertfield(struct lua_State *L, struct luaL_serializer *cfg, int idx,
		  struct luaL_field *field);

/**
 * @brief A wrapper for luaL_tofield() and luaL_convertfield() that
 * tries to convert value or raise an error.
 * @param L stack
 * @param cfg configuration
 * @param idx stack index
 * @param field conversion result
 * @sa lua_tofield()
 * @sa luaL_convertfield()
 *
 * Common conversion order for tables:
 * size/count detection -> (sparse array checking) -> (__serialize)
 *
 * Common conversion order for userdata/cdata objects:
 * (internal trigger) -> (__serialize) -> (tostring) -> (nil) -> exception
 *
 * Common conversion order for other types:
 * (tostring) -> (nil) -> exception
 */
static inline void
luaL_checkfield(struct lua_State *L, struct luaL_serializer *cfg, int idx,
		struct luaL_field *field)
{
	if (luaL_tofield(L, cfg, idx, field) < 0)
		luaT_error(L);
	if (field->type != MP_EXT)
		return;
	if (luaL_convertfield(L, cfg, idx, field) != 0)
		luaT_error(L);
}

/* }}} Fill luaL_field */

/* {{{ Set map / array hint */

/**
 * Push Lua Table with __serialize = 'map' hint onto the stack.
 * Tables with __serialize hint are properly handled by all serializers.
 * @param L stack
 * @param idx index in the stack
 */
static inline void
luaL_setmaphint(struct lua_State *L, int idx)
{
	if (idx < 0)
		idx = lua_gettop(L) + idx + 1;
	assert(lua_type(L, idx) == LUA_TTABLE);
	lua_rawgeti(L, LUA_REGISTRYINDEX, luaL_map_metatable_ref);
	lua_setmetatable(L, idx);
}

/**
 * Push Lua Table with __serialize = 'seq' hint onto the stack.
 * Tables with __serialize hint are properly handled by all serializers.
 * @param L stack
 * @param idx index in the stack
 */
static inline void
luaL_setarrayhint(struct lua_State *L, int idx)
{
	if (idx < 0)
		idx = lua_gettop(L) + idx + 1;
	assert(lua_type(L, idx) == LUA_TTABLE);
	lua_rawgeti(L, LUA_REGISTRYINDEX, luaL_array_metatable_ref);
	lua_setmetatable(L, idx);
}

/* }}} Set map / array hint */

static inline void
luaL_checkfinite(struct lua_State *L, struct luaL_serializer *cfg,
		 lua_Number number)
{
	if (!cfg->decode_invalid_numbers && !isfinite(number))
		luaL_error(L, "number must not be NaN or Inf");
}

/* {{{ luaT_reftable */

/**
 * Serialize the object at the given Lua stack index and all the
 * descendant ones recursively and create a mapping from the
 * original objects to the resulting ones.
 *
 * The mapping (a Lua table) is pushed on top of the Lua stack.
 * The function returns amount of objects pushed to the stack: it
 * is always 1.
 *
 * The serialization is performed using luaL_checkfield() with the
 * provided configuration.
 *
 * A table that indirectly references itself is a valid input for
 * this function: it tracks visited objects internally to break
 * the cycle.
 *
 * If an error is raised by a __serialize or __tostring
 * metamethod, it is raised by this function (not caught).
 */
int
luaT_reftable_new(struct lua_State *L, struct luaL_serializer *cfg, int idx);

/**
 * Look for an object from top of the Lua stack in the reference
 * table and, if found, replace it with the saved serialized
 * object.
 *
 * If the object is not found, do nothing.
 *
 * The function leaves the stack size unchanged.
 */
void
luaT_reftable_serialize(struct lua_State *L, int reftable_index);

/* }}} luaT_reftable */

int
tarantool_lua_serializer_init(struct lua_State *L);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */
