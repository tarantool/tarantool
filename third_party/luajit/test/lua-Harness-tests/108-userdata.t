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

=head1 Lua userdata & coercion

=head2 Synopsis

    % prove 108-userdata.t

=head2 Description

=cut

--]]

require'test_assertion'
local has_op53 = _VERSION >= 'Lua 5.3'

plan'no_plan'

local u = io.stdin

error_matches(function () return -u end,
        "^[^:]+:%d+: attempt to perform arithmetic on",
        "-u")

error_matches(function () return #u end,
        "^[^:]+:%d+: attempt to get length of",
        "#u")

equals(not u, false, "not u")

error_matches(function () return u + 10 end,
        "^[^:]+:%d+: attempt to perform arithmetic on",
        "u + 10")

error_matches(function () return u - 2 end,
        "^[^:]+:%d+: attempt to perform arithmetic on",
        "u - 2")

error_matches(function () return u * 3.14 end,
        "^[^:]+:%d+: attempt to perform arithmetic on",
        "u * 3.14")

error_matches(function () return u / 7 end,
        "^[^:]+:%d+: attempt to perform arithmetic on",
        "u / 7")

error_matches(function () return u % 4 end,
        "^[^:]+:%d+: attempt to perform arithmetic on",
        "u % 4")

error_matches(function () return u ^ 3 end,
        "^[^:]+:%d+: attempt to perform arithmetic on",
        "u ^ 3")

error_matches(function () return u .. 'end' end,
        "^[^:]+:%d+: attempt to concatenate",
        "u .. 'end'")

equals(u == u, true, "u == u")

local v = io.stdout
equals(u ~= v, true, "u ~= v")

equals(u == 1, false, "u == 1")

equals(u ~= 1, true, "u ~= 1")

error_matches(function () return u < v end,
        "^[^:]+:%d+: attempt to compare two",
        "u < v")

error_matches(function () return u <= v end,
        "^[^:]+:%d+: attempt to compare two",
        "u <= v")

error_matches(function () return u > v end,
        "^[^:]+:%d+: attempt to compare two",
        "u > v")

error_matches(function () return u >= v end,
        "^[^:]+:%d+: attempt to compare two",
        "u >= v")

error_matches(function () return u < 0 end,
        "^[^:]+:%d+: attempt to compare",
        "u < 0")

error_matches(function () return u <= 0 end,
        "^[^:]+:%d+: attempt to compare",
        "u <= 0")

error_matches(function () return u > 0 end,
        "^[^:]+:%d+: attempt to compare",
        "u > 0")

error_matches(function () return u > 0 end,
        "^[^:]+:%d+: attempt to compare",
        "u >= 0")

equals(u[1], nil, "index")

error_matches(function () u[1] = 1 end,
        "^[^:]+:%d+: attempt to index",
        "index")

local t = {}
t[u] = true
truthy(t[u])

if has_op53 then
    _dofile'lexico53/userdata.t'
end

done_testing()

-- Local Variables:
--   mode: lua
--   lua-indent-level: 4
--   fill-column: 100
-- End:
-- vim: ft=lua expandtab shiftwidth=4:
