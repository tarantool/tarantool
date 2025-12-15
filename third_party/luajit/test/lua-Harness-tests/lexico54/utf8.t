--
-- lua-Harness : <https://fperrad.frama.io/lua-Harness/>
--
-- Copyright (C) 2019-2021, Perrad Francois
--
-- This code is licensed under the terms of the MIT/X11 license,
-- like Lua itself.
--

do -- codes
    local ap = {}
    local ac = {}
    for p, c in utf8.codes("A\u{200000}3", true) do
        ap[#ap+1] = p
        ac[#ac+1] = c
    end
    array_equals(ap, {1, 2, 7}, "function codes lax")
    array_equals(ac, {0x41, 0x200000, 0x33})

    error_matches(function () for _ in utf8.codes("A\u{200000}3", false) do end end,
            "^[^:]+:%d+: invalid UTF%-8 code")

    error_matches(function () for _ in utf8.codes("A\u{200000}3") do end end,
            "^[^:]+:%d+: invalid UTF%-8 code")
end

do -- codepoints
    array_equals({utf8.codepoint("A\u{200000}3", 1, 7, true)}, {0x41, 0x200000, 0x33}, "function codepoint lax")

    error_matches(function () utf8.codepoint("A\u{200000}3", 1, 7, false) end,
            "^[^:]+:%d+: invalid UTF%-8 code")

    error_matches(function () utf8.codepoint("A\u{200000}3", 1, 7) end,
            "^[^:]+:%d+: invalid UTF%-8 code")
end

do -- len
    equals(utf8.len('A\u{200000}C', 1, -1, true), 3, "function len lax")

    local len, pos = utf8.len('A\u{200000}C')
    equals(len, nil)
    equals(pos, 2)

    len, pos = utf8.len('A\u{200000}C', 1, -1, false)
    equals(len, nil)
    equals(pos, 2)
end

-- Local Variables:
--   mode: lua
--   lua-indent-level: 4
--   fill-column: 100
-- End:
-- vim: ft=lua expandtab shiftwidth=4:
