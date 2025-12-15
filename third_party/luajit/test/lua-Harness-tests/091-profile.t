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

    % prove 091-profile.t

=head2 Description

=cut

]]

require'test_assertion'

plan'no_plan'

is_string(_VERSION, "variable _VERSION")
matches(_VERSION, '^Lua 5%.%d$')

if jit then
    is_number(jit.version_num, "variable jit.version_num")
end

require_ok'profile'

done_testing()

-- Local Variables:
--   mode: lua
--   lua-indent-level: 4
--   fill-column: 100
-- End:
-- vim: ft=lua expandtab shiftwidth=4:
