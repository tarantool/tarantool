--
-- lua-Harness : <https://fperrad.frama.io/lua-Harness/>
--
-- Copyright (C) 2009-2021, Perrad Francois
--
-- This code is licensed under the terms of the MIT/X11 license,
-- like Lua itself.
--

error_matches(function () return ~nil end,
        "^[^:]+:%d+: attempt to perform bitwise operation on a nil value",
        "~nil")

error_matches(function () return  nil // 3 end,
        "^[^:]+:%d+: attempt to perform arithmetic on a nil value",
        "nil // 3")

error_matches(function () return nil & 7 end,
        "^[^:]+:%d+: attempt to perform bitwise operation on a nil value",
        "nil & 7")

error_matches(function () return nil | 1 end,
        "^[^:]+:%d+: attempt to perform bitwise operation on a nil value",
        "nil | 1")

error_matches(function () return nil ~ 4 end,
        "^[^:]+:%d+: attempt to perform bitwise operation on a nil value",
        "nil ~ 4")

error_matches(function () return nil >> 5 end,
        "^[^:]+:%d+: attempt to perform bitwise operation on a nil value",
        "nil >> 5")

error_matches(function () return nil << 2 end,
        "^[^:]+:%d+: attempt to perform bitwise operation on a nil value",
        "nil << 2")

-- Local Variables:
--   mode: lua
--   lua-indent-level: 4
--   fill-column: 100
-- End:
-- vim: ft=lua expandtab shiftwidth=4:
