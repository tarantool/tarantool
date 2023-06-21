local t = require('luatest')

local ffi = require('ffi')
ffi.cdef('void *dlsym(void *handle, const char *symbol);')

-- See `man 3 dlsym`:
-- RTLD_DEFAULT
--   Find the first occurrence of the desired symbol using the
--   default shared object search order. The search will include
--   global symbols in the executable and its dependencies, as
--   well as symbols in shared objects that were dynamically
--   loaded with the RTLD_GLOBAL flag.
local RTLD_DEFAULT = ffi.cast('void *', jit.os == 'OSX' and -2LL or 0LL)

local function dlsym(sym)
    return ffi.C.dlsym(RTLD_DEFAULT, sym)
end

local exported = {

    -- Interfaces from the Lua 5.0 Reference Manual (i.e. Lua C
    -- API function from lua.h). Mostly deprecated or substituted
    -- by something new. Left only for compatibility.
    -- XXX: Interfaces implemented via C macros are also listed
    -- below only for consistency.
    -- <lua_getgccount> is a macro to <lua_gc>
    -- <lua_getregistry> is a macro to <lua_pushvalue>
    -- <lua_open> is a macro to <luaL_newstate>
    -- <lua_strlen> is a macro to <lua_objlen>

    -- Auxiliary interfaces from the Lua 5.0 Reference Manual
    -- (i.e. Lua auxiliary library functions from lauxlib.h).
    -- Mostly deprecated or substituted by something new. Left
    -- only for compatibility.
    -- XXX: Interfaces implemented via C macros are also listed
    -- below only for consistency.
    -- <luaL_putchar> is a macro to <luaL_addchar>

    -- Interfaces to load particular Lua builtin module (i.e. Lua
    -- library loaders from lualib.h). Use <lua_openlibs> in favor
    -- to any of them.
    'luaopen_base',
    'luaopen_debug',
    'luaopen_io',
    'luaopen_math',
    'luaopen_os',
    'luaopen_package',
    'luaopen_string',
    'luaopen_table',

    -- Interfaces from the Lua 5.1 Reference Manual (i.e. Lua C
    -- API functions from lua.h).
    -- XXX: Interfaces implemented via C macros are also listed
    -- below only for consistency.
    'lua_atpanic',
    'lua_call',
    'lua_checkstack',
    'lua_close',
    'lua_concat',
    'lua_cpcall',
    'lua_createtable',
    'lua_dump',
    'lua_equal',
    'lua_error',
    'lua_gc',
    'lua_getallocf',
    'lua_getfenv',
    'lua_getfield',
    -- <lua_getglobal> is a macro to <lua_getfield>.
    'lua_gethook',
    'lua_gethookcount',
    'lua_gethookmask',
    'lua_getinfo',
    'lua_getlocal',
    'lua_getmetatable',
    'lua_getstack',
    'lua_gettable',
    'lua_gettop',
    'lua_getupvalue',
    'lua_insert',
    -- <lua_isboolean> is a macro to <lua_type>.
    'lua_iscfunction',
    -- <lua_isfunction> is a macro to <lua_type>.
    -- <lua_islightuserdata> is a macro to <lua_type>.
    -- <lua_isnil> is a macro to <lua_type>.
    -- <lua_isnone> is a macro to <lua_type>.
    -- <lua_isnoneornil> is a macro to <lua_type>.
    'lua_isnumber',
    'lua_isstring',
    -- <lua_istable> is a macro to <lua_type>.
    -- <lua_isthread> is a macro to <lua_type>.
    'lua_isuserdata',
    'lua_lessthan',
    'lua_load',
    'lua_newstate',
    -- <lua_newtable> is a macro to <lua_createtable>.
    'lua_newthread',
    'lua_newuserdata',
    'lua_next',
    'lua_objlen',
    'lua_pcall',
    -- <lua_pop> is a macro to <lua_settop>.
    'lua_pushboolean',
    'lua_pushcclosure',
    -- <lua_pushcfunction> is a macro to <lua_pushcclosure>.
    'lua_pushfstring',
    'lua_pushinteger',
    'lua_pushlightuserdata',
    -- <lua_pushliteral> is a macro to <lua_pushlstring>.
    'lua_pushlstring',
    'lua_pushnil',
    'lua_pushnumber',
    'lua_pushstring',
    'lua_pushthread',
    'lua_pushvalue',
    'lua_pushvfstring',
    'lua_rawequal',
    'lua_rawget',
    'lua_rawgeti',
    'lua_rawset',
    'lua_rawseti',
    -- <lua_register> is a macro to <lua_pushcfunction> + <lua_setglobal>.
    'lua_remove',
    'lua_replace',
    'lua_resume',
    'lua_setallocf',
    'lua_setfenv',
    'lua_setfield',
    -- <lua_setglobal> is a macro to <lua_setfield>.
    'lua_sethook',
    'lua_setlocal',
    'lua_setmetatable',
    'lua_settable',
    'lua_settop',
    'lua_setupvalue',
    'lua_status',
    'lua_toboolean',
    'lua_tocfunction',
    'lua_tointeger',
    'lua_tolstring',
    'lua_tonumber',
    'lua_topointer',
    -- <lua_tostring> is a macro to <lua_tolstring>.
    'lua_tothread',
    'lua_touserdata',
    'lua_type',
    'lua_typename',
    -- <lua_upvalueindex> is a macro to LUA_GLOBALSINDEX.
    'lua_xmove',
    'lua_yield',

    -- Auxiliary interfaces from the Lua 5.1 Reference Manual
    -- (i.e. Lua auxiliary library functions from lauxlib.h).
    -- XXX: Interfaces implemented via C macros are also listed
    -- below only for consistency.
    -- <luaL_addchar> is a macro.
    'luaL_addlstring',
    -- <luaL_addsize> is a macro.
    'luaL_addstring',
    'luaL_addvalue',
    -- <luaL_argcheck> is a macro to <luaL_argerror>.
    'luaL_argerror',
    'luaL_buffinit',
    'luaL_callmeta',
    'luaL_checkany',
    -- <luaL_checkint> is a macro to <luaL_checkinteger>.
    'luaL_checkinteger',
    -- <luaL_checklong> is a macro to <luaL_checkinteger>.
    'luaL_checklstring',
    'luaL_checknumber',
    'luaL_checkoption',
    'luaL_checkstack',
    -- <luaL_checkstring> is a macro to <luaL_checklstring>.
    'luaL_checktype',
    'luaL_checkudata',
    -- <luaL_dofile> is a macro to <luaL_loadfile> + <lua_pcall>.
    -- <luaL_dostring> is a macro to <luaL_loadstring> + <lua_pcall>.
    'luaL_error',
    'luaL_getmetafield',
    -- <luaL_getmetatable> is a macro to <lua_getfield> + LUA_REGISTRYINDEX.
    'luaL_gsub',
    'luaL_loadbuffer',
    'luaL_loadfile',
    'luaL_loadstring',
    'luaL_newmetatable',
    'luaL_newstate',
    'luaL_openlibs',
    -- <luaL_optint> is a macro to <luaL_optinteger>.
    'luaL_optinteger',
    -- <luaL_optlong> is a macro to <luaL_optinteger>.
    'luaL_optlstring',
    'luaL_optnumber',
    -- <luaL_optstring> is a macro to <luaL_optlstring>.
    'luaL_prepbuffer',
    'luaL_pushresult',
    'luaL_ref',
    'luaL_register',
    -- <luaL_typename> is a macro to <lua_typename>.
    'luaL_typerror',
    'luaL_unref',
    'luaL_where',

    -- Interfaces from the Lua 5.2 Reference Manual (i.e. Lua C
    -- API functions from lua.h).
    -- XXX: Interfaces implemented via C macros are also listed
    -- below only for consistency.
    'lua_copy',
    'lua_loadx',
    'lua_tointegerx',
    'lua_tonumberx',
    'lua_upvalueid',
    'lua_upvaluejoin',
    'lua_version',

    -- Auxiliary interfaces from the Lua 5.2 Reference Manual
    -- (i.e. Lua auxiliary library functions from lauxlib.h).
    -- XXX: Interfaces implemented via C macros are also listed
    -- below only for consistency.
    'luaL_execresult',
    'luaL_fileresult',
    'luaL_loadbufferx',
    'luaL_loadfilex',
    -- <luaL_newlibtable> is a macro to <lua_createtable>.
    -- <luaL_newlib> is a macro to <luaL_newlibtable> + <luaL_setfuncs>.
    -- XXX: Interface to provide compatibility with old module
    -- system. Not listed in Reference Manual, but is introduced
    -- in Lua 5.2.
    'luaL_openlib',
    -- XXX: Interface to provide compatibility with old module
    -- system. Not listed in Reference Manual, but is introduced
    -- in Lua 5.2.
    'luaL_pushmodule',
    'luaL_setfuncs',
    'luaL_setmetatable',
    'luaL_testudata',
    'luaL_traceback',

    -- Interfaces from the Lua 5.3 Reference Manual (i.e. Lua C
    -- API functions from lua.h).
    -- XXX: Interfaces implemented via C macros are also listed
    -- below only for consistency.
    'lua_isyieldable',

    -- Auxiliary interfaces from the Lua 5.3 Reference Manual
    -- (i.e. Lua auxiliary library functions from lauxlib.h).
    -- XXX: Interfaces implemented via C macros are also listed
    -- below only for consistency.
    -- <luaL_opt> is a macro to <lua_isnoneornil>.

    -- Interfaces provided by LuaJIT (i.e. public functions from
    -- luajit.h).
    'luaJIT_profile_dumpstack',
    'luaJIT_profile_start',
    'luaJIT_profile_stop',
    'luaJIT_setmode',
    'luaJIT_version_2_1_0_beta3',

    -- Auxiliary interfaces provided by LuaJIT (i.e. public
    -- functions from lauxlib.h, which are neither listed in any
    -- Lua Reference Manual, nor found in Lua source code).
    'luaL_findtable',

    -- Interfaces to load particular LuaJIT builtin module (listed
    -- in lualib.h for consistency). # Use <lua_openlibs> in favor
    -- to any of them.
    'luaopen_bit',
    'luaopen_ffi',
    'luaopen_jit',

    -- Interfaces provided by Tarantool LuaJIT fork (i.e. public
    -- functions from lmisclib.h)
    'luaM_metrics',
    'luaM_sysprof_report',
    'luaM_sysprof_set_backtracer',
    'luaM_sysprof_set_on_stop',
    'luaM_sysprof_set_writer',
    'luaM_sysprof_start',
    'luaM_sysprof_stop',

    -- Interface to load misc builtin module. Use <lua_openlibs>
    -- in favor to it.
    'luaopen_misc',
}

local hidden = {
    -- Interfaces introduced for internal Tarantool use.
    'lua_hash',
    'lua_hashstring',
}

local g = t.group()

g.before_all(function()
    -- XXX: There is known ffi.C.dlsym misbehaviour on FreeBSD
    -- if JIT is enabled. For more info see the following issue:
    -- https://github.com/tarantool/tarantool/issues/7640.
    if jit.os == 'BSD' then
        jit.off()
    end
end)

for _, sym in pairs(exported) do
    g['test_export_' .. sym] = function()
        t.assert(dlsym(sym) ~= nil, ('Symbol %q is not exported'):format(sym))
    end
end

for _, sym in pairs(hidden) do
    g['test_hidden_' .. sym] = function()
        t.assert(dlsym(sym) == nil, ('Symbol %q is not hidden'):format(sym))
    end
end
