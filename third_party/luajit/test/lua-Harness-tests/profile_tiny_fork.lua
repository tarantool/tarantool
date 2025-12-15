---
-- lua-Harness : <https://fperrad.frama.io/lua-Harness/>
---

require'compat53.math'
-- luacheck: globals utf8
utf8 = require'compat53.utf8'

local profile = {

--[[ compat 5.0
    has_string_gfind = true,
    has_math_mod = true,
--]]

    compat51 = false,
--[[
    has_unpack = true,
    has_package_loaders = true,
    has_math_log10 = true,
    has_loadstring = true,
    has_table_maxn = true,
    has_module = true,
    has_package_seeall = true,
--]]

    compat52 = false,
--[[
    has_mathx = true,
    has_bit32 = true,
    has_metamethod_ipairs = true,
--]]

    compat53 = false,
--[[
    has_math_log10 = true,
    has_mathx = true,
    has_metamethod_ipairs = true,
--]]

-- [[ luajit
    luajit_compat52 = false,
    openresty = false,
    pack = true,
    integer = true,
    utf8 = true,
--]]

}

package.loaded.profile = profile        -- prevents loading of default profile

return profile

--
-- Copyright (c) 2018-2019 Francois Perrad
--
-- This library is licensed under the terms of the MIT/X11 license,
-- like Lua itself.
--
