#! /usr/bin/lua
--
-- lua-Harness : <https://fperrad.frama.io/lua-Harness/>
--
-- Copyright (C) 2018-2021, Perrad Francois
--
-- This code is licensed under the terms of the MIT/X11 license,
-- like Lua itself.
--

--[[

=head1 Lua test suite

=head2 Synopsis

    % prove 090-tap.t

=head2 Description

=cut

]]

require'test_assertion'

plan(3)
truthy( true, 'truthy' )
equals( 42, 42, '42 == 42' )
passes( 'pass' )

-- Local Variables:
--   mode: lua
--   lua-indent-level: 4
--   fill-column: 100
-- End:
-- vim: ft=lua expandtab shiftwidth=4:
