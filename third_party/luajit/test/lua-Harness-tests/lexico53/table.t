--
-- lua-Harness : <https://fperrad.frama.io/lua-Harness/>
--
-- Copyright (C) 2009-2021, Perrad Francois
--
-- This code is licensed under the terms of the MIT/X11 license,
-- like Lua itself.
--

error_matches(function () return ~{} end,
        "^[^:]+:%d+: attempt to perform bitwise operation on a table value",
        "~{}")

error_matches(function () return {} // 3 end,
        "^[^:]+:%d+: attempt to perform arithmetic on",
        "{} // 3")

error_matches(function () return {} & 7 end,
        "^[^:]+:%d+: attempt to perform bitwise operation on a table value",
        "{} & 7")

error_matches(function () return {} | 1 end,
        "^[^:]+:%d+: attempt to perform bitwise operation on a table value",
        "{} | 1")

error_matches(function () return {} ~ 4 end,
        "^[^:]+:%d+: attempt to perform bitwise operation on a table value",
        "{} ~ 4")

error_matches(function () return {} >> 5 end,
        "^[^:]+:%d+: attempt to perform bitwise operation on a table value",
        "{} >> 5")

error_matches(function () return {} << 2 end,
        "^[^:]+:%d+: attempt to perform bitwise operation on a table value",
        "{} << 2")

-- Local Variables:
--   mode: lua
--   lua-indent-level: 4
--   fill-column: 100
-- End:
-- vim: ft=lua expandtab shiftwidth=4:
