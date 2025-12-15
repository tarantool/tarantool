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

=head1 Lua closures

=head2 Synopsis

    % prove 213-closure.t

=head2 Description

See section "Closures" in "Programming in Lua".

=cut

--]]

require'test_assertion'

plan(15)

do --[[ inc ]]
    local counter = 0

    local function inc (x)
        counter = counter + x
        return counter
    end

    equals(inc(1), 1, "inc")
    equals(inc(2), 3)
end

do --[[ newCounter ]]
    local function newCounter ()
        local i = 0
        return function ()  -- anonymous function
                   i = i + 1
                   return i
               end
    end

    local c1 = newCounter()
    equals(c1(), 1, "newCounter")
    equals(c1(), 2)

    local c2 = newCounter()
    equals(c2(), 1)
    equals(c1(), 3)
    equals(c2(), 2)
end

do --[[
The loop creates ten closures (that is, ten instances of the anonymous
function). Each of these closures uses a different y variable, while all
of them share the same x.
]]
    local a = {}
    local x = 20
    for i=1,10 do
        local y = 0
        a[i] = function () y=y+1; return x+y end
    end

    equals(a[1](), 21, "ten closures")
    equals(a[1](), 22)
    equals(a[2](), 21)
end

do --[[ add ]]
    local function add(x)
        return function (y) return (x + y) end
    end

    local f = add(2)
    is_function(f, "add")
    equals(f(10), 12)
    local g = add(5)
    equals(g(1), 6)
    equals(g(10), 15)
    equals(f(1), 3)
end

-- Local Variables:
--   mode: lua
--   lua-indent-level: 4
--   fill-column: 100
-- End:
-- vim: ft=lua expandtab shiftwidth=4:
