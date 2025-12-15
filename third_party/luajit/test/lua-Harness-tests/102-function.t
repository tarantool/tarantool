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

=head1 Lua function & coercion

=head2 Synopsis

    % prove 102-function.t

=head2 Description

=cut

--]]

require'test_assertion'
local has_op53 = _VERSION >= 'Lua 5.3'

plan'no_plan'

local f = function () return 1 end

error_matches(function () return -f end,
        "^[^:]+:%d+: attempt to perform arithmetic on",
        "-f")

error_matches(function () f = print; return -f end,
        "^[^:]+:%d+: attempt to perform arithmetic on")

error_matches(function () return #f end,
        "^[^:]+:%d+: attempt to get length of",
        "#f")

error_matches(function () f = print; return #f end,
        "^[^:]+:%d+: attempt to get length of")

equals(not f, false, "not f")

equals(not print, false)

error_matches(function () return f + 10 end,
        "^[^:]+:%d+: attempt to perform arithmetic on",
        "f + 10")

error_matches(function () f = print; return f + 10 end,
        "^[^:]+:%d+: attempt to perform arithmetic on")

error_matches(function () return f - 2 end,
        "^[^:]+:%d+: attempt to perform arithmetic on",
        "f - 2")

error_matches(function () f = print; return f - 2 end,
        "^[^:]+:%d+: attempt to perform arithmetic on")

error_matches(function () return f * 3.14 end,
        "^[^:]+:%d+: attempt to perform arithmetic on",
        "f * 3.14")

error_matches(function () f = print; return f * 3.14 end,
        "^[^:]+:%d+: attempt to perform arithmetic on")

error_matches(function () return f / -7 end,
        "^[^:]+:%d+: attempt to perform arithmetic on",
        "f / -7")

error_matches(function () f = print; return f / -7 end,
        "^[^:]+:%d+: attempt to perform arithmetic on")

error_matches(function () return f % 4 end,
        "^[^:]+:%d+: attempt to perform arithmetic on",
        "f % 4")

error_matches(function () f = print; return f % 4 end,
        "^[^:]+:%d+: attempt to perform arithmetic on")

error_matches(function () return f ^ 3 end,
        "^[^:]+:%d+: attempt to perform arithmetic on",
        "f ^ 3")

error_matches(function () f = print; return f ^ 3 end,
        "^[^:]+:%d+: attempt to perform arithmetic on")

error_matches(function () return f .. 'end' end,
        "^[^:]+:%d+: attempt to concatenate",
        "f .. 'end'")

error_matches(function () f = print; return f .. 'end' end,
        "^[^:]+:%d+: attempt to concatenate")

local g = f
equals(f == g, true, "f == f")

g = print
equals(g == print, true)

g = function () return 2 end
equals(f ~= g, true, "f ~= g")
local h = type
equals(f ~= h, true)

equals(print ~= g, true)
equals(print ~= h, true)

equals(f == 1, false, "f == 1")

equals(print == 1, false)

equals(f ~= 1, true, "f ~= 1")

equals(print ~= 1, true)

error_matches(function () return f < g end,
        "^[^:]+:%d+: attempt to compare two function values",
        "f < g")

error_matches(function () f = print; g = type; return f < g end,
        "^[^:]+:%d+: attempt to compare two function values")

error_matches(function () return f <= g end,
        "^[^:]+:%d+: attempt to compare two function values",
        "f <= g")

error_matches(function () f = print; g = type; return f <= g end,
        "^[^:]+:%d+: attempt to compare two function values")

error_matches(function () return f > g end,
        "^[^:]+:%d+: attempt to compare two function values",
        "f > g")

error_matches(function () f = print; g = type; return f > g end,
        "^[^:]+:%d+: attempt to compare two function values")

error_matches(function () return f >= g end,
        "^[^:]+:%d+: attempt to compare two function values",
        "f >= g")

error_matches(function () f = print; g = type; return f >= g end,
        "^[^:]+:%d+: attempt to compare two function values")

error_matches(function () return f < 0 end,
        "^[^:]+:%d+: attempt to compare %w+ with %w+",
        "f < 0")

error_matches(function () f = print; return f < 0 end,
        "^[^:]+:%d+: attempt to compare %w+ with %w+")

error_matches(function () return f <= 0 end,
        "^[^:]+:%d+: attempt to compare %w+ with %w+",
        "f <= 0")

error_matches(function () f = print; return f <= 0 end,
        "^[^:]+:%d+: attempt to compare %w+ with %w+")

error_matches(function () return f > 0 end,
        "^[^:]+:%d+: attempt to compare %w+ with %w+",
        "f > 0")

error_matches(function () f = print; return f > 0 end,
        "^[^:]+:%d+: attempt to compare %w+ with %w+")

error_matches(function () return f > 0 end,
        "^[^:]+:%d+: attempt to compare %w+ with %w+",
        "f >= 0")

error_matches(function () f = print; return f >= 0 end,
        "^[^:]+:%d+: attempt to compare %w+ with %w+")

error_matches(function () local a = f; local b = a[1]; end,
        "^[^:]+:%d+: attempt to index",
        "index")

error_matches(function () local a = print; local b = a[1]; end,
        "^[^:]+:%d+: attempt to index")

error_matches(function () local a = f; a[1] = 1; end,
        "^[^:]+:%d+: attempt to index",
        "index")

error_matches(function () local a = print; a[1] = 1; end,
        "^[^:]+:%d+: attempt to index")

local t = {}
t[print] = true
truthy(t[print])

if has_op53 then
    _dofile'lexico53/function.t'
end

done_testing()

-- Local Variables:
--   mode: lua
--   lua-indent-level: 4
--   fill-column: 100
-- End:
-- vim: ft=lua expandtab shiftwidth=4:
