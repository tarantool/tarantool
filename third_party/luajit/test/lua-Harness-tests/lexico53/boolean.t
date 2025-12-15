--
-- lua-Harness : <https://fperrad.frama.io/lua-Harness/>
--
-- Copyright (C) 2009-2021, Perrad Francois
--
-- This code is licensed under the terms of the MIT/X11 license,
-- like Lua itself.
--

error_matches(function () return ~true end,
        "^[^:]+:%d+: attempt to perform bitwise operation on a boolean value",
        "~true")

error_matches(function () return true // 3 end,
        "^[^:]+:%d+: attempt to perform arithmetic on a boolean value",
        "true // 3")

error_matches(function () return true & 7 end,
        "^[^:]+:%d+: attempt to perform bitwise operation on a boolean value",
        "true & 7")

error_matches(function () return true | 1 end,
        "^[^:]+:%d+: attempt to perform bitwise operation on a boolean value",
        "true | 1")

error_matches(function () return true ~ 4 end,
        "^[^:]+:%d+: attempt to perform bitwise operation on a boolean value",
        "true ~ 4")

error_matches(function () return true >> 5 end,
        "^[^:]+:%d+: attempt to perform bitwise operation on a boolean value",
        "true >> 5")

error_matches(function () return true << 2 end,
        "^[^:]+:%d+: attempt to perform bitwise operation on a boolean value",
        "true << 2")

-- Local Variables:
--   mode: lua
--   lua-indent-level: 4
--   fill-column: 100
-- End:
-- vim: ft=lua expandtab shiftwidth=4:
