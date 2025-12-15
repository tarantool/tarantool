---
-- lua-Harness : <https://fperrad.frama.io/lua-Harness/>
---

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
--]]

}

require'strict'.off()                   -- allows undeclared variables

_G.utf8 = nil                           -- not compatible with the PUC one

-- luacheck: globals _dofile
function _dofile (filename)
    print("# Custom dofile")
    return dofile(filename)
end

package.loaded.profile = profile        -- prevents loading of default profile

return profile

--
-- Copyright (c) 2018-2021 Francois Perrad
--
-- This library is licensed under the terms of the MIT/X11 license,
-- like Lua itself.
--
