#! /usr/bin/lua
--
-- lua-Harness : <https://fperrad.frama.io/lua-Harness/>
--
-- Copyright (C) 2009-2021, Perrad Francois
--
-- This code is licensed under the terms of the MIT/X11 license,
-- like Lua itself.
--

--[[

=head1 Lua scope

=head2 Synopsis

    % prove 211-scope.t

=head2 Description

See section "Visibility Rules" in "Reference Manual"
L<https://www.lua.org/manual/5.1/manual.html#2.6>,
L<https://www.lua.org/manual/5.2/manual.html#3.5>,
L<https://www.lua.org/manual/5.3/manual.html#3.5>,
L<https://www.lua.org/manual/5.4/manual.html#3.5>

See section "Local Variables and Blocks" in "Programming in Lua".

=cut

--]]

require'test_assertion'

plan(10)

--[[ scope ]]
x = 10
do
    local x = x
    equals(x, 10, "scope")
    x = x + 1
    do
        local x = x + 1
        equals(x, 12)
    end
    equals(x, 11)
end
equals(x, 10)

--[[ scope ]]
x = 10
local i = 1

while i<=x do
    local x = i*2
--    print(x)
    i = i + 1
end

if i > 20 then
    local x
    x = 20
    fails("scope")
else
    equals(x, 10, "scope")
end

equals(x, 10)

--[[ scope ]]
local a, b = 1, 10
if a < b then
    equals(a, 1, "scope")
    local a
    equals(a, nil)
end
equals(a, 1)
equals(b, 10)

-- Local Variables:
--   mode: lua
--   lua-indent-level: 4
--   fill-column: 100
-- End:
-- vim: ft=lua expandtab shiftwidth=4:
