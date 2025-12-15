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

=head1 Lua metatables

=head2 Synopsis

    % prove 231-metatable.t

=head2 Description

See section "Metatables and Metamethods" in "Reference Manual"
L<https://www.lua.org/manual/5.1/manual.html#2.8>,
L<https://www.lua.org/manual/5.2/manual.html#2.4>,
L<https://www.lua.org/manual/5.3/manual.html#2.4>,
L<https://www.lua.org/manual/5.4/manual.html#2.4>

See section "Metatables and Metamethods" in "Programming in Lua".

=cut

--]]

require'test_assertion'
local profile = require'profile'
local has_metamethod52 = _VERSION >= 'Lua 5.2' or profile.luajit_compat52
local has_metamethod_ipairs = _VERSION == 'Lua 5.2' or profile.compat52 or profile.luajit_compat52
local has_metamethod_le_emulated = _VERSION <= 'Lua 5.3' or profile.compat53
local has_metamethod_pairs = _VERSION >= 'Lua 5.2' or profile.luajit_compat52
local has_metamethod_tostring53 = _VERSION >= 'Lua 5.3'
local has_metamethod_tostring54 = _VERSION >= 'Lua 5.4'
local has_anno_toclose = _VERSION >= 'Lua 5.4'

plan'no_plan'

do
    local t = {}
    equals(getmetatable(t), nil, "metatable")
    local t1 = {}
    equals(setmetatable(t, t1), t)
    equals(getmetatable(t), t1)
    equals(setmetatable(t, nil), t)
    error_matches(function () setmetatable(t, true) end,
            "^[^:]+:%d+: bad argument #2 to 'setmetatable' %(nil or table expected")

    local mt = {}
    mt.__metatable = "not your business"
    setmetatable(t, mt)
    equals(getmetatable(t), "not your business", "protected metatable")
    error_matches(function () setmetatable(t, {}) end,
            "^[^:]+:%d+: cannot change a protected metatable")

    equals(getmetatable('').__index, string, "metatable for string")

    equals(getmetatable(nil), nil, "metatable for nil")
    equals(getmetatable(false), nil, "metatable for boolean")
    equals(getmetatable(2), nil, "metatable for number")
    equals(getmetatable(print), nil, "metatable for function")
end

do
    local t = {}
    local mt = { __tostring=function () return '__TABLE__' end }
    setmetatable(t, mt)
    equals(tostring(t), '__TABLE__', "__tostring")
end

do
    local t = {}
    local mt = {}
    local a = nil
    function mt.__tostring () a = "return nothing" end
    setmetatable(t, mt)
    if has_metamethod_tostring53 then
        error_matches(function () tostring(t) end,
                "^[^:]+:%d+: '__tostring' must return a string")
        equals(a, "return nothing")
        if has_metamethod_tostring54 then
            error_matches(function () print(t) end,
                    "^[^:]+:%d+: '__tostring' must return a string")
        else
            error_equals(function () print(t) end,
                    "'__tostring' must return a string")
        end
    else
        equals(tostring(t), nil, "__tostring no-output")
        equals(a, "return nothing")
        error_matches(function () print(t) end,
                "^[^:]+:%d+: 'tostring' must return a string to 'print'")
    end

    mt.__tostring = function () return '__FIRST__', 2 end
    setmetatable(t, mt)
    equals(tostring(t), '__FIRST__', "__tostring too-many-output")
end

do
    local t = {}
    t.mt = {}
    setmetatable(t, t.mt)
    t.mt.__tostring = "not a function"
    error_matches(function () tostring(t) end,
            "attempt to call",
            "__tostring invalid")
end

do
    local t = {}
    local mt = { __len=function () return 42 end }
    setmetatable(t, mt)
    if has_metamethod52 then
        equals(#t, 42, "__len")
    else
        equals(#t, 0, "__len 5.1")
    end
end

if has_metamethod52 then
    local t = {}
    local mt = { __len=function () return nil end }
    setmetatable(t, mt)
    if jit then
        todo("not with LuaJIT")
    end
    error_matches(function () print(table.concat(t)) end,
            "object length is not a.-er",
            "__len invalid")
end

do
    local t = {}
    local mt = {
        __tostring=function () return 't' end,
        __concat=function (op1, op2)
            return tostring(op1) .. '|' .. tostring(op2)
        end,
    }
    setmetatable(t, mt)
    equals(t .. t .. t ..'end' .. '.', "t|t|t|end.", "__concat")
end

do --[[ Cplx ]]
    local Cplx = {}
    Cplx.mt = {}
    local tointeger = math.tointeger or math.floor

    function Cplx.new (re, im)
        local c = {}
        setmetatable(c, Cplx.mt)
        c.re = tonumber(re)
        if im == nil then
            c.im = 0.0
        else
            c.im = tonumber(im)
        end
        return c
    end

    function Cplx.mt.__tostring (c)
        return '(' .. tostring(tointeger(c.re)) .. ',' .. tostring(tointeger(c.im)) .. ')'
    end

    function Cplx.mt.__add (a, b)
        if type(a) ~= 'table' then
            a = Cplx.new(a, 0)
        end
        if type(b) ~= 'table' then
            b = Cplx.new(b, 0)
        end
        local r = Cplx.new(a.re + b.re, a.im + b.im)
        return r
    end

    local c1 = Cplx.new(1, 3)
    local c2 = Cplx.new(2, -1)

    equals(tostring(c1 + c2), '(3,2)', "cplx __add")
    equals(tostring(c1 + 3), '(4,3)')
    equals(tostring(-2 + c1), '(-1,3)')
    equals(tostring(c1 + '3'), '(4,3)')
    equals(tostring('-2' + c1), '(-1,3)')

    function Cplx.mt.__sub (a, b)
        if type(a) ~= 'table' then
            a = Cplx.new(a, 0)
        end
        if type(b) ~= 'table' then
            b = Cplx.new(b, 0)
        end
        local r = Cplx.new(a.re - b.re, a.im - b.im)
        return r
    end

    equals(tostring(c1 - c2), '(-1,4)', "cplx __sub")
    equals(tostring(c1 - 3), '(-2,3)')
    equals(tostring(-2 - c1), '(-3,-3)')
    equals(tostring(c1 - '3'), '(-2,3)')
    equals(tostring('-2' - c1), '(-3,-3)')

    function Cplx.mt.__mul (a, b)
        if type(a) ~= 'table' then
            a = Cplx.new(a, 0)
        end
        if type(b) ~= 'table' then
            b = Cplx.new(b, 0)
        end
        local r = Cplx.new(a.re*b.re - a.im*b.im,
            a.re*b.im + a.im*b.re)
        return r
    end

    equals(tostring(c1 * c2), '(5,5)', "cplx __mul")
    equals(tostring(c1 * 3), '(3,9)')
    equals(tostring(-2 * c1), '(-2,-6)')
    equals(tostring(c1 * '3'), '(3,9)')
    equals(tostring('-2' * c1), '(-2,-6)')

    function Cplx.mt.__div (a, b)
        if type(a) ~= 'table' then
            a = Cplx.new(a, 0)
        end
        if type(b) ~= 'table' then
            b = Cplx.new(b, 0)
        end
        local n = b.re*b.re + b.im*b.im
        local inv = Cplx.new(b.re/n, b.im/n)
        local r = Cplx.new(a.re*inv.re - a.im*inv.im,
                           a.re*inv.im + a.im*inv.re)
        return r
    end

    c1 = Cplx.new(2, 6)
    c2 = Cplx.new(2, 0)

    equals(tostring(c1 / c2), '(1,3)', "cplx __div")
    equals(tostring(c1 / 2), '(1,3)')
    equals(tostring(-4 / c2), '(-2,0)')
    equals(tostring(c1 / '2'), '(1,3)')
    equals(tostring('-4' / c2), '(-2,0)')

    function Cplx.mt.__unm (a)
        if type(a) ~= 'table' then
            a = Cplx.new(a, 0)
        end
        local r = Cplx.new(-a.re, -a.im)
        return r
    end

    c1 = Cplx.new(1, 3)
    equals(tostring(- c1), '(-1,-3)', "cplx __unm")

    function Cplx.mt.__len (a)
        return math.sqrt(a.re*a.re + a.im*a.im)
    end

    c1 = Cplx.new(3, 4)
    if has_metamethod52 then
        equals( #c1, 5, "cplx __len")
    else
        equals( #c1, 0, "__len 5.1")
    end

    function Cplx.mt.__eq (a, b)
        if type(a) ~= 'table' then
            a = Cplx.new(a, 0)
        end
        if type(b) ~= 'table' then
            b = Cplx.new(b, 0)
        end
        return (a.re == b.re) and (b.im == b.im)
    end

    c1 = Cplx.new(2, 0)
    c2 = Cplx.new(1, 3)
    local c3 = Cplx.new(2, 0)

    equals(c1 ~= c2, true, "cplx __eq")
    equals(c1 == c3, true)
    equals(c1 == 2, false)
    equals(Cplx.mt.__eq(c1, 2), true)

    function Cplx.mt.__lt (a, b)
        if type(a) ~= 'table' then
            a = Cplx.new(a, 0)
        end
        if type(b) ~= 'table' then
            b = Cplx.new(b, 0)
        end
        local ra = a.re*a.re + a.im*a.im
        local rb = b.re*b.re + b.im*b.im
        return ra < rb
    end

    equals(c1 < c2, true, "cplx __lt")
    equals(c1 < c3, false)
    if has_metamethod_le_emulated then
        equals(c1 <= c3, true)
    end
    if has_metamethod52 then
        equals(c1 < 1, false)
        equals(c1 < 4, true)
    end

    function Cplx.mt.__le (a, b)
        if type(a) ~= 'table' then
            a = Cplx.new(a, 0)
        end
        if type(b) ~= 'table' then
            b = Cplx.new(b, 0)
        end
        local ra = a.re*a.re + a.im*a.im
        local rb = b.re*b.re + b.im*b.im
        return ra <= rb
    end

    equals(c1 < c2, true, "cplx __lt __le")
    equals(c1 < c3, false)
    equals(c1 <= c3, true)

    local a = nil
    function Cplx.mt.__call (obj)
        a = "Cplx.__call " .. tostring(obj)
        return true
    end

    c1 = Cplx.new(2, 0)
    local r = c1()
    equals(r, true, "cplx __call (without args)")
    equals(a, "Cplx.__call (2,0)")

    function Cplx.mt.__call (obj, ...)
        a = "Cplx.__call " .. tostring(obj) .. ", " .. table.concat({...}, ", ")
        return true
    end

    equals(c1(), true, "cplx __call (with args)")
    equals(a, "Cplx.__call (2,0), ")
    equals(c1('a'), true)
    equals(a, "Cplx.__call (2,0), a")
    equals(c1('a', 'b', 'c'), true)
    equals(a, "Cplx.__call (2,0), a, b, c")
end

--[[ delegate ]]
if has_metamethod_pairs then
    local t = {
        _VALUES = {
            a = 1,
            b = 'text',
            c = true,
        }
    }
    local mt = {
        __pairs = function (op)
            return next, op._VALUES
        end
    }
    setmetatable(t, mt)

    local r = {}
    for k in pairs(t) do
        r[#r+1] = k
    end
    table.sort(r)
    equals( table.concat(r, ','), 'a,b,c', "__pairs" )
end
if has_metamethod_ipairs then
    local t = {
        _VALUES = { 'a', 'b', 'c' }
    }
    local mt = {
        __ipairs = function (op)
            return ipairs(op._VALUES)
        end
    }
    setmetatable(t, mt)

    local r = ''
    for i, v in ipairs(t) do
        r = r .. v
    end
    equals( r, 'abc', "__ipairs" )
end

do --[[ Window ]]
    -- create a namespace
    local Window = {}
    -- create a prototype with default values
    Window.prototype = {x=0, y=0, width=100, heigth=100, }
    -- create a metatable
    Window.mt = {}
    -- declare the constructor function
    function Window.new (o)
        setmetatable(o, Window.mt)
        return o
    end

    Window.mt.__index = function (table, key)
        return Window.prototype[key]
    end

    local w = Window.new{x=10, y=20}
    equals(w.x, 10, "table-access")
    equals(w.width, 100)
    equals(rawget(w, 'x'), 10)
    equals(rawget(w, 'width'), nil)

    Window.mt.__index = Window.prototype  -- just a table
    w = Window.new{x=10, y=20}
    equals(w.x, 10, "table-access")
    equals(w.width, 100)
    equals(rawget(w, 'x'), 10)
    equals(rawget(w, 'width'), nil)
end

do --[[ tables with default values ]]
    local function setDefault_1 (t, d)
        local mt = {__index = function () return d end}
        setmetatable (t, mt)
    end

    local tab = {x=10, y=20}
    equals(tab.x, 10, "tables with default values")
    equals(tab.z, nil)
    setDefault_1(tab, 0)
    equals(tab.x, 10)
    equals(tab.z, 0)
end

do --[[ tables with default values ]]
    local mt = {__index = function (t) return t.___ end}
    local function setDefault_2 (t, d)
        t.___ = d
        setmetatable (t, mt)
    end

    local tab = {x=10, y=20}
    equals(tab.x, 10, "tables with default values")
    equals(tab.z, nil)
    setDefault_2(tab, 0)
    equals(tab.x, 10)
    equals(tab.z, 0)
end

do --[[ tables with default values ]]
    local key = {}
    local mt = {__index = function (t) return t[key] end}
    local function setDefault_3 (t, d)
        t[key] = d
        setmetatable (t, mt)
    end

    local tab = {x=10, y=20}
    equals(tab.x, 10, "tables with default values")
    equals(tab.z, nil)
    setDefault_3(tab, 0)
    equals(tab.x, 10)
    equals(tab.z, 0)
end

do --[[ private access ]]
    local t = {}  -- original table
    -- keep a private access to original table
    local _t = t
    -- create proxy
    t = {}

    local w = nil
    local r = nil
    -- create metatable
    local mt = {
        __index = function (t,k)
            r = "*access to element " .. tostring(k)
            return _t[k]  -- access the original table
        end,

        __newindex = function (t,k,v)
             w = "*update of element " .. tostring(k) ..
                                " to " .. tostring(v)
            _t[k] = v  -- update original table
        end
    }
    setmetatable(t, mt)

    t[2] = 'hello'
    equals(t[2], 'hello', "tracking table accesses")
    equals(w, "*update of element 2 to hello")
    equals(r, "*access to element 2")
end

do --[[ private access ]]
    -- create private index
    local index = {}

    local w = nil
    local r = nil
    -- create metatable
    local mt = {
        __index = function (t,k)
            r = "*access to element " .. tostring(k)
            return t[index][k]  -- access the original table
        end,

        __newindex = function (t,k,v)
            w = "*update of element " .. tostring(k) ..
                               " to " .. tostring(v)
            t[index][k] = v  -- update original table
        end
    }
    local function track (t)
        local proxy = {}
        proxy[index] = t
        setmetatable(proxy, mt)
        return proxy
    end

    local t = {}
    t = track(t)

    t[2] = 'hello'
    equals(t[2], 'hello', "tracking table accesses")
    equals(w, "*update of element 2 to hello")
    equals(r, "*access to element 2")
end

do --[[ read-only table ]]
    local function readOnly (t)
        local proxy = {}
        local mt = {
            __index = t,
            __newindex = function (_t,k,v)
                error("attempt to update a read-only table", 2)
            end
        }
        setmetatable(proxy, mt)
        return proxy
    end

    local days = readOnly{'Sunday', 'Monday', 'Tuesday', 'Wednesday',
                         'Thurday', 'Friday', 'Saturday'}

    equals(days[1], 'Sunday', "read-only tables")

    error_matches(function () days[2] = 'Noday' end,
            "^[^:]+:%d+: attempt to update a read%-only table")
end

do --[[ declare global ]]
    local function declare (name, initval)
        rawset(_G, name, initval or false)
    end

    setmetatable(_G, {
        __newindex = function (_, n)
            error("attempt to write to undeclared variable " .. n, 2)
        end,
        __index = function (_, n)
            error("attempt to read undeclared variable" .. n, 2)
        end,
    })

    error_matches(function () new_a = 1 end,
            "^[^:]+:%d+: attempt to write to undeclared variable new_a",
            "declaring global variables")

    declare 'new_a'
    new_a = 1
    equals(new_a, 1)
end

do
    local newindex = {}
    -- create metatable
    local mt = {
        __newindex = newindex
    }
    local t = setmetatable({}, mt)
    t[1] = 42
    equals(newindex[1], 42, "__newindex")
end

if has_anno_toclose then
    _dofile'lexico54/metatable.t'
end

done_testing()

-- Local Variables:
--   mode: lua
--   lua-indent-level: 4
--   fill-column: 100
-- End:
-- vim: ft=lua expandtab shiftwidth=4:
