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

=head1 Lua Debug Library

=head2 Synopsis

    % prove 310-debug.t

=head2 Description

Tests Lua Debug Library

See section "The Debug Library" in "Reference Manual"
L<https://www.lua.org/manual/5.1/manual.html#5.9>,
L<https://www.lua.org/manual/5.2/manual.html#6.10>,
L<https://www.lua.org/manual/5.3/manual.html#6.10>,
L<https://www.lua.org/manual/5.4/manual.html#6.10>

=cut

]]

require 'test_assertion'
local profile = require'profile'
local has_getfenv = _VERSION == 'Lua 5.1'
local has_gethook54 = _VERSION >= 'Lua 5.4'
local has_getlocal52 = _VERSION >= 'Lua 5.2' or profile.luajit_compat52
local has_getuservalue = _VERSION >= 'Lua 5.2' or profile.luajit_compat52
local has_getuservalue54 = _VERSION >= 'Lua 5.4'
local has_setcstacklimit = _VERSION >= 'Lua 5.4'
local has_setmetatable52 = _VERSION >= 'Lua 5.2' or (profile.luajit_compat52 and not ujit)
local has_upvalueid = _VERSION >= 'Lua 5.2' or jit
local has_upvaluejoin = _VERSION >= 'Lua 5.2' or jit

if not debug then
    skip_all("no debug")
end

plan'no_plan'

-- getfenv
if has_getfenv then
    equals(debug.getfenv(3.14), nil, "function getfenv")
    local function f () end
    is_table(debug.getfenv(f))
    equals(debug.getfenv(f), _G)
    is_table(debug.getfenv(print))
    equals(debug.getfenv(print), _G)

    local a = coroutine.create(function () return 1 end)
    is_table(debug.getfenv(a), "function getfenv (thread)")
    equals(debug.getfenv(a), _G)
else
    is_nil(debug.getfenv, "no debug.getfenv (removed)")
end

do -- getinfo
    local info = debug.getinfo(equals)
    is_table(info, "function getinfo (function)")
    equals(info.func, equals, " .func")

    info = debug.getinfo(equals, 'L')
    is_table(info, "function getinfo (function, opt)")
    is_table(info.activelines)

    info = debug.getinfo(1)
    is_table(info, "function getinfo (level)")
    matches(info.func, "^function: [0]?[Xx]?%x+", " .func")

    equals(debug.getinfo(12), nil, "function getinfo (too depth)")

    error_matches(function () debug.getinfo('bad') end,
            "bad argument #1 to 'getinfo' %(.- expected",
            "function getinfo (bad arg)")

    error_matches(function () debug.getinfo(equals, 'X') end,
            "bad argument #2 to 'getinfo' %(invalid option%)",
            "function getinfo (bad opt)")
end

do -- getlocal
    local name, value = debug.getlocal(0, 1)
    is_string(name, "function getlocal (level)")
    equals(value, 0)

    error_matches(function () debug.getlocal(42, 1) end,
            "bad argument #1 to 'getlocal' %(level out of range%)",
            "function getlocal (out of range)")

    if has_getlocal52 then
        name, value = debug.getlocal(matches, 1)
        is_string(name, "function getlocal (func)")
        equals(value, nil)
    else
        diag("no getlocal with function")
    end
end

do -- getmetatable
    local t = {}
    equals(debug.getmetatable(t), nil, "function getmetatable")
    local t1 = {}
    debug.setmetatable(t, t1)
    equals(debug.getmetatable(t), t1)

    local a = true
    equals(debug.getmetatable(a), nil)
    debug.setmetatable(a, t1)
    equals(debug.getmetatable(t), t1)

    a = 3.14
    equals(debug.getmetatable(a), nil)
    debug.setmetatable(a, t1)
    equals(debug.getmetatable(t), t1)
end

do -- getregistry
    local reg = debug.getregistry()
    is_table(reg, "function getregistry")
    is_table(reg._LOADED)
end

do -- getupvalue
    local name = debug.getupvalue(plan, 1)
    is_string(name, "function getupvalue")
end

do -- gethook
    debug.sethook()
    local hook, mask, count
    if has_gethook54 then
        hook = debug.gethook()
        equals(hook, nil, "function gethook")
    else
        hook, mask, count = debug.gethook()
        equals(hook, nil, "function gethook")
        equals(mask, '')
        equals(count, 0)
    end
    local function f () end
    debug.sethook(f, 'c', 42)
    hook , mask, count = debug.gethook()
    equals(hook, f, "function gethook")
    equals(mask, 'c')
    equals(count, 42)

    local co = coroutine.create(function () print "thread" end)
    hook = debug.gethook(co)
    if jit then
        is_function(hook, "function gethook(thread)")
    else
        is_nil(hook, "function gethook(thread)")
    end
end

do -- setlocal
    local name = debug.setlocal(0, 1, 0)
    is_string(name, "function setlocal (level)")

    name = debug.setlocal(0, 42, 0)
    equals(name, nil, "function setlocal (level)")

    error_matches(function () debug.setlocal(42, 1, true) end,
            "bad argument #1 to 'setlocal' %(level out of range%)",
            "function setlocal (out of range)")
end

-- setcstacklimit
if has_setcstacklimit then
    is_number(debug.setcstacklimit(200), "function setcstacklimit")
    equals(debug.setcstacklimit(1000), 200)

    error_matches(function () debug.setcstacklimit('bad') end,
            "^[^:]+:%d+: bad argument #1 to 'setcstacklimit' %(number expected, got string%)",
            "function setcstacklimit (bad arg)")
else
    is_nil(debug.setcstacklimit, "no debug.setcstacklimit")
end

-- setfenv
if has_getfenv then
    local t = {}
    local function f () end
    equals(debug.setfenv(f, t), f, "function setfenv")
    is_table(debug.getfenv(f))
    equals(debug.getfenv(f), t)
    equals(debug.setfenv(print, t), print)
    is_table(debug.getfenv(print))
    equals(debug.getfenv(print), t)

    t = {}
    local a = coroutine.create(function () return 1 end)
    equals(debug.setfenv(a, t), a, "function setfenv (thread)")
    is_table(debug.getfenv(a))
    equals(debug.getfenv(a), t)

    error_matches(function () t = {}; debug.setfenv(t, t) end,
            "^[^:]+:%d+: 'setfenv' cannot change environment of given object",
            "function setfenv (forbidden)")
else
    is_nil(debug.setfenv, "no debug.setfenv (removed)")
end

do -- setmetatable
    local t = {}
    local t1 = {}
    if has_setmetatable52 then
        equals(debug.setmetatable(t, t1), t, "function setmetatable")
    else
        equals(debug.setmetatable(t, t1), true, "function setmetatable")
    end
    equals(getmetatable(t), t1)

    error_matches(function () debug.setmetatable(t, true) end,
            "^[^:]+:%d+: bad argument #2 to 'setmetatable' %(nil or table expected")
end

do -- setupvalue
    local r, tb = pcall(require, 'Test.Builder')
    local value = r and tb:new() or {}
    local name = debug.setupvalue(plan, 1, value)
    is_string(name, "function setupvalue")

    name = debug.setupvalue(plan, 42, true)
    is_nil(name)
end

-- getuservalue / setuservalue
if has_getuservalue54 then
    local u = io.tmpfile()      -- lua_newuserdatauv(L, sizeof(LStream), 0);
    equals(debug.getuservalue(u, 0), nil, "function getuservalue")
    equals(debug.getuservalue(true), nil)

    error_matches(function () debug.getuservalue(u, 'foo') end,
            "^[^:]+:%d+: bad argument #2 to 'getuservalue' %(number expected, got string%)")

    local data = {}
    equals(debug.setuservalue(u, data, 42), nil, "function setuservalue")

    error_matches(function () debug.setuservalue({}, data) end,
            "^[^:]+:%d+: bad argument #1 to 'setuservalue' %(userdata expected, got table%)")

    error_matches(function () debug.setuservalue(u, data, 'foo') end,
            "^[^:]+:%d+: bad argument #3 to 'setuservalue' %(number expected, got string%)")
elseif has_getuservalue then
    local u = io.tmpfile()
    local old = debug.getuservalue(u)
    if jit then
        is_table(old, "function getuservalue")
    else
        is_nil(old, "function getuservalue")
    end
    equals(debug.getuservalue(true), nil)

    local data = {}
    local r = debug.setuservalue(u, data)
    equals(r, u, "function setuservalue")
    equals(debug.getuservalue(u), data)
    r = debug.setuservalue(u, old)
    equals(debug.getuservalue(u), old)

    error_matches(function () debug.setuservalue({}, data) end,
            "^[^:]+:%d+: bad argument #1 to 'setuservalue' %(userdata expected, got table%)")
else
    is_nil(debug.getuservalue, "no getuservalue")
    is_nil(debug.setuservalue, "no setuservalue")
end

do -- traceback
    matches(debug.traceback(), "^stack traceback:\n", "function traceback")

    matches(debug.traceback("message\n"), "^message\n\nstack traceback:\n", "function traceback with message")

    matches(debug.traceback(false), "false", "function traceback")
end

-- upvalueid
if has_upvalueid then
    local id = debug.upvalueid(plan, 1)
    is_userdata(id, "function upvalueid")
else
    is_nil(debug.upvalueid, "no upvalueid")
end

-- upvaluejoin
if has_upvaluejoin and jit then
    diag("jit upvaluejoin")
    -- TODO
elseif has_upvaluejoin then
    debug.upvaluejoin(passes, 1, fails, 1)

    error_matches(function () debug.upvaluejoin(true, 1, nil, 1) end,
            "bad argument #1 to 'upvaluejoin' %(function expected, got boolean%)",
            "function upvaluejoin (bad arg)")

    error_matches(function () debug.upvaluejoin(passes, 1, true, 1) end,
            "bad argument #3 to 'upvaluejoin' %(function expected, got boolean%)",
            "function upvaluejoin (bad arg)")
else
    is_nil(debug.upvaluejoin, "no upvaluejoin")
end

done_testing()

-- Local Variables:
--   mode: lua
--   lua-indent-level: 4
--   fill-column: 100
-- End:
-- vim: ft=lua expandtab shiftwidth=4:
