--
-- lua-Harness : <https://fperrad.frama.io/lua-Harness/>
--
-- Copyright (C) 2009-2021, Perrad Francois
--
-- This code is licensed under the terms of the MIT/X11 license,
-- like Lua itself.
--

local co = coroutine.create(function () return 1 end)

error_matches(function () return ~co end,
        "^[^:]+:%d+: attempt to perform bitwise operation on",
        "~co")

error_matches(function () return co // 3 end,
        "^[^:]+:%d+: attempt to perform arithmetic on",
        "co // 3")

error_matches(function () return co & 7 end,
        "^[^:]+:%d+: attempt to perform bitwise operation on",
        "co & 7")

error_matches(function () return co | 1 end,
        "^[^:]+:%d+: attempt to perform bitwise operation on",
        "co | 1")

error_matches(function () return co ~ 4 end,
        "^[^:]+:%d+: attempt to perform bitwise operation on",
        "co ~ 4")

error_matches(function () return co >> 5 end,
        "^[^:]+:%d+: attempt to perform bitwise operation on",
        "co >> 5")

error_matches(function () return co << 2 end,
        "^[^:]+:%d+: attempt to perform bitwise operation on",
        "co << 2")

-- Local Variables:
--   mode: lua
--   lua-indent-level: 4
--   fill-column: 100
-- End:
-- vim: ft=lua expandtab shiftwidth=4:
