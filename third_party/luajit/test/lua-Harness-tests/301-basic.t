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

=head1 Lua Basic Library

=head2 Synopsis

    % prove 301-basic.t

=head2 Description

Tests Lua Basic Library

See section "Basic Functions" in "Reference Manual"
L<https://www.lua.org/manual/5.1/manual.html#5.1>,
L<https://www.lua.org/manual/5.2/manual.html#6.1>,
L<https://www.lua.org/manual/5.3/manual.html#6.1>,
L<https://www.lua.org/manual/5.4/manual.html#6.1>

=cut

--]]

require'test_assertion'
local profile = require'profile'
local has_error53 = _VERSION >= 'Lua 5.3'
local has_gcinfo = _VERSION == 'Lua 5.1'
local has_getfenv = _VERSION == 'Lua 5.1'
local has_ipairs53 = _VERSION >= 'Lua 5.3'
local has_load52 = _VERSION >= 'Lua 5.2' or profile.luajit_compat52
local has_loadfile52 = _VERSION >= 'Lua 5.2' or profile.luajit_compat52
local has_loadstring = _VERSION == 'Lua 5.1'
local has_alias_loadstring = profile.compat51
local has_newproxy = _VERSION == 'Lua 5.1'
local has_rawlen = _VERSION >= 'Lua 5.2' or profile.luajit_compat52
local has_unpack = _VERSION == 'Lua 5.1'
local has_alias_unpack = profile.compat51
local has_warn = _VERSION >= 'Lua 5.4'
local has_xpcall52 = _VERSION >= 'Lua 5.2' or jit
local has_xpcall53 = _VERSION >= 'Lua 5.3' or jit

local lua = _retrieve_progname()

plan'no_plan'

do -- assert
    local v, msg, extra = assert('text', "assert string", 'extra')
    equals(v, 'text', "function assert")
    equals(msg, "assert string")
    equals(extra, 'extra')
    v, msg, extra = assert({}, "assert table", 'extra')
    equals(msg, "assert table")
    equals(extra, 'extra')

    error_matches(function () assert(false, "ASSERTION TEST", 'extra') end,
            "^[^:]+:%d+: ASSERTION TEST",
            "function assert(false, msg)")

    error_matches(function () assert(false) end,
            "^[^:]+:%d+: assertion failed!",
            "function assert(false)")

    if has_error53 then
        v, msg = pcall(function() assert(false, 42) end)
        equals(msg, 42, "function assert(false, 42)")
    else
        error_matches(function () assert(false, 42) end,
                "^[^:]+:%d+: 42",
                "function assert(false, 42) --> invalid")
    end

    if has_error53 then
        local obj = {}
        v, msg = pcall(function() assert(false, obj) end)
        equals(msg, obj, "function assert(false, {})")
    else
        diag("no assert with table")
    end
end

-- collectgarbage
if jit then
    equals(collectgarbage('stop'), 0, "function collectgarbage 'stop/restart/collect'")
    equals(collectgarbage('step'), false)
    equals(collectgarbage('restart'), 0)
    equals(collectgarbage('step'), false)
    equals(collectgarbage('collect'), 0)
    equals(collectgarbage('setpause', 10), 200)
    equals(collectgarbage('setstepmul', 200), 200)
    equals(collectgarbage(), 0)
elseif _VERSION == 'Lua 5.1' then
    equals(collectgarbage('stop'), 0, "function collectgarbage 'stop/restart/collect'")
    equals(collectgarbage('restart'), 0)
    equals(collectgarbage('step'), false)
    equals(collectgarbage('collect'), 0)
    equals(collectgarbage(), 0)
elseif _VERSION == 'Lua 5.2' then
    equals(collectgarbage('stop'), 0, "function collectgarbage 'stop/restart/collect'")
    equals(collectgarbage('isrunning'), false)
    equals(collectgarbage('step'), false)
    equals(collectgarbage('restart'), 0)
    equals(collectgarbage('isrunning'), true)
    equals(collectgarbage('step'), false)
    equals(collectgarbage('collect'), 0)
    equals(collectgarbage('setpause', 10), 200)
    equals(collectgarbage('setstepmul', 200), 200)
    equals(collectgarbage(), 0)
    equals(collectgarbage('generational'), 0)
    equals(collectgarbage('step'), false)
    equals(collectgarbage('incremental'), 0)
    equals(collectgarbage('setmajorinc'), 200)
elseif _VERSION == 'Lua 5.3' then
    equals(collectgarbage('stop'), 0, "function collectgarbage 'stop/restart/collect'")
    equals(collectgarbage('isrunning'), false)
    --equals(collectgarbage('step'), false)
    is_boolean(collectgarbage('step'))
    equals(collectgarbage('restart'), 0)
    equals(collectgarbage('isrunning'), true)
    equals(collectgarbage('step'), false)
    equals(collectgarbage('collect'), 0)
    equals(collectgarbage('setpause', 10), 200)
    equals(collectgarbage('setstepmul', 200), 200)
    equals(collectgarbage(), 0)
    equals(collectgarbage('step'), false)
elseif _VERSION == 'Lua 5.4' then
    equals(collectgarbage('stop'), 0, "function collectgarbage 'stop/restart/collect'")
    equals(collectgarbage('isrunning'), false)
    equals(collectgarbage('generational'), 'generational')
    equals(collectgarbage('incremental'), 'generational')
    equals(collectgarbage('incremental'), 'incremental')
    equals(collectgarbage('generational'), 'incremental')
    equals(collectgarbage('step'), false)
    equals(collectgarbage('restart'), 0)
    equals(collectgarbage('isrunning'), true)
    equals(collectgarbage('step'), false)
    equals(collectgarbage('collect'), 0)
    equals(collectgarbage('setpause', 10), 200)
    equals(collectgarbage('setstepmul', 200), 100)
    equals(collectgarbage(), 0)
    equals(collectgarbage('step'), false)
end

is_number(collectgarbage('count'), "function collectgarbage 'count'")

error_matches(function () collectgarbage('unknown') end,
        "^[^:]+:%d+: bad argument #1 to 'collectgarbage' %(invalid option 'unknown'%)",
        "function collectgarbage (invalid)")

do -- dofile
    local f = io.open('lib-301.lua', 'w')
    f:write[[
function norm (x, y)
    return (x^2 + y^2)^0.5
end

function twice (x)
    return 2*x
end
]]
    f:close()
    dofile('lib-301.lua')
    local n = norm(3.4, 1.0)
    near(twice(n), 7.088, 0.001, "function dofile")

    os.remove('lib-301.lua') -- clean up

    error_matches(function () dofile('no_file-301.lua') end,
            "cannot open no_file%-301%.lua: No such file or directory",
            "function dofile (no file)")

    f = io.open('foo-301.lua', 'w')
    f:write[[?syntax error?]]
    f:close()
    error_matches(function () dofile('foo-301.lua') end,
            "^foo%-301%.lua:%d+:",
            "function dofile (syntax error)")

    os.remove('foo-301.lua') -- clean up
end

do -- error
    error_matches(function () error("ERROR TEST") end,
            "^[^:]+:%d+: ERROR TEST",
            "function error(msg)")

    error_equals(function () error("ERROR TEST", 0) end,
            "ERROR TEST",
            "function error(msg, 0)")

    if has_error53 then
        local v, msg = pcall(function() error(42) end)
        equals(msg, 42, "function error(42)")
    else
        error_matches(function () error(42) end,
                "^[^:]+:%d+: 42",
                "function error(42)")
    end

    local obj = {}
    local v, msg = pcall(function() error(obj) end)
    equals(msg, obj, "function error({})")

    v, msg = pcall(function() error() end)
    equals(msg, nil, "function error()")
end

-- gcinfo
if has_gcinfo then
    is_number(gcinfo(), "function gcinfo")
else
    is_nil(gcinfo, "no gcinfo (removed)")
end

-- getfenv
if has_getfenv then
    is_table(getfenv(0), "function getfenv")
    equals(getfenv(0), _G)
    equals(getfenv(1), _G)
    equals(getfenv(), _G)
    local function f () end
    is_table(getfenv(f))
    equals(getfenv(f), _G)
    is_table(getfenv(print))
    equals(getfenv(print), _G)

    error_matches(function () getfenv(-3) end,
            "^[^:]+:%d+: bad argument #1 to 'getfenv' %(.-level.-%)",
            "function getfenv (negative)")

    error_matches(function () getfenv(12) end,
            "^[^:]+:%d+: bad argument #1 to 'getfenv' %(invalid level%)",
            "function getfenv (too depth)")
else
    is_nil(getfenv, "no getfenv")
end

do -- getmetatable
    equals(getmetatable(true), nil, "boolean has no metatable by default")
    equals(getmetatable(getmetatable), nil, "function has no metatable by default")
    equals(getmetatable(nil), nil, "nil has no metatable by default")
    equals(getmetatable(3.14), nil, "number has no metatable by default")
    equals(getmetatable({}), nil, "table has no metatable by default")
    local co = coroutine.create(function () return 1 end)
    equals(getmetatable(co), nil, "thread has no metatable by default")

    is_table(getmetatable('ABC'), "string has a metatable")
    equals(getmetatable('ABC'), getmetatable('abc'), "string has a shared metatable")
end

do -- ipairs
    local a = {'a','b','c'}
    if has_ipairs53 then
        a = setmetatable({
            [1] = 'a',
            [3] = 'c',
        }, {
            __index = {
               [2] = 'b',
            }
        })
    end
    local f, v, s = ipairs(a)
    is_function(f, "function ipairs")
    is_table(v)
    equals(s, 0)
    s, v = f(a, s)
    equals(s, 1)
    equals(v, 'a')
    s, v = f(a, s)
    equals(s, 2)
    equals(v, 'b')
    s, v = f(a, s)
    equals(s, 3)
    equals(v, 'c')
    s, v = f(a, s)
    equals(s, nil)
    equals(v, nil)
end

do -- load
    local t = { [[
function bar (x)
    return x
end
]] }
    local i = 0
    local function reader ()
        i = i + 1
        return t[i]
    end
    local f, msg = load(reader)
    if msg then
        diag(msg)
    end
    is_function(f, "function load(reader)")
    equals(bar, nil)
    f()
    equals(bar('ok'), 'ok')
    bar = nil

    t = { [[
function baz (x)
    return x
end
]] }
    i = -1
    function reader ()
        i = i + 1
        return t[i]
    end
    f, msg = load(reader)
    if msg then
        diag(msg)
    end
    is_function(f, "function load(pathological reader)")
    f()
    if _VERSION == 'Lua 5.1' and not jit then
        todo("not with 5.1")
    end
    equals(baz, nil)

    t = { [[?syntax error?]] }
    i = 0
    f, msg = load(reader, "errorchunk")
    is_nil(f, "function load(syntax error)")
    matches(msg, "^%[string \"errorchunk\"%]:%d+:")

    f = load(function () return nil end)
    is_function(f, "when reader returns nothing")

    f, msg = load(function () return {} end)
    is_nil(f, "reader function must return a string")
    matches(msg, "reader function must return a string")

    if has_load52 then
        f = load([[
function bar (x)
    return x
end
]])
        equals(bar, nil, "function load(str)")
        f()
        equals(bar('ok'), 'ok')
        bar = nil

        local env = {}
        f = load([[
function bar (x)
    return x
end
]], "from string", 't', env)
        equals(env.bar, nil, "function load(str)")
        f()
        equals(env.bar('ok'), 'ok')

        f, msg = load([[?syntax error?]], "errorchunk")
        is_nil(f, "function load(syntax error)")
        matches(msg, "^%[string \"errorchunk\"%]:%d+:")

        f, msg = load([[print 'ok']], "chunk txt", 'b')
        matches(msg, "attempt to load")
        is_nil(f, "mode")

        f, msg = load("\x1bLua", "chunk bin", 't')
        matches(msg, "attempt to load")
        is_nil(f, "mode")
    else
       diag("no load with string")
    end
end

do -- loadfile
    local f = io.open('foo-301.lua', 'w')
    if _VERSION ~= 'Lua 5.1' or jit then
        f:write'\xEF\xBB\xBF' -- BOM
    end
    f:write[[
function foo (x)
    return x
end
]]
    f:close()
    f = loadfile('foo-301.lua')
    equals(foo, nil, "function loadfile")
    f()
    equals(foo('ok'), 'ok')

    if has_loadfile52 then
        local msg
        f, msg = loadfile('foo-301.lua', 'b')
        matches(msg, "attempt to load")
        is_nil(f, "mode")

        local env = {}
        f = loadfile('foo-301.lua', 't', env)
        equals(env.foo, nil, "function loadfile")
        f()
        equals(env.foo('ok'), 'ok')
    else
        diag("no loadfile with mode & env")
    end

    os.remove('foo-301.lua') -- clean up

    local msg
    f, msg = loadfile('no_file-301.lua')
    is_nil(f, "function loadfile (no file)")
    equals(msg, "cannot open no_file-301.lua: No such file or directory")

    f = io.open('foo-301.lua', 'w')
    f:write[[?syntax error?]]
    f:close()
    f, msg = loadfile('foo-301.lua')
    is_nil(f, "function loadfile (syntax error)")
    matches(msg, '^foo%-301%.lua:%d+:')
    os.remove('foo-301.lua') -- clean up
end

-- loadstring
if has_loadstring then
    local f = loadstring([[i = i + 1]])
    i = 0
    f()
    equals(i, 1, "function loadstring")
    f()
    equals(i, 2)

    i = 32
    local i = 0
    f = loadstring([[i = i + 1; return i]])
    local g = function () i = i + 1; return i end
    equals(f(), 33, "function loadstring")
    equals(g(), 1)

    local msg
    f, msg = loadstring([[?syntax error?]])
    is_nil(f, "function loadstring (syntax error)")
    matches(msg, '^%[string "%?syntax error%?"%]:%d+:')
elseif has_alias_loadstring then
    equals(loadstring, load, "alias loadstring")
else
    is_nil(loadstring, "no loadstring")
end

-- newproxy
if has_newproxy then
    local proxy = newproxy(false)
    is_userdata(proxy, "function newproxy(false)")
    is_nil(getmetatable(proxy), "without metatable")
    proxy = newproxy(true)
    is_userdata(proxy, "function newproxy(true)")
    is_table(getmetatable(proxy), "with metatable")

    local proxy2 = newproxy(proxy)
    is_userdata(proxy, "function newproxy(proxy)")
    equals(getmetatable(proxy2), getmetatable(proxy))

    error_matches(function () newproxy({}) end,
            "^[^:]+:%d+: bad argument #1 to 'newproxy' %(boolean or proxy expected%)",
            "function newproxy({})")
else
    is_nil(newproxy, "no newproxy")
end

do -- next
    local t = {'a','b','c'}
    local a = next(t, nil)
    equals(a, 1, "function next (array)")
    a = next(t, 1)
    equals(a, 2)
    a = next(t, 2)
    equals(a, 3)
    a = next(t, 3)
    equals(a, nil)

    error_matches(function () a = next() end,
            "^[^:]+:%d+: bad argument #1 to 'next' %(table expected, got no value%)",
            "function next (no arg)")

    error_matches(function () a = next(t, 6) end,
            "invalid key to 'next'",
            "function next (invalid key)")

    t = {'a','b','c'}
    a = next(t, 2)
    equals(a, 3, "function next (unorderer)")
    a = next(t, 1)
    equals(a, 2)
    a = next(t, 3)
    equals(a, nil)

    t = {}
    a = next(t, nil)
    equals(a, nil, "function next (empty table)")
end

do -- pairs
    local a = {'a','b','c'}
    local f, v, s = pairs(a)
    is_function(f, "function pairs")
    is_table(v)
    equals(s, nil)
    s = f(v, s)
    equals(s, 1)
    s = f(v, s)
    equals(s, 2)
    s = f(v, s)
    equals(s, 3)
    s = f(v, s)
    equals(s, nil)
end

do -- pcall
    local status, result = pcall(assert, 1)
    is_true(status, "function pcall")
    equals(result, 1)
    status, result = pcall(assert, false, 'catched')
    is_false(status)
    equals(result, 'catched')
    status = pcall(assert)
    is_false(status)
end

do -- rawequal
    local t = {}
    local a = t
    is_true(rawequal(nil, nil), "function rawequal -> true")
    is_true(rawequal(false, false))
    is_true(rawequal(3, 3))
    is_true(rawequal('text', 'text'))
    is_true(rawequal(t, a))
    is_true(rawequal(print, print))

    is_false(rawequal(nil, 2), "function rawequal -> false")
    is_false(rawequal(false, true))
    is_false(rawequal(false, 2))
    is_false(rawequal(3, 2))
    is_false(rawequal(3, '2'))
    is_false(rawequal('text', '2'))
    is_false(rawequal('text', 2))
    is_false(rawequal(t, {}))
    is_false(rawequal(t, 2))
    is_false(rawequal(print, type))
    is_false(rawequal(print, 2))
end

-- rawlen
if has_rawlen then
    equals(rawlen("text"), 4, "function rawlen (string)")
    equals(rawlen({ 'a', 'b', 'c'}), 3, "function rawlen (table)")
    error_matches(function () local a = rawlen(true) end,
            "^[^:]+:%d+: bad argument #1 to 'rawlen' %(table ",
            "function rawlen (bad arg)")
else
    is_nil(rawlen, "no rawlen")
end

do -- rawget
    local t = {a = 'letter a', b = 'letter b'}
    equals(rawget(t, 'a'), 'letter a', "function rawget")
end

do -- rawset
    local t = {}
    equals(rawset(t, 'a', 'letter a'), t, "function rawset")
    equals(t.a, 'letter a')

    error_matches(function () t = {}; rawset(t, nil, 42) end,
            "^table index is nil",
            "function rawset (table index is nil)")
end

do -- select
    equals(select('#'), 0, "function select")
    equals(select('#','a','b','c'), 3)
    array_equals({select(1,'a','b','c')}, {'a','b','c'})
    array_equals({select(3,'a','b','c')}, {'c'})
    array_equals({select(5,'a','b','c')}, {})
    array_equals({select(-1,'a','b','c')}, {'c'})
    array_equals({select(-2,'a','b','c')}, {'b', 'c'})
    array_equals({select(-3,'a','b','c')}, {'a', 'b', 'c'})

    error_matches(function () select(0,'a','b','c') end,
            "^[^:]+:%d+: bad argument #1 to 'select' %(index out of range%)",
            "function select (out of range)")

    error_matches(function () select(-4,'a','b','c') end,
            "^[^:]+:%d+: bad argument #1 to 'select' %(index out of range%)",
            "function select (out of range)")
end

-- setfenv
if has_getfenv then
    local t = {}
    local function f () end
    equals(setfenv(f, t), f, "function setfenv")
    is_table(getfenv(f))
    equals(getfenv(f), t)

    save = getfenv(1)
    a = 1
    setfenv(1, {g = _G})
    g.equals(a, nil, "function setfenv")
    g.equals(g.a, 1)
    g.setfenv(1, g.save) -- restore

    save = getfenv(1)
    a = 1
    local newgt = {}        -- create new environment
    setmetatable(newgt, {__index = _G})
    setfenv(1, newgt)       -- set it
    equals(a, 1, "function setfenv")
    a = 10
    equals(a, 10)
    equals(_G.a, 1)
    _G.a = 20
    equals(_G.a, 20)
    setfenv(1, save) -- restore

    save = getfenv(1)
    local function factory ()
        return function ()
                   return a    -- "global" a
               end
    end
    a = 3
    local f1 = factory()
    local f2 = factory()
    equals(f1(), 3, "function setfenv")
    equals(f2(), 3)
    setfenv(f1, {a = 10})
    equals(f1(), 10)
    equals(f2(), 3)
    setfenv(1, save) -- restore

    equals(setfenv(0, _G), nil, "function setfenv(0)")

    error_matches(function () setfenv(-3, {}) end,
            "^[^:]+:%d+: bad argument #1 to 'setfenv' %(.-level.-%)",
            "function setfenv (negative)")

    error_matches(function () setfenv(12, {}) end,
            "^[^:]+:%d+: bad argument #1 to 'setfenv' %(invalid level%)",
            "function setfenv (too depth)")

    t = {}
    error_matches(function () setfenv(t, {}) end,
            "^[^:]+:%d+: bad argument #1 to 'setfenv' %(number expected, got table%)",
            "function setfenv (bad arg)")

    error_matches(function () setfenv(print, {}) end,
            "^[^:]+:%d+: 'setfenv' cannot change environment of given object",
            "function setfenv (forbidden)")
else
    is_nil(setfenv, "no setfenv")
end

do -- setmetatable
    local mt = {}
    local t = {}
    equals(t, setmetatable(t, mt), "setmetatable")
    equals(getmetatable(t), mt)
    equals(t, setmetatable(t, nil))
    equals(getmetatable(t), nil)

    error_matches(function () setmetatable(t, true) end,
            "^[^:]+:%d+: bad argument #2 to 'setmetatable' %(nil or table expected",
            "function setmetatable (bad arg)")
    error_matches(function () setmetatable(true, mt) end,
            "^[^:]+:%d+: bad argument #1 to 'setmetatable' %(table expected, got boolean%)",
            "function setmetatable (bad arg)")
end

do -- type
    equals(type("Hello world"), 'string', "function type")
    equals(type(10.4*3), 'number')
    equals(type(print), 'function')
    equals(type(type), 'function')
    equals(type(true), 'boolean')
    equals(type(nil), 'nil')
    equals(type(io.stdin), 'userdata')
    equals(type(type(X)), 'string')

    local a = nil
    equals(type(a), 'nil', "function type")
    a = 10
    equals(type(a), 'number')
    a = "a string!!"
    equals(type(a), 'string')
    a = print
    equals(type(a), 'function')
    equals(type(function () end), 'function')

    error_matches(function () type() end,
            "^[^:]+:%d+: bad argument #1 to 'type' %(value expected%)",
            "function type (no arg)")
end

do -- tonumber
    equals(tonumber('text12'), nil, "function tonumber")
    equals(tonumber('12text'), nil)
    equals(tonumber(3.14), 3.14)
    equals(tonumber('3.14'), 3.14)
    equals(tonumber('  3.14  '), 3.14)
    equals(tonumber(tostring(111), 2), 7)
    equals(tonumber('111', 2), 7)
    equals(tonumber('  111  ', 2), 7)
    local a = {}
    equals(tonumber(a), nil)

    error_matches(function () tonumber() end,
            "^[^:]+:%d+: bad argument #1 to 'tonumber' %(value expected%)",
            "function tonumber (no arg)")

    error_matches(function () tonumber('111', 200) end,
            "^[^:]+:%d+: bad argument #2 to 'tonumber' %(base out of range%)",
            "function tonumber (bad base)")
end

do -- tostring
    equals(tostring('text'), 'text', "function tostring")
    equals(tostring(3.14), '3.14')
    equals(tostring(nil), 'nil')
    equals(tostring(true), 'true')
    equals(tostring(false), 'false')
    matches(tostring({}), '^table: 0?[Xx]?%x+$')
    matches(tostring(print), '^function: 0?[Xx]?[builtin]*#?%x+$')

    error_matches(function () tostring() end,
            "^[^:]+:%d+: bad argument #1 to 'tostring' %(value expected%)",
            "function tostring (no arg)")
end

-- unpack
if has_unpack then
    array_equals({unpack({})}, {}, "function unpack")
    array_equals({unpack({'a'})}, {'a'})
    array_equals({unpack({'a','b','c'})}, {'a','b','c'})
    array_equals({(unpack({'a','b','c'}))}, {'a'})
    array_equals({unpack({'a','b','c','d','e'},2,4)}, {'b','c','d'})
    array_equals({unpack({'a','b','c'},2,4)}, {'b','c'})
elseif has_alias_unpack then
    equals(unpack, table.unpack, "alias unpack")
else
    is_nil(unpack, "no unpack")
end

-- warn
if has_warn then
    equals(warn('foo'), nil, "function warn")

    local r, f = pcall(io.popen, lua .. [[ -W -e "warn'foo'" 2>&1]])
    if r then
        equals(f:read'*l', 'Lua warning: foo', "warn called with popen")
        equals(f:read'*l', nil)
        equals(f:close(), true)
    else
        diag("io.popen not supported")
    end

    r, f = pcall(io.popen, lua .. [[ -e "warn'@on'; warn'foo'" 2>&1]])
    if r then
        equals(f:read'*l', 'Lua warning: foo', "warn called with popen")
        equals(f:read'*l', nil)
        equals(f:close(), true)
    else
        diag("io.popen not supported")
    end

    r, f = pcall(io.popen, lua .. [[ -e "warn'@on'; warn('foo', 'bar')" 2>&1]])
    if r then
        equals(f:read'*l', 'Lua warning: foobar', "warn called with popen")
        equals(f:read'*l', nil)
        equals(f:close(), true)
    else
        diag("io.popen not supported")
    end

    error_matches(function () warn('foo', warn) end,
            "^[^:]+:%d+: bad argument #2 to 'warn' %(string expected, got function%)",
            "function warn (no arg)")

    error_matches(function () warn() end,
            "^[^:]+:%d+: bad argument #1 to 'warn' %(string expected, got no value%)",
            "function warn (no arg)")
else
    is_nil(warn, "no warn")
end

do -- xpcall
    local function err (obj)
        return obj
    end

    local function backtrace ()
        return 'not a back trace'
    end

    local status, result = xpcall(function () return assert(1) end, err)
    is_true(status, "function xpcall")
    equals(result, 1)
    status, result = xpcall(function () return assert(false, 'catched') end, err)
    is_false(status)
    if jit then
        equals(result, 'catched')
    else
        matches(result, ':%d+: catched')
    end
    status, result = xpcall(function () return assert(false, 'catched') end, backtrace)
    is_false(status)
    equals(result, 'not a back trace')

    if has_xpcall52 then
        status, result = xpcall(assert, err, 1)
        is_true(status, "function xpcall with args")
        equals(result, 1)
        status, result = xpcall(assert, err, false, 'catched')
        is_false(status)
        equals(result, 'catched')
        status, result = xpcall(assert, backtrace, false, 'catched')
        is_false(status)
        equals(result, 'not a back trace')
    end

    error_matches(function () xpcall(assert) end,
            "bad argument #2 to 'xpcall' %(.-",
            "function xpcall")

    if has_xpcall53 then
        error_matches(function () xpcall(assert, 1) end,
                "bad argument #2 to 'xpcall' %(function expected, got number%)",
                "function xpcall")
    else
        is_false(xpcall(assert, nil), "function xpcall")
    end
end

if jit and pcall(require, 'ffi') then
    _dofile'lexicojit/basic.t'
end

done_testing()

-- Local Variables:
--   mode: lua
--   lua-indent-level: 4
--   fill-column: 100
-- End:
-- vim: ft=lua expandtab shiftwidth=4:
