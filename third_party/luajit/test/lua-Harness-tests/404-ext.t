#! /usr/bin/lua
--
-- lua-Harness : <https://fperrad.frama.io/lua-Harness/>
--
-- Copyright (C) 2019-2021, Perrad Francois
--
-- This code is licensed under the terms of the MIT/X11 license,
-- like Lua itself.
--

--[[

=head1 JIT Library extensions

=head2 Synopsis

    % prove 404-ext.t

=head2 Description

See L<https://luajit.org/ext_jit.html>.

=cut

--]]

require 'test_assertion'
local profile = require'profile'

local luajit21 = jit and (jit.version_num >= 20100 or jit.version:match'^RaptorJIT')
if not luajit21 then
    skip_all("only with LuaJIT 2.1")
end

plan'no_plan'

do -- table.new
    local r, new = pcall(require, 'table.new')
    is_true(r, 'table.new')
    is_function(new)
    equals(package.loaded['table.new'], new)

    is_table(new(100, 0))
    is_table(new(0, 100))
    is_table(new(200, 200))

    error_matches(function () new(42) end,
            "^[^:]+:%d+: bad argument #2 to 'new' %(number expected, got no value%)")
end

do -- table.clear
    local r, clear = pcall(require, 'table.clear')
    is_true(r, 'table.clear')
    is_function(clear)
    equals(package.loaded['table.clear'], clear)

    local t = { 'foo', bar = 42 }
    equals(t[1], 'foo')
    equals(t.bar, 42)
    clear(t)
    is_nil(t[1])
    is_nil(t.bar)

    error_matches(function () clear(42) end,
            "^[^:]+:%d+: bad argument #1 to 'clear' %(table expected, got number%)")
end

-- table.clone
if profile.openresty then
    local r, clone = pcall(require, 'table.clone')
    is_true(r, 'table.clone')
    is_function(clone)
    equals(package.loaded['table.clone'], clone)

    local mt = {}
    local t = setmetatable({ 'foo', bar = 42 }, mt)
    equals(t[1], 'foo')
    equals(t.bar, 42)
    local t2 = clone(t)
    is_table(t2)
    not_equals(t2, t)
    is_nil(getmetatable(t2))
    equals(t2[1], 'foo')
    equals(t2.bar, 42)

    error_matches(function () clone(42) end,
            "^[^:]+:%d+: bad argument #1 to 'clone' %(table expected, got number%)")
else
    is_false(pcall(require, 'table.clone'), 'no table.clone')
end

-- table.isarray
if profile.openresty then
    local r, isarray = pcall(require, 'table.isarray')
    is_true(r, 'table.isarray')
    is_function(isarray)
    equals(package.loaded['table.isarray'], isarray)

    is_false(isarray({ [3] = 3, [5.3] = 4 }))
    is_true(isarray({ [3] = 'a', [5] = true }))
    is_true(isarray({ 'a', nil, true, 3.14 }))
    is_true(isarray({}))
    is_false(isarray({ ['1'] = 3, ['2'] = 4 }))
    is_false(isarray({ ['dog'] = 3, ['cat'] = 4 }))
    is_false(isarray({ 'dog', 'cat', true, ['bird'] = 3 }))

    error_matches(function () isarray(42) end,
            "^[^:]+:%d+: bad argument #1 to 'isarray' %(table expected, got number%)")
else
    is_false(pcall(require, 'table.isarray'), 'no table.isarray')
end

-- table.isempty
if profile.openresty then
    local r, isempty = pcall(require, 'table.isempty')
    is_true(r, 'table.isempty')
    is_function(isempty)
    equals(package.loaded['table.isempty'], isempty)

    is_true(isempty({}))
    is_true(isempty({ nil }))
    is_true(isempty({ dogs = nil }))
    is_false(isempty({ 3.1 }))
    is_false(isempty({ 'a', 'b' }))
    is_false(isempty({ nil, false }))
    is_false(isempty({ dogs = 3 }))
    is_false(isempty({ dogs = 3, cats = 4 }))
    is_false(isempty({ dogs = 3, 5 }))

    error_matches(function () isempty(42) end,
            "^[^:]+:%d+: bad argument #1 to 'isempty' %(table expected, got number%)")
else
    is_false(pcall(require, 'table.isempty'), 'no table.isempty')
end

-- table.nkeys
if profile.openresty then
    local r, nkeys = pcall(require, 'table.nkeys')
    is_true(r, 'table.nkeys')
    is_function(nkeys)
    equals(package.loaded['table.nkeys'], nkeys)

    equals(nkeys({}), 0)
    equals(nkeys({ cats = 4 }), 1)
    equals(nkeys({ dogs = 3, cats = 4 }), 2)
    equals(nkeys({ dogs = nil, cats = 4 }), 1)
    equals(nkeys({ 'cats' }), 1)
    equals(nkeys({ 'dogs', 3, 'cats', 4 }), 4)
    equals(nkeys({ 'dogs', nil, 'cats', 4 }), 3)
    equals(nkeys({ cats = 4, 5, 6 }), 3)
    equals(nkeys({ nil, 'foo', dogs = 3, cats = 4 }), 3)

    error_matches(function () nkeys(42) end,
            "^[^:]+:%d+: bad argument #1 to 'nkeys' %(table expected, got number%)")
else
    is_false(pcall(require, 'table.nkeys'), 'no table.nkeys')
end

-- thread.exdata
if pcall(require, 'ffi') and (profile.openresty or jit.version:match'moonjit') then
    _dofile'lexicojit/ext.t'
end

done_testing()

-- Local Variables:
--   mode: lua
--   lua-indent-level: 4
--   fill-column: 100
-- End:
-- vim: ft=lua expandtab shiftwidth=4:
