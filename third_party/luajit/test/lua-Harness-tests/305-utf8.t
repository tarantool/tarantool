#! /usr/bin/lua
--
-- lua-Harness : <https://fperrad.frama.io/lua-Harness/>
--
-- Copyright (C) 2014-2021, Perrad Francois
--
-- This code is licensed under the terms of the MIT/X11 license,
-- like Lua itself.
--

--[[

=head1 Lua UTF-8 support Library

=head2 Synopsis

    % prove 305-utf8.t

=head2 Description

Tests Lua UTF-8 Library

This library was introduced in Lua 5.3.

See section "UTF-8 support" in "Reference Manual"
L<https://www.lua.org/manual/5.3/manual.html#6.5>,
L<https://www.lua.org/manual/5.4/manual.html#6.5>

=cut

--]]

require 'test_assertion'

local profile = require'profile'
local has_utf8 = _VERSION >= 'Lua 5.3' or (jit and jit.version:match'moonjit') or profile.utf8

if not utf8 then
    plan(1)
    falsy(has_utf8, "no has_utf8")
else
    plan'no_plan'
    _dofile'lexico53/utf8.t'
    if _VERSION >= 'Lua 5.4' then
        _dofile'lexico54/utf8.t'
    end
    done_testing()
end

-- Local Variables:
--   mode: lua
--   lua-indent-level: 4
--   fill-column: 100
-- End:
-- vim: ft=lua expandtab shiftwidth=4:
