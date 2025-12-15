#! /usr/bin/lua
--
-- lua-Harness : <https://fperrad.frama.io/lua-Harness/>
--
-- Copyright (C) 2009-2020, Perrad Francois
--
-- This code is licensed under the terms of the MIT/X11 license,
-- like Lua itself.
--

--[[

=head1 Lua while statement

=head2 Synopsis

    % prove 011-while.t

=head2 Description

See section "Control Structures" in "Reference Manual"
L<https://www.lua.org/manual/5.1/manual.html#2.4.4>,
L<https://www.lua.org/manual/5.2/manual.html#3.3.4>,
L<https://www.lua.org/manual/5.3/manual.html#3.3.4>,
L<https://www.lua.org/manual/5.4/manual.html#3.3.4>

=cut

]]

print("1..11")

do
    local a = {}
    local i = 1
    while a[i] do
        i = i + 1
    end
    if i == 1 then
        print("ok 1 - while empty")
    else
        print("not ok 1 - " .. i)
    end
end

do
    local a = {"ok 2 - while ", "ok 3", "ok 4"}
    local i = 1
    while a[i] do
        print(a[i])
        i = i + 1
    end
end

do
    local a = {"ok 5 - with break", "ok 6", "stop", "more"}
    local i = 1
    while a[i] do
        if a[i] == 'stop' then break end
        print(a[i])
        i = i + 1
    end
    if i == 3 then
        print("ok 7 - break")
    else
        print("not ok 7 - " .. i)
    end
end

do
    local x = 3
    local i = 1
    while i<=x do
        print("ok " .. tostring(7+i))
        i = i + 1
    end
    if i == 4 then
        print("ok 11")
    else
        print("not ok 11 - " .. tostring(i))
    end
end

-- Local Variables:
--   mode: lua
--   lua-indent-level: 4
--   fill-column: 100
-- End:
-- vim: ft=lua expandtab shiftwidth=4:
