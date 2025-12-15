#! /usr/bin/lua
--
-- lua-Harness : <https://fperrad.frama.io/lua-Harness/>
--
-- Copyright (C) 2009-2021, Perrad Francois
--
-- This code is licensed under the terms of the MIT/X11 license,
-- like Lua itself.
--

--[[

=head1 Lua Package Library

=head2 Synopsis

    % prove 303-package.t

=head2 Description

Tests Lua Package Library

See section "Modules" in "Reference Manual"
L<https://www.lua.org/manual/5.1/manual.html#5.3>,
L<https://www.lua.org/manual/5.2/manual.html#6.3>,
L<https://www.lua.org/manual/5.3/manual.html#6.3>,
L<https://www.lua.org/manual/5.4/manual.html#6.3>

=cut

--]]

require'test_assertion'
local profile = require'profile'
local luajit21 = jit and (jit.version_num >= 20100 or jit.version:match'^RaptorJIT')
local has_loaders = _VERSION == 'Lua 5.1'
local has_alias_loaders = profile.compat51
local has_loadlib52 = _VERSION >= 'Lua 5.2' or jit
local has_module = _VERSION == 'Lua 5.1' or profile.compat51
local has_searchers = _VERSION >= 'Lua 5.2'
local has_alias_searchers = luajit21 and profile.luajit_compat52
local has_searcherpath = _VERSION >= 'Lua 5.2' or jit
local has_require54 = _VERSION >= 'Lua 5.4'

plan'no_plan'


is_string(package.config)

is_string(package.cpath)

is_string(package.path)

if has_loaders then
    is_table(package.loaders, "table package.loaders")
elseif has_alias_loaders then
    equals(package.loaders, package.searchers, "alias package.loaders")
else
    is_nil(package.loaders, "no package.loaders")
end

if has_searchers then
    is_table(package.searchers, "table package.searchers")
elseif has_alias_searchers then
    equals(package.searchers, package.loaders, "alias package.searchers")
else
    is_nil(package.searchers, "no package.searchers")
end

do -- loaded
    truthy(package.loaded._G, "table package.loaded")
    truthy(package.loaded.coroutine)
    truthy(package.loaded.io)
    truthy(package.loaded.math)
    truthy(package.loaded.os)
    truthy(package.loaded.package)
    truthy(package.loaded.string)
    truthy(package.loaded.table)

    local m = require'os'
    equals(m, package.loaded['os'])
end

do -- preload
    is_table(package.preload, "table package.preload")
    equals(# package.preload, 0)

    local foo = {}
    foo.bar = 1234
    local function foo_loader ()
       return foo
    end
    package.preload.foo = foo_loader
    local m = require 'foo'
    assert(m == foo)
    equals(m.bar, 1234, "function require & package.preload")
end

do -- loadlib
    local path_lpeg = package.searchpath and package.searchpath('lpeg', package.cpath)

    local f, msg = package.loadlib('libbar', 'baz')
    is_nil(f, "loadlib")
    is_string(msg)

    if path_lpeg then
        f, msg = package.loadlib(path_lpeg, 'baz')
        is_nil(f, "loadlib")
        matches(msg, 'undefined symbol')

        f = package.loadlib(path_lpeg, 'luaopen_lpeg')
        is_function(f, "loadlib ok")
    else
        skip("no lpeg path")
    end

    if has_loadlib52 then
        f, msg = package.loadlib('libbar', '*')
        is_nil(f, "loadlib '*'")
        is_string(msg)

        if path_lpeg then
            f = package.loadlib(path_lpeg, '*')
            is_true(f, "loadlib '*'")
        else
            skip("no lpeg path")
        end
    end
end

-- searchpath
if has_searcherpath then
    local p = package.searchpath('test_assertion', package.path)
    is_string(p, "searchpath")
    matches(p, "test_assertion.lua$", "searchpath")
    p = package.searchpath('test_assertion', 'bad path')
    is_nil(p)
else
    is_nil(package.searchpath, "no package.searchpath")
end

do -- require
    local f = io.open('complex.lua', 'w')
    f:write [[
complex = {}

function complex.new (r, i) return {r=r, i=i} end

--defines a constant 'i'
complex.i = complex.new(0, 1)

function complex.add (c1, c2)
    return complex.new(c1.r + c2.r, c1.i + c2.i)
end

function complex.sub (c1, c2)
    return complex.new(c1.r - c2.r, c1.i - c2.i)
end

function complex.mul (c1, c2)
    return complex.new(c1.r*c2.r - c1.i*c2.i,
                       c1.r*c2.i + c1.i*c2.r)
end

local function inv (c)
    local n = c.r^2 + c.i^2
    return complex.new(c.r/n, -c.i/n)
end

function complex.div (c1, c2)
    return complex.mul(c1, inv(c2))
end

return complex
]]
    f:close()
    if has_require54 then
        local m1, path1 = require 'complex'
        equals(m1, complex, "function require")
        equals(path1, './complex.lua')
        local m2, path2 = require 'complex'
        equals(m1, m2)
        equals(path2, nil)
    else
        local m1 = require 'complex'
        equals(m1, complex, "function require")
        local m2 = require 'complex'
        equals(m1, m2)
    end
    equals(complex.i.r, 0)
    equals(complex.i.i, 1)
    os.remove('complex.lua') -- clean up

    error_matches(function () require('no_module') end,
            "^[^:]+:%d+: module 'no_module' not found:",
            "function require (no module)")

    f = io.open('syntax.lua', 'w')
    f:write [[?syntax error?]]
    f:close()
    error_matches(function () require('syntax') end,
            "error loading module 'syntax' from file '.+[/\\]syntax%.lua':",
            "function require (syntax error)")
    os.remove('syntax.lua') -- clean up

    f = io.open('bar.lua', 'w')
    f:write [[
    print("    in bar.lua", ...)
    a = ...
]]
    f:close()
    a = nil
    require 'bar'
    equals(a, 'bar', "function require (arg)")
    os.remove('bar.lua') -- clean up

    f = io.open('cplx.lua', 'w')
    f:write [[
-- print('cplx.lua', ...)
local _G = _G
_ENV = nil
local cplx = {}

local function new (r, i) return {r=r, i=i} end
cplx.new = new

--defines a constant 'i'
cplx.i = new(0, 1)

function cplx.add (c1, c2)
    return new(c1.r + c2.r, c1.i + c2.i)
end

function cplx.sub (c1, c2)
    return new(c1.r - c2.r, c1.i - c2.i)
end

function cplx.mul (c1, c2)
    return new(c1.r*c2.r - c1.i*c2.i,
               c1.r*c2.i + c1.i*c2.r)
end

local function inv (c)
    local n = c.r^2 + c.i^2
    return new(c.r/n, -c.i/n)
end

function cplx.div (c1, c2)
    return mul(c1, inv(c2))
end

_G.cplx = cplx
return cplx
]]
    f:close()
    require 'cplx'
    equals(cplx.i.r, 0, "function require & module")
    equals(cplx.i.i, 1)
    os.remove('cplx.lua') -- clean up
end

-- module & seeall
local done_testing = done_testing
if has_module then
    m = {}
    package.seeall(m)
    m.passes("function package.seeall")

    is_nil(mod, "function module & seeall")
    module('mod', package.seeall)
    is_table(mod)
    equals(mod, package.loaded.mod)

    is_nil(modz, "function module")
    local _G = _G
    module('modz')
    _G.is_table(_G.modz)
    _G.equals(_G.modz, _G.package.loaded.modz)
else
    is_nil(package.seeall, "package.seeall (removed)")
    is_nil(module, "module (removed)")
end

done_testing()

-- Local Variables:
--   mode: lua
--   lua-indent-level: 4
--   fill-column: 100
-- End:
-- vim: ft=lua expandtab shiftwidth=4:
