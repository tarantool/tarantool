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

=head1 Lua iterators

=head2 Synopsis

    % prove 223-iterator.t

=head2 Description

See section "Iterators and the Generic for" and
section "Coroutines as Iterators" in "Programming in Lua".

=cut

--]]

require'test_assertion'

plan(8)

do --[[ list_iter ]]
    local function list_iter (t)
        local i = 0
        local n = #t
        return function ()
                   i = i + 1
                   if i <= n then
                       return t[i]
                   else
                       return nil
                   end
               end
    end

    local t = {10, 20, 30}
    local output = {}
    for element in list_iter(t) do
        output[#output+1] = element
    end
    array_equals(output, t, "list_iter")
end

do --[[ values ]]
    local function values (t)
        local i = 0
        return function ()
                   i = i + 1
                   return t[i]
               end
    end

    local t = {10, 20, 30}
    local output = {}
    for element in values(t) do
        output[#output+1] = element
    end
    array_equals(output, t, "values")
end

do --[[ emul ipairs ]]
    local function iter (a, i)
        i = i + 1
        local v = a[i]
        if v then
            return i, v
        end
    end

    local function my_ipairs (a)
        return iter, a, 0
    end

    local a = {'one', 'two', 'three'}
    local output = {}
    for i, v in my_ipairs(a) do
        output[#output+1] = i
        output[#output+1] = v
    end
    array_equals(output, {1, 'one', 2, 'two', 3, 'three'}, "emul ipairs")
end

do --[[ emul pairs ]]
    local function my_pairs (t)
        return next, t, nil
    end

    local a = {'one', 'two', 'three'}
    local output = {}
    for k, v in my_pairs(a) do
        output[#output+1] = k
        output[#output+1] = v
    end
    array_equals(output, {1, 'one', 2, 'two', 3, 'three'}, "emul ipairs")
end

do --[[ with next ]]
    local t = {'one', 'two', 'three'}
    local output = {}
    for k, v in next, t do
        output[#output+1] = k
        output[#output+1] = v
    end
    array_equals(output, {1, 'one', 2, 'two', 3, 'three'}, "with next")
end

do --[[ permutations ]]
    local function permgen (a, n)
        n = n or #a         -- default for 'n' is size of 'a'
        if n <= 1 then      -- nothing to change?
            coroutine.yield(a)
        else
            for i=1,n do
                -- put i-th element as the last one
                a[n], a[i] = a[i], a[n]
                -- generate all permutations of the other elements
                permgen(a, n - 1)
                -- restore i-th element
                a[n], a[i] = a[i], a[n]
            end
        end
    end

    local function permutations (a)
        local co = coroutine.create(function () permgen(a) end)
        return function ()  -- iterator
                   local code, res = coroutine.resume(co)
                   return res
               end
    end

    local output = {}
    for p in permutations{'a', 'b', 'c'} do
        output[#output+1] = table.concat(p, ' ')
    end
    array_equals(output, {'b c a','c b a','c a b','a c b','b a c','a b c'}, "permutations")
end

do --[[ permutations with wrap ]]
    local function permgen (a, n)
        n = n or #a         -- default for 'n' is size of 'a'
        if n <= 1 then      -- nothing to change?
            coroutine.yield(a)
        else
            for i=1,n do
                -- put i-th element as the last one
                a[n], a[i] = a[i], a[n]
                -- generate all permutations of the other elements
                permgen(a, n - 1)
                -- restore i-th element
                a[n], a[i] = a[i], a[n]
            end
        end
    end

    local function permutations (a)
        return coroutine.wrap(function () permgen(a) end)
    end

    local output = {}
    for p in permutations{'a', 'b', 'c'} do
        output[#output+1] = table.concat(p, ' ')
    end
    array_equals(output, {'b c a','c b a','c a b','a c b','b a c','a b c'}, "permutations with wrap")
end

do --[[ fibo ]]
    local function fibogen ()
        local x, y = 0, 1
        while true do
            coroutine.yield(x)
            x, y = y, x + y
        end
    end

    local function fibo ()
        return coroutine.wrap(function () fibogen() end)
    end

    local output = {}
    for n in fibo() do
        output[#output+1] = n
        if n > 30 then break end
    end
    array_equals(output, {0, 1, 1, 2, 3, 5, 8, 13, 21, 34}, "fibo")
end

-- Local Variables:
--   mode: lua
--   lua-indent-level: 4
--   fill-column: 100
-- End:
-- vim: ft=lua expandtab shiftwidth=4:
