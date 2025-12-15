--
-- lua-Harness : <https://fperrad.frama.io/lua-Harness/>
--
-- Copyright (C) 2014-2021, Perrad Francois
--
-- This code is licensed under the terms of the MIT/X11 license,
-- like Lua itself.
--

local has_char54 = _VERSION >= 'Lua 5.4'
local has_charpattern54 = _VERSION >= 'Lua 5.4'

do -- char
    equals(utf8.char(65, 66, 67), 'ABC', "function char")
    equals(utf8.char(0x20AC), '\u{20AC}')
    equals(utf8.char(), '')

    equals(utf8.char(0):len(), 1)
    equals(utf8.char(0x7F):len(), 1)
    equals(utf8.char(0x80):len(), 2)
    equals(utf8.char(0x7FF):len(), 2)
    equals(utf8.char(0x800):len(), 3)
    equals(utf8.char(0xFFFF):len(), 3)
    equals(utf8.char(0x10000):len(), 4)
    equals(utf8.char(0x10FFFF):len(), 4)
    if has_char54 then
        equals(utf8.char(0x1FFFFF):len(), 4)
        equals(utf8.char(0x200000):len(), 5)
        equals(utf8.char(0x3FFFFFF):len(), 5)
        equals(utf8.char(0x4000000):len(), 6)
        equals(utf8.char(0x7FFFFFFF):len(), 6)
    else
        error_matches(function () utf8.char(0x110000) end,
                "^[^:]+:%d+: bad argument #1 to 'char' %(value out of ",
                "function char (out of range)")
    end

    error_matches(function () utf8.char(0, -1) end,
            "^[^:]+:%d+: bad argument #2 to 'char' %(value out of ",
            "function char (out of range)")

     error_matches(function () utf8.char(0, 'bad') end,
            "^[^:]+:%d+: bad argument #2 to 'char' %(number expected, got string%)",
            "function char (bad)")
end

do -- charpattern
    if has_charpattern54 then
        equals(utf8.charpattern, "[\0-\x7F\xC2-\xFD][\x80-\xBF]*", "charpattern")
    else
        equals(utf8.charpattern, "[\0-\x7F\xC2-\xF4][\x80-\xBF]*", "charpattern")
    end
end

do -- codes
    local ap = {}
    local ac = {}
    for p, c in utf8.codes("A\u{20AC}3") do
        ap[#ap+1] = p
        ac[#ac+1] = c
    end
    array_equals(ap, {1, 2, 5}, "function codes")
    array_equals(ac, {0x41, 0x20AC, 0x33})

    local empty = true
    for p, c in utf8.codes('') do
        empty = false
    end
    truthy(empty, "codes (empty)")

    error_matches(function () utf8.codes() end,
            "^[^:]+:%d+: bad argument #1 to 'codes' %(string expected, got no value%)",
            "function codes ()")

    error_matches(function () utf8.codes(true) end,
            "^[^:]+:%d+: bad argument #1 to 'codes' %(string expected, got boolean%)",
            "function codes (true)")

    error_matches(function () for p, c in utf8.codes('invalid\xFF') do end end,
            "^[^:]+:%d+: invalid UTF%-8 code",
            "function codes (invalid)")
end

do -- codepoints
    equals(utf8.codepoint("A\u{20AC}3"), 0x41, "function codepoint")
    equals(utf8.codepoint("A\u{20AC}3", 2), 0x20AC)
    equals(utf8.codepoint("A\u{20AC}3", -1), 0x33)
    equals(utf8.codepoint("A\u{20AC}3", 5), 0x33)
    array_equals({utf8.codepoint("A\u{20AC}3", 1, 5)}, {0x41, 0x20AC, 0x33})
    array_equals({utf8.codepoint("A\u{20AC}3", 1, 4)}, {0x41, 0x20AC})

    error_matches(function () utf8.codepoint("A\u{20AC}3", 6) end,
            "^[^:]+:%d+: bad argument #3 to 'codepoint' %(out of ",
            "function codepoint (out of range)")

    error_matches(function () utf8.codepoint("A\u{20AC}3", 8) end,
            "^[^:]+:%d+: bad argument #3 to 'codepoint' %(out of ",
            "function codepoint (out of range)")

    error_matches(function () utf8.codepoint("invalid\xFF", 8) end,
            "^[^:]+:%d+: invalid UTF%-8 code",
            "function codepoint (invalid)")
end

do -- len
    equals(utf8.len('A'), 1, "function len")
    equals(utf8.len(''), 0)
    equals(utf8.len("\u{41}\u{42}\u{43}"), 3)
    equals(utf8.len("A\u{20AC}3"), 3)

    equals(utf8.len('A', 1), 1)
    equals(utf8.len('A', 2), 0)
    equals(utf8.len('ABC', 1, 1), 1)
    equals(utf8.len('ABC', 2, 2), 1)
    equals(utf8.len('ABC', -1), 1)
    equals(utf8.len('ABC', -2), 2)

    error_matches(function () utf8.len('A', 3) end,
            "^[^:]+:%d+: bad argument #2 to 'len' %(initial position out of ",
            "function len (out of range)")

    local len, pos = utf8.len('invalid\xFF')
    equals(len, nil, "function len (invalid)")
    equals(pos, 8)
end

do -- offset
    equals(utf8.offset("A\u{20AC}3", 1), 1, "function offset")
    equals(utf8.offset("A\u{20AC}3", 2), 2)
    equals(utf8.offset("A\u{20AC}3", 3), 5)
    equals(utf8.offset("A\u{20AC}3", 4), 6)
    equals(utf8.offset("A\u{20AC}3", 5), nil)
    equals(utf8.offset("A\u{20AC}3", 6), nil)
    equals(utf8.offset("A\u{20AC}3", -1), 5)
    equals(utf8.offset("A\u{20AC}3", 1, 2), 2)
    equals(utf8.offset("A\u{20AC}3", 2, 2), 5)
    equals(utf8.offset("A\u{20AC}3", 3, 2), 6)
    equals(utf8.offset("A\u{20AC}3", 4, 2), nil)
    equals(utf8.offset("A\u{20AC}3", -1, 2), 1)
    equals(utf8.offset("A\u{20AC}3", -2, 2), nil)
    equals(utf8.offset("A\u{20AC}3", 1, 5), 5)
    equals(utf8.offset("A\u{20AC}3", 2, 5), 6)
    equals(utf8.offset("A\u{20AC}3", 3, 5), nil)
    equals(utf8.offset("A\u{20AC}3", -1, 5), 2)
    equals(utf8.offset("A\u{20AC}3", -2, 5), 1)
    equals(utf8.offset("A\u{20AC}3", -3, 5), nil)
    equals(utf8.offset("A\u{20AC}3", 1, 6), 6)
    equals(utf8.offset("A\u{20AC}3", 2, 6), nil)
    equals(utf8.offset("A\u{20AC}3", 1, -1), 5)
    equals(utf8.offset("A\u{20AC}3", -1, -1), 2)
    equals(utf8.offset("A\u{20AC}3", -2, -1), 1)
    equals(utf8.offset("A\u{20AC}3", -3, -1), nil)
    equals(utf8.offset("A\u{20AC}3", 1, -4), 2)
    equals(utf8.offset("A\u{20AC}3", 2, -4), 5)
    equals(utf8.offset("A\u{20AC}3", -1, -4), 1)
    equals(utf8.offset("A\u{20AC}3", -2, -4), nil)

    equals(utf8.offset("A\u{20AC}3", 0, 1), 1)
    equals(utf8.offset("A\u{20AC}3", 0, 2), 2)
    equals(utf8.offset("A\u{20AC}3", 0, 3), 2)
    equals(utf8.offset("A\u{20AC}3", 0, 4), 2)
    equals(utf8.offset("A\u{20AC}3", 0, 5), 5)
    equals(utf8.offset("A\u{20AC}3", 0, 6), 6)

    error_matches(function () utf8.offset("A\u{20AC}3", 1, 7) end,
            "^[^:]+:%d+: bad argument #3 to 'offset' %(position out of ",
            "function offset (out of range)")

    error_matches(function () utf8.offset("\x80", 1) end,
            "^[^:]+:%d+: initial position is a continuation byte",
            "function offset (continuation byte)")
end

-- Local Variables:
--   mode: lua
--   lua-indent-level: 4
--   fill-column: 100
-- End:
-- vim: ft=lua expandtab shiftwidth=4:
