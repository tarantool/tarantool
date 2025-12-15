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

=head1 Lua functions

=head2 Synopsis

    % prove 212-function.t

=head2 Description

See section "Function Definitions" in "Reference Manual"
L<https://www.lua.org/manual/5.1/manual.html#2.5.9>,
L<https://www.lua.org/manual/5.2/manual.html#3.4.10>,
L<https://www.lua.org/manual/5.3/manual.html#3.4.11>,
L<https://www.lua.org/manual/5.4/manual.html#3.4.11>

See section "Functions" in "Programming in Lua".

=cut

--]]

require'test_assertion'
local loadstring = loadstring or load

plan(68)

do --[[ add ]]
    local function add (a)
        local sum = 0
        for i,v in ipairs(a) do
            sum = sum + v
        end
        return sum
    end

    local t = { 10, 20, 30, 40 }
    equals(add(t), 100, "add")
end

do --[[ f ]]
    local function f(a, b) return a or b end

    equals(f(3), 3, "f")
    equals(f(3, 4), 3)
    equals(f(3, 4, 5), 3)
end

do --[[ incCount ]]
    local count = 0

    local function incCount (n)
        n = n or 1
        count = count + n
    end

    equals(count, 0, "inCount")
    incCount()
    equals(count, 1)
    incCount(2)
    equals(count, 3)
    incCount(1)
    equals(count, 4)
end

do --[[ maximum ]]
    local function maximum (a)
        local mi = 1                -- maximum index
        local m = a[mi]             -- maximum value
        for i,val in ipairs(a) do
            if val > m then
                mi = i
                m = val
            end
        end
        return m, mi
    end

    local m, mi = maximum({8,10,23,12,5})
    equals(m, 23, "maximum")
    equals(mi, 3)
end

do --[[ call by value ]]
    local function f (n)
        n = n - 1
        return n
    end

    local a = 12
    equals(a, 12, "call by value")
    local b = f(a)
    equals(b, 11)
    equals(a, 12)
    local c = f(12)
    equals(c, 11)
    equals(a, 12)
end

do --[[ call by ref ]]
    local function f (t)
        t[#t+1] = 'end'
        return t
    end

    local a = { 'a', 'b', 'c' }
    equals(table.concat(a, ','), 'a,b,c', "call by ref")
    local b = f(a)
    equals(table.concat(b, ','), 'a,b,c,end')
    equals(table.concat(a, ','), 'a,b,c,end')
end

do --[[ var args ]]
    local function g1(a, b, ...)
        local arg = {...}
        equals(a, 3, "vararg")
        equals(b, nil)
        equals(#arg, 0)
        equals(arg[1], nil)
    end
    g1(3)

    local function g2(a, b, ...)
        local arg = {...}
        equals(a, 3)
        equals(b, 4)
        equals(#arg, 0)
        equals(arg[1], nil)
    end
    g2(3, 4)

    local function g3(a, b, ...)
        local arg = {...}
        equals(a, 3)
        equals(b, 4)
        equals(#arg, 2)
        equals(arg[1], 5)
        equals(arg[2], 8)
    end
    g3(3, 4, 5, 8)
end

do --[[ var args ]]
    local function g1(a, b, ...)
        local c, d, e = ...
        equals(a, 3, "var args")
        equals(b, nil)
        equals(c, nil)
        equals(d, nil)
        equals(e, nil)
    end
    g1(3)

    local function g2(a, b, ...)
        local c, d, e = ...
        equals(a, 3)
        equals(b, 4)
        equals(c, nil)
        equals(d, nil)
        equals(e, nil)
    end
    g2(3, 4)

    local function g3(a, b, ...)
        local c, d, e = ...
        equals(a, 3)
        equals(b, 4)
        equals(c, 5)
        equals(d, 8)
        equals(e, nil)
    end
    g3(3, 4, 5, 8)
end

do --[[ var args ]]
    local function g1(a, b, ...)
        equals(#{a, b, ...}, 1, "varargs")
    end
    g1(3)

    local function g2(a, b, ...)
        equals(#{a, b, ...}, 2)
    end
    g2(3, 4)

    local function g3(a, b, ...)
        equals(#{a, b, ...}, 4)
    end
    g3(3, 4, 5, 8)
end

do --[[ var args ]]
    local function f() return 1, 2 end
    local function g() return 'a', f() end
    local function h() return f(), 'b' end
    local function k() return 'c', (f()) end

    local x, y = f()
    equals(x, 1, "var args")
    equals(y, 2)
    local z
    x, y, z = g()
    equals(x, 'a')
    equals(y, 1)
    equals(z, 2)
    x, y = h()
    equals(x, 1)
    equals(y, 'b')
    x, y, z = k()
    equals(x, 'c')
    equals(y, 1)
    equals(z, nil)
end

do --[[ invalid var args ]]
    local f, msg = loadstring [[
function f ()
    print(...)
end
]]
    matches(msg, "^[^:]+:%d+: cannot use '...' outside a vararg function", "invalid var args")
end

do --[[ tail call ]]
    local output = {}
    local function foo (n)
        output[#output+1] = n
        if n > 0 then
            return foo(n -1)
        end
        return 'end', 0
    end

    array_equals({foo(3)}, {'end', 0}, "tail call")
    array_equals(output, {3, 2, 1, 0})
end

do --[[ no tail call ]]
    local output = {}
    local function foo (n)
        output[#output+1] = n
        if n > 0 then
            return (foo(n -1))
        end
        return 'end', 0
    end

    equals(foo(3), 'end', "no tail call")
    array_equals(output, {3, 2, 1, 0})
end

do --[[ no tail call ]]
    local output = {}
    local function foo (n)
        output[#output+1] = n
        if n > 0 then
            foo(n -1)
        end
    end

    equals(foo(3), nil, "no tail call")
    array_equals(output, {3, 2, 1, 0})
end

do --[[ sub name ]]
    local function f () return 1 end
    equals(f(), 1, "sub name")

    function f () return 2 end
    equals(f(), 2)
end

-- Local Variables:
--   mode: lua
--   lua-indent-level: 4
--   fill-column: 100
-- End:
-- vim: ft=lua expandtab shiftwidth=4:
