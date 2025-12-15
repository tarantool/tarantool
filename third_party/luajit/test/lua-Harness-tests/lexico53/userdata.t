--
-- lua-Harness : <https://fperrad.frama.io/lua-Harness/>
--
-- Copyright (C) 2009-2021, Perrad Francois
--
-- This code is licensed under the terms of the MIT/X11 license,
-- like Lua itself.
--

local u = io.stdin

error_matches(function () return ~u end,
        "^[^:]+:%d+: attempt to perform bitwise operation on",
        "~u")

error_matches(function () return u // 3 end,
        "^[^:]+:%d+: attempt to perform arithmetic on",
        "u // 3")

error_matches(function () return u & 7 end,
        "^[^:]+:%d+: attempt to perform bitwise operation on",
        "u & 7")

error_matches(function () return u | 1 end,
        "^[^:]+:%d+: attempt to perform bitwise operation on",
        "u | 1")

error_matches(function () return u ~ 4 end,
        "^[^:]+:%d+: attempt to perform bitwise operation on",
        "u ~ 4")

error_matches(function () return u >> 5 end,
        "^[^:]+:%d+: attempt to perform bitwise operation on",
        "u >> 5")

error_matches(function () return u << 2 end,
        "^[^:]+:%d+: attempt to perform bitwise operation on",
        "u << 2")

-- Local Variables:
--   mode: lua
--   lua-indent-level: 4
--   fill-column: 100
-- End:
-- vim: ft=lua expandtab shiftwidth=4:
