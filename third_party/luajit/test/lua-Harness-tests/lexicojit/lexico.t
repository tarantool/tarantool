--
-- lua-Harness : <https://fperrad.frama.io/lua-Harness/>
--
-- Copyright (C) 2018-2021, Perrad Francois
--
-- This code is licensed under the terms of the MIT/X11 license,
-- like Lua itself.
--

is_cdata(42LL, "42LL")
is_cdata(42ULL, "42ULL")
is_cdata(42uLL, "42uLL")
is_cdata(42ull, "42ull")

is_cdata(0x2aLL, "0x2aLL")
is_cdata(0x2aULL, "0x2aULL")
is_cdata(0x2auLL, "0x2auLL")
is_cdata(0x2aull, "0x2aull")

is_cdata(12.5i, '12.5i')
is_cdata(12.5I, '12.5I')
is_cdata(1i, '1i')
is_cdata(1I, '1I')
is_cdata(0i, '0i')
is_cdata(0I, '0I')

-- Local Variables:
--   mode: lua
--   lua-indent-level: 4
--   fill-column: 100
-- End:
-- vim: ft=lua expandtab shiftwidth=4:
