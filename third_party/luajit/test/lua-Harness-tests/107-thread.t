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

=head1 Lua thread & coercion

=head2 Synopsis

    % prove 107-thread.t

=head2 Description

=cut

--]]

require'test_assertion'
local has_op53 = _VERSION >= 'Lua 5.3'

plan'no_plan'

local co = coroutine.create(function () return 1 end)

error_matches(function () return -co end,
        "^[^:]+:%d+: attempt to perform arithmetic on",
        "-co")

error_matches(function () return #co end,
        "^[^:]+:%d+: attempt to get length of",
        "#co")

equals(not co, false, "not co")

error_matches(function () return co + 10 end,
        "^[^:]+:%d+: attempt to perform arithmetic on",
        "co + 10")

error_matches(function () return co - 2 end,
        "^[^:]+:%d+: attempt to perform arithmetic on",
        "co - 2")

error_matches(function () return co * 3.14 end,
        "^[^:]+:%d+: attempt to perform arithmetic on",
        "co * 3.14")

error_matches(function () return co / 7 end,
        "^[^:]+:%d+: attempt to perform arithmetic on",
        "co / 7")

error_matches(function () return co % 4 end,
        "^[^:]+:%d+: attempt to perform arithmetic on",
        "co % 4")

error_matches(function () return co ^ 3 end,
        "^[^:]+:%d+: attempt to perform arithmetic on",
        "co ^ 3")

error_matches(function () return co .. 'end' end,
        "^[^:]+:%d+: attempt to concatenate",
        "co .. 'end'")

equals(co == co, true, "co == co")

local co1 = coroutine.create(function () return 1 end)
local co2 = coroutine.create(function () return 2 end)
equals(co1 ~= co2, true, "co1 ~= co2")

equals(co == 1, false, "co == 1")

equals(co ~= 1, true, "co ~= 1")

error_matches(function () return co1 < co2 end,
        "^[^:]+:%d+: attempt to compare two thread values",
        "co1 < co2")

error_matches(function () return co1 <= co2 end,
        "^[^:]+:%d+: attempt to compare two thread values",
        "co1 <= co2")

error_matches(function () return co1 > co2 end,
        "^[^:]+:%d+: attempt to compare two thread values",
        "co1 > co2")

error_matches(function () return co1 >= co2 end,
        "^[^:]+:%d+: attempt to compare two thread values",
        "co1 >= co2")

error_matches(function () return co < 0 end,
        "^[^:]+:%d+: attempt to compare %w+ with %w+",
        "co < 0")

error_matches(function () return co <= 0 end,
        "^[^:]+:%d+: attempt to compare %w+ with %w+",
        "co <= 0")

error_matches(function () return co > 0 end,
        "^[^:]+:%d+: attempt to compare %w+ with %w+",
        "co > 0")

error_matches(function () return co > 0 end,
        "^[^:]+:%d+: attempt to compare %w+ with %w+",
        "co >= 0")

error_matches(function () local a = co[1] end,
        "^[^:]+:%d+: attempt to index",
        "index")

error_matches(function () co[1] = 1 end,
        "^[^:]+:%d+: attempt to index",
        "index")

local t = {}
t[co] = true
truthy(t[co])

if has_op53 then
    _dofile'lexico53/thread.t'
end

done_testing()

-- Local Variables:
--   mode: lua
--   lua-indent-level: 4
--   fill-column: 100
-- End:
-- vim: ft=lua expandtab shiftwidth=4:
