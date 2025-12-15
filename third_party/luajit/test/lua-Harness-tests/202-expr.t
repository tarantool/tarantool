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

=head1 Lua expression

=head2 Synopsis

    % prove 202-expr.t

=head2 Description

See section "Expressions" in "Reference Manual"
L<https://www.lua.org/manual/5.1/manual.html#2.5>,
L<https://www.lua.org/manual/5.2/manual.html#3.4>,
L<https://www.lua.org/manual/5.3/manual.html#3.4>,
L<https://www.lua.org/manual/5.4/manual.html#3.4>

=cut

--]]

require'test_assertion'
local profile = require'profile'
local nocvtn2s = profile.nocvtn2s
local nocvts2n = profile.nocvts2n

plan'no_plan'

local x = math.pi
equals(tostring(x - x%0.0001), tostring(3.1415), "modulo")

local a = {}; a.x = 1; a.y = 0;
local b = {}; b.x = 1; b.y = 0;
local c = a
equals(a == c, true, "relational op (by reference)")
equals(a ~= b, true)

equals('0' == 0, false, "relational op")
equals(2 < 15, true)
equals('2' < '15', false)

error_matches(function () return 2 < '15' end,
        "compare",
        "relational op")

error_matches(function () return '2' < 15 end,
        "compare",
        "relational op")

equals(4 and 5, 5, "logical op")
equals(nil and 13, nil)
equals(false and 13, false)
equals(4 or 5, 4)
equals(false or 5, 5)
equals(false or 'text', 'text')

equals(10 or 20, 10, "logical op")
equals(10 or error(), 10)
equals(nil or 'a', 'a')
equals(nil and 10, nil)
equals(false and error(), false)
equals(false and nil, false)
equals(false or nil, nil)
equals(10 and 20, 20)

equals(not nil, true, "logical not")
equals(not false, true)
equals(not 0, false)
equals(not not nil, false)
equals(not 'text', false)
a = {}
equals(not a, false)
equals(not (a == a), false)
equals(not (a ~= a), true)

equals("Hello " .. "World", "Hello World", "concatenation")
if not nocvtn2s then
    equals(0 .. 1, '01')
end
a = "Hello"
equals(a .. " World", "Hello World")
equals(a, "Hello")

if not nocvts2n then
    equals('10' + 1, 11, "coercion")
    equals('-5.3' * '2', -10.6)
end
equals(tostring(10), '10')
if not nocvtn2s then
    equals(10 .. 20, '1020')
    equals(10 .. '', '10')
end

error_matches(function () return 'hello' + 1 end,
        ((not nocvts2n and _VERSION >= 'Lua 5.4') or ravi) and "attempt to add" or "perform arithmetic",
        "no coercion")

error_matches(function ()
                local function first() return end
                local function limit() return 2 end
                local function step()  return 1 end
                for i = first(), limit(), step() do
                    print(i)
                end
        end,
        "^[^:]+:%d+:.- 'for' initial value",
        "for tonumber")

error_matches(function ()
                local function first() return 1 end
                local function limit() return end
                local function step()  return 2 end
                for i = first(), limit(), step() do
                    print(i)
                end
        end,
        "^[^:]+:%d+:.- 'for' limit",
        "for tonumber")

error_matches(function ()
                local function first() return 1 end
                local function limit() return 2 end
                local function step()  return end
                for i = first(), limit(), step() do
                    print(i)
                end
        end,
        "^[^:]+:%d+:.- 'for' step",
        "for tonumber")

if _VERSION >= 'Lua 5.4' then
    error_matches(function ()
                    for i = 1, 10, 0 do
                        print(i)
                    end
            end,
            "^[^:]+:%d+: 'for' step is zero",
            "for step zero")
end

done_testing()

-- Local Variables:
--   mode: lua
--   lua-indent-level: 4
--   fill-column: 100
-- End:
-- vim: ft=lua expandtab shiftwidth=4:
