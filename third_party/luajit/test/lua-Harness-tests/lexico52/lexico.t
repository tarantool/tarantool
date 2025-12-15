--
-- lua-Harness : <https://fperrad.frama.io/lua-Harness/>
--
-- Copyright (C) 2012-2021, Perrad Francois
--
-- This code is licensed under the terms of the MIT/X11 license,
-- like Lua itself.
--

equals("\x41", "A")
equals("\x3d", "=")
equals("\x3D", "=")

do
    local f, msg = load [[a = "A\xyz"]]
    matches(msg, "^[^:]+:%d+: .- near")

    f, msg = load [[a = "A\Z"]]
    matches(msg, "^[^:]+:%d+: .- escape .- near")
end

do
    local a = 'alo\n123"'
    equals("alo\n\z
    123\"", a)

    local f, msg = load [[a = " escape \z unauthorized
new line" ]]
    matches(msg, "^[^:]+:%d+: unfinished string near")
end

equals(0x0.1E, 0x1E / 0x100)    -- 0.1171875
equals(0xA23p-4, 0xA23 / (2^4)) -- 162.1875
if string.pack and #string.pack('n', 0) == 4 then
    diag('Small Lua')
else
    equals(0X1.921FB54442D18P+1, (1 + 0x921FB54442D18/0x10000000000000) * 2)
end

-- Local Variables:
--   mode: lua
--   lua-indent-level: 4
--   fill-column: 100
-- End:
-- vim: ft=lua expandtab shiftwidth=4:
