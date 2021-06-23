#include <lua.h>              /* lua_*() */
#include <lauxlib.h>          /* luaL_*() */
#include <lualib.h>           /* luaL_openlibs() */
#include "unit.h"             /* plan, header, footer, is, ok */
#include "lua/serializer.h"   /* functions to test */
#include "mp_extension_types.h" /* enum mp_extension_type */

static int
check_luaL_field(const struct luaL_field *field, const struct luaL_field *exp,
		 const char *description)
{
	plan(4);
	header();

	is(field->type, exp->type, "%s: type", description);

	/* More types may be added on demand. */
	switch (exp->type) {
	case MP_STR: {
		/*
		 * Don't compare string values for equality: check
		 * whether actual result contains the expected
		 * pattern at beginning. It is just to simplify
		 * writting of test cases.
		 */
		int rc = strstr(field->sval.data, exp->sval.data) ==
			field->sval.data;
		ok(rc, "%s: sval.data", description);
		/* Don't compare 'sval.len'. */
		ok(true, "# skip; %s: Don't compare 'exp_type'", description);
		ok(true, "# skip; %s: Don't compare 'compact'", description);
		break;
	}
	case MP_ARRAY:
	case MP_MAP:
		is(field->size, exp->size, "%s: size", description);
		ok(true, "# skip; %s: Don't compare 'exp_type'", description);
		is(field->compact, exp->compact, "%s: compact", description);
		break;
	case MP_EXT:
		ok(true, "# skip; %s: Don't check MP_EXT data", description);
		is(field->ext_type, exp->ext_type, "%s: ext_type", description);
		ok(true, "# skip; %s: Don't compare 'compact'", description);
		break;
	default:
		assert(false);
	}

	footer();
	return check_plan();
}

static int
test_luaL_field_basic(struct lua_State *L)
{
	struct {
		/* A string to output with a test case. */
		const char *description;
		/* A code to push a Lua object to inspect. */
		const char *src;
		/*
		 * Whether to call and verify luaL_checkfield()
		 * instead of luaL_tofield() (the default is
		 * luaL_tofield()).
		 */
		bool invoke_checkfield;
		/*
		 * Expected <struct luaL_field> after call to
		 * luaL_tofield() / luaL_checkfield().
		 *
		 * For MP_STR: interpret .sval.data as beginning
		 * of the actual string (for testing purposes:
		 * tostring() representation of userdata and cdata
		 * contains an address at the end). Don't compare
		 * .sval.len.
		 */
		struct luaL_field exp_field;
		/*
		 * A code to check the resulting value on the Lua
		 * stack after luaL_tofield() / luaL_checkfield().
		 *
		 * 'res' is the object to verify.
		 *
		 * 'src' contains the source object.
		 *
		 * Use 'cmp(a, b)' for a deep comparison.
		 */
		const char *check_lua;
	} cases[] = {
		{
			.description = "table as array",
			.src = "return {1, 2, 3}",
			.exp_field = {
				.size = 3,
				.type = MP_ARRAY,
				.compact = false,
			},
			.check_lua = "return res == src",
		},
		{
			.description = "table as map",
			.src = "return {foo = 'bar'}",
			.exp_field = {
				.size = 1,
				.type = MP_MAP,
				.compact = false,
			},
			.check_lua = "return res == src",
		},
		{
			.description = "table with __serialize = 'map'",
			.src = "return setmetatable({1, 2, 3}, {"
			       "    __serialize = 'map'"
			       "})",
			.exp_field = {
				.size = 3,
				.type = MP_MAP,
				.compact = true,
			},
			.check_lua = "return res == src",
		},
		{
			.description = "table with __serialize function",
			.src = "return setmetatable({foo = 'bar'}, {"
			       "    __serialize = function(self)"
			       "        return {1, 2, 3}"
			       "    end"
			       "})",
			.exp_field = {
				.size = 3,
				.type = MP_ARRAY,
				.compact = false,
			},
			.check_lua = "return cmp(res, {1, 2, 3})",
		},
		{
			.description = "unknown userdata",
			.src = "return newproxy()",
			.exp_field = {
				.type = MP_EXT,
				.ext_type = MP_UNKNOWN_EXTENSION,
			},
			.check_lua = "return type(res) == 'userdata' and"
				     "    res == src",
		},
		{
			.description = "unknown userdata (checkfield)",
			.src = "return newproxy()",
			.invoke_checkfield = true,
			.exp_field = {
				.sval = {
					.data = "userdata: 0x",
				},
				.type = MP_STR,
			},
			.check_lua = "return type(res) == 'string' and"
				     "    res:match('^userdata: ')",
		},
		{
			.description = "userdata with __serialize function",
			.src = "do"
			       "    local ud = newproxy(true)"
			       "    local mt = getmetatable(ud)"
			       "    mt.__serialize = function(self)"
			       "        return {1, 2, 3}"
			       "    end"
			       /*
				* See the comment in the next test
				* case.
				*/
			       "    mt.__index = mt"
			       /* Keep the trailing whitespace. */
			       "    return ud "
			       "end",
			.exp_field = {
				.type = MP_EXT,
				.ext_type = MP_UNKNOWN_EXTENSION,
			},
			.check_lua = "return type(res) == 'userdata' and"
				     "    res == src",
		},
		{
			.description = "userdata with __serialize function "
				       "(checkfield)",
			.src = "do"
			       "    local ud = newproxy(true)"
			       "    local mt = getmetatable(ud)"
			       "    mt.__serialize = function(self)"
			       "        return {1, 2, 3}"
			       "    end"
			       /*
				* Surprise! __serialize should be
				* accessible via lua_gettable(),
				* otherwise our serializer will
				* not recognize it. So we should
				* set __index to the metatable
				* itself.
				*
				* luaL_register_type() does it.
				* However, personally, I don't see
				* a strict reason for that and for
				* me it looks hacky. It would be
				* better to teach the serializer
				* to inspect the metatable
				* directly.
				*/
			       "    mt.__index = mt"
			       /* Keep the trailing whitespace. */
			       "    return ud "
			       "end",
			.invoke_checkfield = true,
			.exp_field = {
				.size = 3,
				.type = MP_ARRAY,
			},
			.check_lua = "return cmp(res, {1, 2, 3})",
		},
		{
			.description = "unknown cdata",
			.src = "do"
			       "    local ffi = require('ffi')"
			       "    ffi.cdef([["
			       "        struct foo {"
			       "            int x;"
			       "        };"
			       "    ]])"
			       /* Keep the trailing whitespace. */
			       "    return ffi.new('struct foo', {x = 42}) "
			       "end",
			.exp_field = {
				.type = MP_EXT,
				.ext_type = MP_UNKNOWN_EXTENSION,
			},
			.check_lua = "do"
				   "    local ffi = require('ffi')"
				   "    return type(res) == 'cdata' and"
				   "        ffi.istype('struct foo', res) and"
				   "        res == src and"
				   /* Keep the trailing whitespace. */
				   "        res.x == 42 "
				   "end",
		},
		{
			.description = "unknown cdata (checkfield)",
			.src = "do"
			       "    local ffi = require('ffi')"
			       /*
				* ffi.cdef() is in the previous
				* test case.
				*/
			       /* Keep the trailing whitespace. */
			       "    return ffi.new('struct foo', {x = 42}) "
			       "end",
			.invoke_checkfield = true,
			.exp_field = {
				.sval = {
					.data = "cdata<struct foo>: 0x"
				},
				.type = MP_STR,
			},
			.check_lua = "return type(res) == 'string' and"
				     "    res:match('^cdata<struct foo>: ')",
		},
		{
			.description = "cdata with __serialize",
			.src = "do"
			       "    local ffi = require('ffi')"
			       "    local mt = {"
			       "        __serialize = function(self)"
			       "            return {1, 2, 3}"
			       "        end"
			       "    }"
			       /*
				* See the comment for the userdata
				* test case above.
				*/
			       "    mt.__index = mt"
			       /*
				* ffi.cdef() is in one of previous
				* test cases.
				*/
			       "    ffi.metatype('struct foo', mt)"
			       /* Keep the trailing whitespace. */
			       "    return ffi.new('struct foo', {x = 42}) "
			       "end",
			.exp_field = {
				.type = MP_EXT,
				.ext_type = MP_UNKNOWN_EXTENSION,
			},
			.check_lua = "do"
				   "    local ffi = require('ffi')"
				   "    return type(res) == 'cdata' and"
				   "        ffi.istype('struct foo', res) and"
				   "        res == src and"
				   /* Keep the trailing whitespace. */
				   "        res.x == 42 "
				   "end",
		},
		{
			.description = "cdata with __serialize (checkfield)",
			.src = "do"
			       "    local ffi = require('ffi')"
			       /*
				* ffi.cdef() and ffi.metatype()
				* are in previous test cases.
				*/
			       /* Keep the trailing whitespace. */
			       "    return ffi.new('struct foo', {x = 42}) "
			       "end",
			.invoke_checkfield = true,
			.exp_field = {
				.size = 3,
				.type = MP_ARRAY,
			},
			.check_lua = "return cmp(res, {1, 2, 3})",
		},
	};

	int planned = (int) (sizeof(cases) / sizeof(cases[0]));
	plan(4 * planned);
	header();

	/*
	 * Initialize serializer with almost default options.
	 *
	 * Set 'has_compact' to test it (otherwise
	 * <struct luaL_field>.compact will not be set).
	 *
	 * Set 'encode_use_tostring' just to don't introduce
	 * complex code with catching a Lua error from a C
	 * function.
	 */
	struct luaL_serializer cfg;
	luaL_serializer_create(&cfg);
	cfg.has_compact = true;
	cfg.encode_use_tostring = true;

	/* Add cmp function for check_lua. */
	luaL_loadstring(L,
		"do"
		"    local cmp"
		""
		"    cmp = function(a, b)"
		"        if type(a) ~= type(b) then"
		"            return false"
		"        end"
		""
		"        if type(a) == 'table' then"
		"            for k, v in pairs(a) do"
		"                if not cmp(v, b[k]) then"
		"                    return false"
		"                end"
		"            end"
		"            for k, v in pairs(b) do"
		"                if not cmp(v, a[k]) then"
		"                    return false"
		"                end"
		"            end"
		"            return true"
		"        end"
		""
		"        return a == b"
		"    end"
		""
		/* Caution: keep the trailing whitespace. */
		"    return cmp "
		"end");
	lua_call(L, 0, 1);
	lua_setglobal(L, "cmp");

	for (int i = 0; i < planned; ++i) {
		int initial_top = lua_gettop(L);

		const char *description = cases[i].description;

		/* Push a Lua object to the Lua stack. */
		luaL_loadstring(L, cases[i].src);
		lua_call(L, 0, 1);

		/* Set _G.src. */
		lua_pushvalue(L, -1);
		lua_setglobal(L, "src");

		/* Call luaL_tofield() / luaL_checkfield(). */
		int top = lua_gettop(L);
		struct luaL_field field;
		if (cases[i].invoke_checkfield) {
			luaL_checkfield(L, &cfg, -1, &field);
			ok(true, "# skip; %s: luaL_checkfield() has no retval",
			   description);
		} else {
			int rc = luaL_tofield(L, &cfg, NULL, -1, &field);
			is(rc, 0, "%s: luaL_tofield retval", description);
		}

		/* Check stack size. */
		is(lua_gettop(L) - top, 0, "%s: Lua stack size", description);

		/*
		 * Set _G.res. The object is placed to the same
		 * index as the source object: the top item in our
		 * case.
		 */
		lua_pushvalue(L, -1);
		lua_setglobal(L, "res");

		/* Check resulting Lua object. */
		luaL_loadstring(L, cases[i].check_lua);
		lua_call(L, 0, 1);
		is(lua_toboolean(L, -1), 1, "%s: Lua result", description);
		lua_pop(L, 1);

		/* Check luaL_field content. */
		check_luaL_field(&field, &cases[i].exp_field, description);

		/* Unset _G.src and _G.res. */
		lua_pushnil(L);
		lua_setglobal(L, "src");
		lua_pushnil(L);
		lua_setglobal(L, "res");

		/* Clean up the Lua stack. */
		lua_pop(L, 1);
		assert(lua_gettop(L) == initial_top);
	}

	/* Unset _G.cmp. */
	lua_pushnil(L);
	lua_setglobal(L, "cmp");

	footer();
	return check_plan();
}

int
main()
{
	struct lua_State *L = luaL_newstate();
	luaL_openlibs(L);

	tarantool_lua_serializer_init(L);

	return test_luaL_field_basic(L);
}
