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

=head1 Lua string & coercion

=head2 Synopsis

    % prove 105-string.t

=head2 Description

=cut

--]]

require'test_assertion'
local profile = require'profile'
local has_op53 = _VERSION >= 'Lua 5.3'

plan'no_plan'

if profile.nocvts2n then
    error_matches(function () return - '1' end,
            "^[^:]+:%d+: attempt to",
            "-'1'")
else
    equals(- '1', -1, "-'1'")
end

error_matches(function () return - 'text' end,
        "^[^:]+:%d+: attempt to",
        "-'text'")

equals(# 'text', 4, "#'text'")

equals(not 'text', false, "not 'text'")

if profile.nocvts2n then
    error_matches(function () return '10' + 2 end,
            "^[^:]+:%d+: attempt to",
            "'10' + 2")

    error_matches(function () return '2' - 10.5 end,
            "^[^:]+:%d+: attempt to",
            "'2' - 10.5")

    error_matches(function () return '2' * 3 end,
            "^[^:]+:%d+: attempt to",
            "'2' * 3")

    error_matches(function () return '3.14' * 1 end,
            "^[^:]+:%d+: attempt to",
            "'3.14' * 1")

    error_matches(function () return '-7' / 0.5 end,
            "^[^:]+:%d+: attempt to",
            "'-7' / 0.5")

    error_matches(function () return '-25' % 3 end,
            "^[^:]+:%d+: attempt to",
            "'-25' % 3")

    error_matches(function () return '3' ^ 3 end,
            "^[^:]+:%d+: attempt to",
            "'3' ^ 3")
else
    equals('10' + 2, 12, "'10' + 2")

    equals('2' - 10.5, -8.5, "'2' - 10.5")

    equals('2' * 3, 6, "'2' * 3")

    equals('3.14' * 1, 3.14, "'3.14' * 1")

    equals('-7' / 0.5, -14, "'-7' / 0.5")

    equals('-25' % 3, 2, "'-25' % 3")

    equals('3' ^ 3, 27, "'3' ^ 3")
end

error_matches(function () return '10' + true end,
        "^[^:]+:%d+: attempt to",
        "'10' + true")

error_matches(function () return '2' - nil end,
        "^[^:]+:%d+: attempt to",
        "'2' - nil")

error_matches(function () return '2' * {} end,
        "^[^:]+:%d+: attempt to",
        "'2' * {}")

error_matches(function () return '3.14' * false end,
        "^[^:]+:%d+: attempt to",
        "'3.14' * false")

error_matches(function () return '-7' / {} end,
        "^[^:]+:%d+: attempt to",
        "'-7' / {}")

error_matches(function () return '-25' % false end,
        "^[^:]+:%d+: attempt to",
        "'-25' % false")

error_matches(function () return '3' ^ true end,
        "^[^:]+:%d+: attempt to",
        "'3' ^ true")

error_matches(function () return '10' + 'text' end,
        "^[^:]+:%d+: attempt to",
        "'10' + 'text'")

error_matches(function () return '2' - 'text' end,
        "^[^:]+:%d+: attempt to",
        "'2' - 'text'")

error_matches(function () return '3.14' * 'text' end,
        "^[^:]+:%d+: attempt to",
        "'3.14' * 'text'")

error_matches(function () return '-7' / 'text' end,
        "^[^:]+:%d+: attempt to",
        "'-7' / 'text'")

error_matches(function () return '-25' % 'text' end,
        "^[^:]+:%d+: attempt to",
        "'-25' % 'text'")

error_matches(function () return '3' ^ 'text' end,
        "^[^:]+:%d+: attempt to",
        "'3' ^ 'text'")

if profile.nocvts2n then
    error_matches(function () return '10' + '2' end,
            "^[^:]+:%d+: attempt to",
            "'10' + '2'")

    error_matches(function () return '2' - '10.5' end,
            "^[^:]+:%d+: attempt to",
            "'2' - '10.5'")

    error_matches(function () return '2' * '3' end,
            "^[^:]+:%d+: attempt to",
            "'2' * '3'")

    error_matches(function () return '3.14' * '1' end,
            "^[^:]+:%d+: attempt to",
            "'3.14' * '1'")

    error_matches(function () return '-7' / '0.5' end,
            "^[^:]+:%d+: attempt to",
            "'-7' / '0.5'")

    error_matches(function () return '-25' % '3' end,
            "^[^:]+:%d+: attempt to",
            "'-25' % '3'")

    error_matches(function () return '3' ^ '3' end,
            "^[^:]+:%d+: attempt to",
            "'3' ^ '3'")
else
    equals('10' + '2', 12, "'10' + '2'")

    equals('2' - '10.5', -8.5, "'2' - '10.5'")

    equals('2' * '3', 6, "'2' * '3'")

    equals('3.14' * '1', 3.14, "'3.14' * '1'")

    equals('-7' / '0.5', -14, "'-7' / '0.5'")

    equals('-25' % '3', 2, "'-25' % '3'")

    equals('3' ^ '3', 27, "'3' ^ '3'")
end

equals('1' .. 'end', '1end', "'1' .. 'end'")

if profile.nocvtn2s then
    error_matches(function () return '1' .. 2 end,
            "^[^:]+:%d+: attempt to concatenate a number value",
            "'1' .. 2")
else
    equals('1' .. 2, '12', "'1' .. 2")
end

error_matches(function () return '1' .. true end,
        "^[^:]+:%d+: attempt to concatenate a boolean value",
        "'1' .. true")

equals('foo\0bar' <= 'foo\0baz', true, "'foo\\0bar' <= 'foo\\0baz'")

equals('foo\0bar' ~= 'foo', true, "'foo\\0bar' ~= 'foo'")

equals('foo\0bar' >= 'foo', true, "'foo\\0bar' >= 'foo'")

equals('1.0' == '1', false, "'1.0' == '1'")

equals('1' ~= '2', true, "'1' ~= '2'")

equals('1' == true, false, "'1' == true")

equals('1' ~= nil, true, "'1' ~= nil")

equals('1' == 1, false, "'1' == 1")

equals('1' ~= 1, true, "'1' ~= 1")

equals('1' < '0', false, "'1' < '0'")

equals('1' <= '0', false, "'1' <= '0'")

equals('1' > '0', true, "'1' > '0'")

equals('1' >= '0', true, "'1' >= '0'")

error_matches(function () return '1' < false end,
        "^[^:]+:%d+: attempt to compare %w+ with %w+",
        "'1' < false")

error_matches(function () return '1' <= nil end,
        "^[^:]+:%d+: attempt to compare %w+ with %w+",
        "'1' <= nil")

error_matches(function () return '1' > true end,
        "^[^:]+:%d+: attempt to compare %w+ with %w+",
        "'1' > true")

error_matches(function () return '1' >= {} end,
        "^[^:]+:%d+: attempt to compare %w+ with %w+",
        "'1' >= {}")

error_matches(function () return '1' < 0 end,
        "^[^:]+:%d+: attempt to compare %w+ with %w+",
        "'1' < 0")

error_matches(function () return '1' <= 0 end,
        "^[^:]+:%d+: attempt to compare %w+ with %w+",
        "'1' <= 0")

error_matches(function () return '1' > 0 end,
        "^[^:]+:%d+: attempt to compare %w+ with %w+",
        "'1' > 0")

error_matches(function () return '1' > 0 end,
        "^[^:]+:%d+: attempt to compare %w+ with %w+",
        "'1' >== 0")

local a = 'text'
equals(a[1], nil, "index")

error_matches(function () a = 'text'; a[1] = 1; end,
        "^[^:]+:%d+: attempt to index",
        "index")

if has_op53 then
    _dofile'lexico53/string.t'
end

done_testing()

-- Local Variables:
--   mode: lua
--   lua-indent-level: 4
--   fill-column: 100
-- End:
-- vim: ft=lua expandtab shiftwidth=4:
