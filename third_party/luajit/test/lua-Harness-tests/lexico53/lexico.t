--
-- lua-Harness : <https://fperrad.frama.io/lua-Harness/>
--
-- Copyright (C) 2015-2021, Perrad Francois
--
-- This code is licensed under the terms of the MIT/X11 license,
-- like Lua itself.
--

equals("\u{41}", "A")
equals("\u{20AC}", "\xE2\x82\xAC")
equals("\u{20ac}", "\xe2\x82\xac")

do
    local f, msg = load [[a = "A\u{yz}"]]
    matches(msg, "^[^:]+:%d+: .- near")

    f, msg = load [[a = "A\u{41"]]
    matches(msg, "^[^:]+:%d+: .- near")

    f, msg = load [[a = "A\u{FFFFFFFFFF}"]]
    matches(msg, "^[^:]+:%d+: .- near")
end

-- Local Variables:
--   mode: lua
--   lua-indent-level: 4
--   fill-column: 100
-- End:
-- vim: ft=lua expandtab shiftwidth=4:
