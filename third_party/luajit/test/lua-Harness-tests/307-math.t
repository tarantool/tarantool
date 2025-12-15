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

=head1 Lua Mathematic Library

=head2 Synopsis

    % prove 307-math.t

=head2 Description

Tests Lua Mathematic Library

See section "Mathematical Functions" in "Reference Manual"
L<https://www.lua.org/manual/5.1/manual.html#5.6>,
L<https://www.lua.org/manual/5.2/manual.html#6.6>,
L<https://www.lua.org/manual/5.3/manual.html#6.7>,
L<https://www.lua.org/manual/5.4/manual.html#6.7>

=cut

--]]

require'test_assertion'
local profile = require'profile'
local has_integer = _VERSION >= 'Lua 5.3' or (jit and jit.version:match'moonjit') or profile.integer
local has_mathx = _VERSION < 'Lua 5.3' or profile.compat52 or profile.compat53 or profile.has_mathx
local has_log10 = _VERSION < 'Lua 5.2' or profile.compat51 or profile.has_math_log10 or
                  profile.compat52 or profile.compat53 or profile.has_mathx
local has_log_with_base = _VERSION >= 'Lua 5.2' or profile.compat52
local has_mod = profile.has_math_mod or ujit
local nocvts2n = profile.nocvts2n or jit

plan'no_plan'

do -- abs
    equals(math.abs(-12.34), 12.34, "function abs (float)")
    equals(math.abs(12.34), 12.34)
    if math.type then
        equals(math.type(math.abs(-12.34)), 'float')
    end
    equals(math.abs(-12), 12, "function abs (integer)")
    equals(math.abs(12), 12)
    if math.type then
        equals(math.type(math.abs(-12)), 'integer')
    end
end

do -- acos
    near(math.acos(0.5), 1.047, 0.001, "function acos")
end

do -- asin
    near(math.asin(0.5), 0.523, 0.001, "function asin")
end

do -- atan
    near(math.atan(0.5), 0.463, 0.001, "function atan")
end

-- atan2
if has_mathx then
    near(math.atan2(1.0, 2.0), 0.463, 0.001, "function atan2")
else
    is_nil(math.atan2, "function atan2 (removed)")
end

do -- ceil
    equals(math.ceil(12.34), 13, "function ceil")
    equals(math.ceil(-12.34), -12)
    equals(math.ceil(-12), -12)
    if math.type then
        equals(math.type(math.ceil(-12.34)), 'integer')
    end
end

do -- cos
    near(math.cos(1.0), 0.540, 0.001, "function cos")
end

-- cosh
if has_mathx then
    near(math.cosh(1.0), 1.543, 0.001, "function cosh")
else
    is_nil(math.cosh, "function cosh (removed)")
end

do -- deg
    equals(math.deg(math.pi), 180, "function deg")
end

do -- exp
    near(math.exp(1.0), 2.718, 0.001, "function exp")
end

do -- floor
    equals(math.floor(12.34), 12, "function floor")
    equals(math.floor(-12.34), -13)
    equals(math.floor(-12), -12)
    if math.type then
        equals(math.type(math.floor(-12.34)), 'integer')
    end
end

do -- fmod
    near(math.fmod(7.0001, 0.3), 0.100, 0.001, "function fmod (float)")
    near(math.fmod(-7.0001, 0.3), -0.100, 0.001)
    near(math.fmod(-7.0001, -0.3), -0.100, 0.001)
    if math.type then
        equals(math.type(math.fmod(7.0, 0.3)), 'float')
    end
    equals(math.fmod(7, 3), 1, "function fmod (integer)")
    equals(math.fmod(-7, 3), -1)
    equals(math.fmod(-7, -1), 0)
    if math.type then
        equals(math.type(math.fmod(7, 3)), 'integer')
    end
    if _VERSION >= 'Lua 5.3' then
        error_matches(function () math.fmod(7, 0) end,
                "^[^:]+:%d+: bad argument #2 to 'fmod' %(zero%)",
                "function fmod 0")
    else
        diag"fmod by zero -> nan"
    end
end

-- frexp
if has_mathx then
    array_equals({math.frexp(1.5)}, {0.75, 1}, "function frexp")
else
    is_nil(math.frexp, "function frexp (removed)")
end

do -- huge
    is_number(math.huge, "variable huge")
    if math.type then
        equals(math.type(math.huge), 'float')
    end
end

-- ldexp
if has_mathx then
    equals(math.ldexp(1.2, 3), 9.6, "function ldexp")
else
    is_nil(math.ldexp, "function ldexp (removed)")
end

do -- log
    near(math.log(47), 3.85, 0.01, "function log")
    if has_log_with_base then
        near(math.log(47, math.exp(1)), 3.85, 0.01, "function log (base e)")
        near(math.log(47, 2), 5.554, 0.001, "function log (base 2)")
        near(math.log(47, 10), 1.672, 0.001, "function log (base 10)")
    end
end

-- log10
if has_log10 then
    near(math.log10(47.0), 1.672, 0.001, "function log10")
else
    is_nil(math.log10, "function log10 (removed)")
end

do --max
    equals(math.max(1), 1, "function max")
    equals(math.max(1, 2), 2)
    equals(math.max(1, 2, 3, -4), 3)

    error_matches(function () math.max() end,
            "^[^:]+:%d+: bad argument #1 to 'max' %(.- expected",
            "function max 0")
end

-- maxinteger
if has_integer then
    is_number(math.maxinteger, "variable maxinteger")
    if math.type then
        equals(math.type(math.maxinteger), 'integer')
    end
else
    is_nil(math.maxinteger, "no maxinteger")
end

do --min
    equals(math.min(1), 1, "function min")
    equals(math.min(1, 2), 1)
    equals(math.min(1, 2, 3, -4), -4)

    error_matches(function () math.min() end,
            "^[^:]+:%d+: bad argument #1 to 'min' %(.- expected",
            "function min 0")
end

-- mininteger
if has_integer then
    is_number(math.mininteger, "variable mininteger")
    if math.type then
        equals(math.type(math.mininteger), 'integer')
    end
else
    is_nil(math.mininteger, "no mininteger")
end

-- mod (compat50)
if has_mod then
    equals(math.mod, math.fmod, "function mod (alias fmod)")
else
    is_nil(math.mod, "function mod (alias removed)")
end

do -- modf
    array_equals({math.modf(2.25)}, {2, 0.25}, "function modf")
    array_equals({math.modf(2)}, {2, 0.0})
end

do -- pi
    near(math.pi, 3.14, 0.01, "variable pi")
end

-- pow
if has_mathx then
    equals(math.pow(-2, 3), -8, "function pow")
else
    is_nil(math.pow, "function pow (removed)")
end

do -- rad
    near(math.rad(180), 3.14, 0.01, "function rad")
end

do -- random
    matches(math.random(), '^0%.%d+', "function random no arg")
    if math.type then
        equals(math.type(math.random()), 'float')
    end
    matches(math.random(9), '^%d$', "function random 1 arg")
    if math.type then
        equals(math.type(math.random(9)), 'integer')
    end
    matches(math.random(10, 19), '^1%d$', "function random 2 arg")
    if math.type then
        equals(math.type(math.random(10, 19)), 'integer')
    end
    matches(math.random(-19, -10), '^-1%d$', "function random 2 arg")

    if _VERSION >= 'Lua 5.4' then
        matches(math.random(0), '^%-?%d+$', "function random 0")
    else
        if jit then
            todo("LuaJIT intentional. Don't check empty interval.")
        end
        error_matches(function () math.random(0) end,
                "^[^:]+:%d+: bad argument #1 to 'random' %(interval is empty%)",
                "function random empty interval")
    end

    if jit then
        todo("LuaJIT intentional. Don't check empty interval.", 2)
    end
    error_matches(function () math.random(-9) end,
            "^[^:]+:%d+: bad argument #%d to 'random' %(interval is empty%)",
            "function random empty interval")

    error_matches(function () math.random(19, 10) end,
            "^[^:]+:%d+: bad argument #%d to 'random' %(interval is empty%)",
            "function random empty interval")

    if jit then
        todo("LuaJIT intentional. Don't care about extra arguments.")
    end
    error_matches(function () math.random(1, 2, 3) end,
            "^[^:]+:%d+: wrong number of arguments",
            "function random too many arg")
end

do -- randomseed
    math.randomseed(42)
    local a = math.random()
    math.randomseed(42)
    local b = math.random()
    equals(a, b, "function randomseed")
end

do -- sin
    near(math.sin(1.0), 0.841, 0.001, "function sin")
end

-- sinh
if has_mathx then
    near(math.sinh(1), 1.175, 0.001, "function sinh")
else
    is_nil(math.sinh, "function sinh (removed)")
end

do -- sqrt
    near(math.sqrt(2), 1.414, 0.001, "function sqrt")
end

do -- tan
    near(math.tan(1.0), 1.557, 0.001, "function tan")
end

-- tanh
if has_mathx then
    near(math.tanh(1), 0.761, 0.001, "function tanh")
else
    is_nil(math.tanh, "function tanh (removed)")
end

-- tointeger
if has_integer then
    equals(math.tointeger(-12), -12, "function tointeger (number)")
    equals(math.tointeger(-12.0), -12)
    equals(math.tointeger(-12.34), nil)
    if nocvts2n then
        equals(math.tointeger('-12'), nil, "function tointeger (string)")
        equals(math.tointeger('-12.0'), nil)
    else
        equals(math.tointeger('-12'), -12, "function tointeger (string)")
        equals(math.tointeger('-12.0'), -12)
    end
    equals(math.tointeger('-12.34'), nil)
    equals(math.tointeger('bad'), nil)
    equals(math.tointeger(true), nil, "function tointeger (boolean)")
    equals(math.tointeger({}), nil, "function tointeger (table)")
else
    is_nil(math.tointeger, "no math.tointeger")
end

-- type
if has_integer then
    equals(math.type(3), 'integer', "function type")
    equals(math.type(3.14), 'float')
    equals(math.type('3.14'), nil)
else
    is_nil(math.type, "no math.type")
end

-- ult
if has_integer then
    equals(math.ult(2, 3), true, "function ult")
    equals(math.ult(2, 2), false)
    equals(math.ult(2, 1), false)

    error_matches(function () math.ult(3.14) end,
            "^%S+ bad argument #1 to 'ult' %(number has no integer representation%)",
            "function ult (float)")
    error_matches(function () math.ult(2, 3.14) end,
            "^%S+ bad argument #2 to 'ult' %(number has no integer representation%)")
    error_matches(function () math.ult(true) end,
            "^[^:]+:%d+: bad argument #1 to 'ult' %(number expected, got boolean%)",
            "function ult (boolean)")
    error_matches(function () math.ult(2, true) end,
            "^[^:]+:%d+: bad argument #2 to 'ult' %(number expected, got boolean%)")
else
    is_nil(math.ult, "no math.ult")
end

done_testing()

-- Local Variables:
--   mode: lua
--   lua-indent-level: 4
--   fill-column: 100
-- End:
-- vim: ft=lua expandtab shiftwidth=4:
