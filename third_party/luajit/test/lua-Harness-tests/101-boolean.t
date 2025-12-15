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

=head1  Lua boolean & coercion

=head2 Synopsis

    % prove 101-boolean.t

=head2 Description

=cut

]]

require'test_assertion'
local has_op53 = _VERSION >= 'Lua 5.3'

plan'no_plan'

error_matches(function () return -true end,
        "^[^:]+:%d+: attempt to perform arithmetic on a %w+ value",
        "-true")

error_matches(function () return #true end,
        "^[^:]+:%d+: attempt to get length of a boolean value",
        "#true")

equals(not false, true, "not false")

error_matches(function () return true + 10 end,
        "^[^:]+:%d+: attempt to perform arithmetic on a boolean value",
        "true + 10")

error_matches(function () return true - 2 end,
        "^[^:]+:%d+: attempt to perform arithmetic on a boolean value",
        "true - 2")

error_matches(function () return true * 3.14 end,
        "^[^:]+:%d+: attempt to perform arithmetic on a boolean value",
        "true * 3.14")

error_matches(function () return true / -7 end,
        "^[^:]+:%d+: attempt to perform arithmetic on a boolean value",
        "true / -7")

error_matches(function () return true % 4 end,
        "^[^:]+:%d+: attempt to perform arithmetic on a boolean value",
        "true % 4")

error_matches(function () return true ^ 3 end,
        "^[^:]+:%d+: attempt to perform arithmetic on a boolean value",
        "true ^ 3")

error_matches(function () return true .. 'end' end,
        "^[^:]+:%d+: attempt to concatenate a boolean value",
        "true .. 'end'")

equals(true == true, true, "true == true")

equals(true ~= false, true, "true ~= false")

equals(true == 1, false, "true == 1")

equals(true ~= 1, true, "true ~= 1")

error_matches(function () return true < false end,
        "^[^:]+:%d+: attempt to compare two boolean values",
        "true < false")

error_matches(function () return true <= false end,
        "^[^:]+:%d+: attempt to compare two boolean values",
        "true <= false")

error_matches(function () return true > false end,
        "^[^:]+:%d+: attempt to compare two boolean values",
        "true > false")

error_matches(function () return true >= false end,
        "^[^:]+:%d+: attempt to compare two boolean values",
        "true >= false")

error_matches(function () return true < 0 end,
        "^[^:]+:%d+: attempt to compare %w+ with %w+",
        "true < 0")

error_matches(function () return true <= 0 end,
        "^[^:]+:%d+: attempt to compare %w+ with %w+",
        "true <= 0")

error_matches(function () return true > 0 end,
        "^[^:]+:%d+: attempt to compare %w+ with %w+",
        "true > 0")

error_matches(function () return true >= 0 end,
        "^[^:]+:%d+: attempt to compare %w+ with %w+",
        "true >= 0")

error_matches(function () local a = true; local b = a[1]; end,
        "^[^:]+:%d+: attempt to index",
        "index")

error_matches(function () local a = true; a[1] = 1; end,
        "^[^:]+:%d+: attempt to index",
        "index")

if has_op53 then
    _dofile'lexico53/boolean.t'
end

done_testing()

-- Local Variables:
--   mode: lua
--   lua-indent-level: 4
--   fill-column: 100
-- End:
-- vim: ft=lua expandtab shiftwidth=4:
