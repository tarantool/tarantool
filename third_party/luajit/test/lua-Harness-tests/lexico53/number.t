--
-- lua-Harness : <https://fperrad.frama.io/lua-Harness/>
--
-- Copyright (C) 2009-2021, Perrad Francois
--
-- This code is licensed under the terms of the MIT/X11 license,
-- like Lua itself.
--

local profile = require'profile'

equals(~4, -5, "~4")

error_matches(function () return ~3.14 end,
        "^[^:]+:%d+: number has no integer representation",
        "~3.14")

equals(25.5 // 3.5, 7.0, "25.5 // 3.5")

equals(25 // 3, 8, "25 // 3")

equals(25 // -3, -9, "25 // -3")

equals(1 // -1, -1, "1 // -1")

is_number(1.0 // 0, "1.0 // 0")

error_matches(function () return 1 // 0 end,
        "^[^:]+:%d+: attempt to divide by zero",
        "1 // 0")

equals(3 & 7, 3, "3 & 7")

equals(4 | 1, 5, "4 | 1")

equals(7 ~ 1, 6, "7 ~ 1")

equals(100 >> 5, 3, "100 >> 5")

equals(3 << 2, 12, "3 << 2")

error_matches(function () return 25 // {} end,
        "^[^:]+:%d+: attempt to perform arithmetic on a table value",
        "25 // {}")

error_matches(function () return 3 & true end,
        "^[^:]+:%d+: attempt to perform bitwise operation on a boolean value",
        "3 & true")

error_matches(function () return 4 | true end,
        "^[^:]+:%d+: attempt to perform bitwise operation on a boolean value",
        "4 | true")

error_matches(function () return 7 ~ true end,
        "^[^:]+:%d+: attempt to perform bitwise operation on a boolean value",
        "7 ~ true")

error_matches(function () return 100 >> true end,
        "^[^:]+:%d+: attempt to perform bitwise operation on a boolean value",
        "100 >> true")

error_matches(function () return 3 << true end,
        "^[^:]+:%d+: attempt to perform bitwise operation on a boolean value",
        "3 << true")

error_matches(function () return 25 // 'text' end,
        "^[^:]+:%d+: attempt to",
        "25 // 'text'")

error_matches(function () return 3 & 'text' end,
        "^[^:]+:%d+: attempt to",
        "3 & 'text'")

error_matches(function () return 4 | 'text' end,
        "^[^:]+:%d+: attempt to",
        "4 | 'text'")

error_matches(function () return 7 ~ 'text' end,
        "^[^:]+:%d+: attempt to",
        "7 ~ 'text'")

error_matches(function () return 100 >> 'text' end,
        "^[^:]+:%d+: attempt to",
        "100 >> 'text'")

error_matches(function () return 3 << 'text' end,
        "^[^:]+:%d+: attempt to",
        "3 << 'text'")

if profile.nocvts2n then
    error_matches(function () return 25.5 // '3.5' end,
            "^[^:]+:%d+: attempt to",
            "25.5 // '3.5'")

    error_matches(function () return 25 // '3' end,
            "^[^:]+:%d+: attempt to",
            "25 // '3'")
else
    equals(25.5 // '3.5', 7.0, "25.5 // '3.5'")

    equals(25 // '3', 8, "25 // '3'")
end

if profile.nocvts2n or _VERSION >= 'Lua 5.4' then
    error_matches(function () return 3 & '7' end,
            "^[^:]+:%d+: attempt to",
            "3 & '7'")

    error_matches(function () return 4 | '1' end,
            "^[^:]+:%d+: attempt to",
            "4 | '1'")

    error_matches(function () return 7 ~ '1' end,
            "^[^:]+:%d+: attempt to",
            "7 ~ '1'")

    error_matches(function () return 100 >> '5' end,
            "^[^:]+:%d+: attempt to",
            "100 >> '5'")

    error_matches(function () return 3 << '2' end,
            "^[^:]+:%d+: attempt to",
            "3 << '2'")
else
    equals(3 & '7', 3, "3 & '7'")

    equals(4 | '1', 5, "4 | '1'")

    equals(7 ~ '1', 6, "7 ~ '1'")

    equals(100 >> '5', 3, "100 >> '5'")

    equals(3 << '2', 12, "3 << '2'")
end

error_matches(function () return 3.5 & 7 end,
        "^[^:]+:%d+: number has no integer representation",
        "3.5 & 7")

error_matches(function () return 3 & 7.5 end,
        "^[^:]+:%d+: number has no integer representation",
        "3 & 7.5")

error_matches(function () return 4.5 | 1 end,
        "^[^:]+:%d+: number has no integer representation",
        "4.5 | 1")

error_matches(function () return 4 | 1.5 end,
        "^[^:]+:%d+: number has no integer representation",
        "4 | 1.5")

error_matches(function () return 7.5 ~ 1 end,
        "^[^:]+:%d+: number has no integer representation",
        "7.5 ~ 1")

error_matches(function () return 7 ~ 1.5 end,
        "^[^:]+:%d+: number has no integer representation",
        "7 ~ 1.5")

error_matches(function () return 100.5 >> 5 end,
        "^[^:]+:%d+: number has no integer representation",
        "100.5 >> 5")

error_matches(function () return 100 >> 5.5 end,
        "^[^:]+:%d+: number has no integer representation",
        "100 >> 5.5")

error_matches(function () return 3.5 << 2 end,
        "^[^:]+:%d+: number has no integer representation",
        "3.5 << 2")

error_matches(function () return 3 << 2.5 end,
        "^[^:]+:%d+: number has no integer representation",
        "3 << 2.5")

-- Local Variables:
--   mode: lua
--   lua-indent-level: 4
--   fill-column: 100
-- End:
-- vim: ft=lua expandtab shiftwidth=4:
