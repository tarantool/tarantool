--
-- lua-Harness : <https://fperrad.frama.io/lua-Harness/>
--
-- Copyright (C) 2018-2021, Perrad Francois
--
-- This code is licensed under the terms of the MIT/X11 license,
-- like Lua itself.
--

do -- tonumber
    equals(tonumber(42uLL), 42, "function tonumber (cdata)")
    equals(tonumber(42LL), 42)
end

do -- tostring
    equals(tostring(42uLL), '42ULL', "function tostring (cdata)")
    equals(tostring(42LL), '42LL')
    equals(tostring(1i), '0+1i')
    equals(tostring(12.5i), '0+12.5i')
end

-- Local Variables:
--   mode: lua
--   lua-indent-level: 4
--   fill-column: 100
-- End:
-- vim: ft=lua expandtab shiftwidth=4:
