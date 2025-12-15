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

=head1 Lua Table Library

=head2 Synopsis

    % prove 305-table.t

=head2 Description

Tests Lua Table Library

See section "Table Manipulation" in "Reference Manual"
L<https://www.lua.org/manual/5.1/manual.html#5.5>,
L<https://www.lua.org/manual/5.2/manual.html#6.5>,
L<https://www.lua.org/manual/5.3/manual.html#6.6>,
L<https://www.lua.org/manual/5.4/manual.html#6.6>

=cut

--]]

require'test_assertion'
local profile = require'profile'
local luajit21 = jit and (jit.version_num >= 20100 or jit.version:match'^RaptorJIT')
local has_foreach = _VERSION == 'Lua 5.1'
local has_foreachi = _VERSION == 'Lua 5.1'
local has_getn = _VERSION == 'Lua 5.1'
local has_setn_obsolete = _VERSION == 'Lua 5.1' and not jit
local has_maxn = _VERSION == 'Lua 5.1' or profile.compat51 or profile.has_table_maxn
local has_pack = _VERSION >= 'Lua 5.2' or profile.luajit_compat52
local has_move = _VERSION >= 'Lua 5.3' or  luajit21
local has_unpack = _VERSION >= 'Lua 5.2'
local has_alias_unpack = profile.luajit_compat52
local nocvtn2s = profile.nocvtn2s

plan'no_plan'

do -- concat
    local t = {'a','b','c','d','e'}
    equals(table.concat(t), 'abcde', "function concat")
    equals(table.concat(t, ','), 'a,b,c,d,e')
    equals(table.concat(t, ',',2), 'b,c,d,e')
    equals(table.concat(t, ',', 2, 4), 'b,c,d')
    equals(table.concat(t, ',', 4, 2), '')

    t = {'a','b',3,'d','e'}
    if nocvtn2s then
        error_matches(function () table.concat(t,',') end,
            "^[^:]+:%d+: invalid value %(number%) at index 3 in table for 'concat'",
            "function concat (number no conv)")
    else
        equals(table.concat(t,','), 'a,b,3,d,e', "function concat (number)")
    end

    t = {'a','b','c','d','e'}
    error_matches(function () table.concat(t, ',', 2, 7) end,
            "^[^:]+:%d+: invalid value %(nil%) at index 6 in table for 'concat'",
            "function concat (out of range)")

    t = {'a','b',true,'d','e'}
    error_matches(function () table.concat(t, ',') end,
            "^[^:]+:%d+: invalid value %(boolean%) at index 3 in table for 'concat'",
            "function concat (non-string)")
end

do -- insert
    local a = {'10', '20', '30'}
    table.insert(a, 1, '15')
    equals(table.concat(a,','), '15,10,20,30', "function insert")
    local t = {}
    table.insert(t, 'a')
    equals(table.concat(t, ','), 'a')
    table.insert(t, 'b')
    equals(table.concat(t, ','), 'a,b')
    table.insert(t, 1, 'c')
    equals(table.concat(t, ','), 'c,a,b')
    table.insert(t, 2, 'd')
    equals(table.concat(t, ','), 'c,d,a,b')
    table.insert(t, 5, 'e')
    equals(table.concat(t, ','), 'c,d,a,b,e')

    if _VERSION == 'Lua 5.1' then
        todo("not with 5.1", 2)
    end
    error_matches(function () table.insert(t, 7, 'f') end,
            "^[^:]+:%d+: bad argument #2 to 'insert' %(position out of bounds%)",
            "function insert (out of bounds)")

    error_matches(function () table.insert(t, -9, 'f') end,
            "^[^:]+:%d+: bad argument #2 to 'insert' %(position out of bounds%)",
            "function insert (out of bounds)")

    error_matches(function () table.insert(t, 2, 'g', 'h')  end,
            "^[^:]+:%d+: wrong number of arguments to 'insert'",
            "function insert (too many arg)")
end

-- foreach 5.0
if has_foreach then
    local t = {a=10, b=100}
    local output = {}
    table.foreach(t, function (k, v) output[k] = v end)
    array_equals(output, t, "function foreach (hash)")

    t = {'a','b','c'}
    output = {}
    table.foreach(t, function (k, v)
        table.insert(output, k)
        table.insert(output, v)
    end)
    array_equals(output, {1, 'a', 2, 'b', 3, 'c'}, "function foreach (array)")
else
    is_nil(table.foreach, "no table.foreach");
end

-- foreachi 5.0
if has_foreachi then
    local t = {'a','b','c'}
    local output = {}
    table.foreachi(t, function (i, v)
        table.insert(output, i)
        table.insert(output, v)
    end)
    array_equals(output, {1, 'a', 2, 'b', 3, 'c'}, "function foreachi")
else
    is_nil(table.foreachi, "no table.foreachi");
end

if has_getn then
    equals(table.getn{10,2,4}, 3, "function getn")
    equals(table.getn{10,2,nil}, 2)
else
    is_nil(table.getn, "no table.getn");
end

-- maxn
if has_maxn then
    local t = {}
    equals(table.maxn(t), 0, "function maxn")
    t[1] = 'a'
    t[2] = 'b'
    equals(table.maxn(t), 2)
    t[6] = 'g'
    equals(table.maxn(t), 6)

    local a = {}
    a[10000] = 1
    equals(table.maxn(a), 10000)
else
    is_nil(table.maxn, "no table.maxn");
end

-- move
if has_move then
    local a = {'a', 'b', 'c'}
    local t = { 1, 2, 3, 4}
    table.move(a, 1, 3, 1, t)
    array_equals(t, {'a', 'b', 'c', 4}, "function move")
    table.move(a, 1, 3, 3, t)
    array_equals(t, {'a', 'b', 'a', 'b', 'c'})

    table.move(a, 1, 3, 1)
    array_equals(a, {'a', 'b', 'c'})
    table.move(a, 1, 3, 3)
    array_equals(a, {'a', 'b', 'a', 'b', 'c'})

    error_matches(function () table.move(a, 1, 2, 1, 2) end,
            "^[^:]+:%d+: bad argument #5 to 'move' %(table expected",
            "function move (bad arg)")

    error_matches(function () table.move(a, 1, 2) end,
            "^[^:]+:%d+: bad argument #4 to 'move' %(number expected, got .-%)",
            "function move (bad arg)")

    error_matches(function () table.move(a, 1) end,
            "^[^:]+:%d+: bad argument #3 to 'move' %(number expected, got .-%)",
            "function move (bad arg)")
else
    is_nil(table.move, "no table.move");
end

-- pack
if has_pack then
    local t = table.pack("abc", "def", "ghi")
    array_equals(t, {
        "abc",
        "def",
        "ghi"
    }, "function pack")
    equals(t.n, 3)

    t = table.pack()
    array_equals(t, {}, "function pack (no element)")
    equals(t.n, 0)
else
    is_nil(table.pack, "no table.pack");
end

do -- remove
    local t = {}
    local a = table.remove(t)
    equals(a, nil, "function remove")
    t = {'a','b','c','d','e'}
    a = table.remove(t)
    equals(a, 'e')
    equals(table.concat(t, ','), 'a,b,c,d')
    a = table.remove(t,3)
    equals(a, 'c')
    equals(table.concat(t, ','), 'a,b,d')
    a = table.remove(t,1)
    equals(a, 'a')
    equals(table.concat(t, ','), 'b,d')

    if _VERSION == 'Lua 5.1' then
        todo("not with 5.1", 1)
    end
    error_matches(function () table.remove(t,7) end,
            "^[^:]+:%d+: bad argument #1 to 'remove' %(position out of bounds%)",
            "function remove (out of bounds)")
end

-- setn obsolete
if has_setn_obsolete then
    local a = {}
    error_matches(function () table.setn(a, 10000) end,
            "^[^:]+:%d+: 'setn' is obsolete",
            "function setn")
else
    is_nil(table.setn, "no table.setn");
end

do -- sort
    local lines = {
        luaH_set = 10,
        luaH_get = 24,
        luaH_present = 48,
    }

    do
        local a = {}
        for n in pairs(lines) do a[#a + 1] = n end
        table.sort(a)
        local output = {}
        for _, n in ipairs(a) do
            table.insert(output, n)
        end
        array_equals(output, {'luaH_get', 'luaH_present', 'luaH_set'}, "function sort")
    end

    do
        local function pairsByKeys (t, f)
            local a = {}
            for n in pairs(t) do a[#a + 1] = n end
            table.sort(a, f)
            local i = 0     -- iterator variable
            return function ()  -- iterator function
                i = i + 1
                return a[i], t[a[i]]
            end
        end

        local output = {}
        for name, line in pairsByKeys(lines) do
            table.insert(output, name)
            table.insert(output, line)
        end
        array_equals(output, {'luaH_get', 24, 'luaH_present', 48, 'luaH_set', 10}, "function sort")

        output = {}
        for name, line in pairsByKeys(lines, function (a, b) return a < b end) do
            table.insert(output, name)
            table.insert(output, line)
        end
        array_equals(output, {'luaH_get', 24, 'luaH_present', 48, 'luaH_set', 10}, "function sort")
    end

    do
        local function permgen (a, n)
            n = n or #a
            if n <= 1 then
                coroutine.yield(a)
            else
                for i=1,n do
                    a[n], a[i] = a[i], a[n]
                    permgen(a, n - 1)
                    a[n], a[i] = a[i], a[n]
                end
            end
        end

        local function permutations (a)
            local co = coroutine.create(function () permgen(a) end)
            return function ()
                       local code, res = coroutine.resume(co)
                       return res
                    end
        end

        local t = {}
        local output = {}
        for _, v in ipairs{'a', 'b', 'c', 'd', 'e', 'f', 'g'} do
            table.insert(t, v)
            local ref = table.concat(t, ' ')
            table.insert(output, ref)
            local n = 0
            for p in permutations(t) do
                local c = {}
                for i, vv in ipairs(p) do
                    c[i] = vv
                end
                table.sort(c)
                assert(ref == table.concat(c, ' '), table.concat(p, ' '))
                n = n + 1
            end
            table.insert(output, n)
        end

        array_equals(output, {
            'a', 1,
            'a b', 2,
            'a b c', 6,
            'a b c d', 24,
            'a b c d e', 120,
            'a b c d e f', 720,
            'a b c d e f g', 5040,
        }, "function sort (all permutations)")
    end

    if _VERSION == 'Lua 5.1' and not jit then
        todo("not with 5.1")
    end
    error_matches(function ()
                   local t = { 1 }
                   table.sort( { t, t, t, t, }, function (a, b) return a[1] == b[1] end )
            end,
            "^[^:]+:%d+: invalid order function for sorting",
            "function sort (bad func)")
end

-- unpack
if has_unpack then
    array_equals({table.unpack({})}, {}, "function unpack")
    array_equals({table.unpack({'a'})}, {'a'})
    array_equals({table.unpack({'a','b','c'})}, {'a','b','c'})
    array_equals({(table.unpack({'a','b','c'}))}, {'a'})
    array_equals({table.unpack({'a','b','c','d','e'},2,4)}, {'b','c','d'})
    array_equals({table.unpack({'a','b','c'},2,4)}, {'b','c'})
elseif has_alias_unpack then
    equals(table.unpack, unpack, "alias table.unpack");
else
    is_nil(table.unpack, "no table.unpack");
end

done_testing()

-- Local Variables:
--   mode: lua
--   lua-indent-level: 4
--   fill-column: 100
-- End:
-- vim: ft=lua expandtab shiftwidth=4:
