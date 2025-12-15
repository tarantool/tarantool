--
-- lua-Harness : <https://fperrad.frama.io/lua-Harness/>
--
-- Copyright (C) 2019-2021, Perrad Francois
--
-- This code is licensed under the terms of the MIT/X11 license,
-- like Lua itself.
--

equals("\u{10000}", "\xF0\x90\x80\x80")
equals("\u{200000}", "\xF8\x88\x80\x80\x80")
equals("\u{4000000}", "\xFC\x84\x80\x80\x80\x80")

-- Local Variables:
--   mode: lua
--   lua-indent-level: 4
--   fill-column: 100
-- End:
-- vim: ft=lua expandtab shiftwidth=4:
