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

=head1 Lua repeat statement

=head2 Synopsis

    % prove 012-repeat.t

=head2 Description

See section "Control Structures" in "Reference Manual"
L<https://www.lua.org/manual/5.1/manual.html#2.4.4>,
L<https://www.lua.org/manual/5.2/manual.html#3.3.4>,
L<https://www.lua.org/manual/5.3/manual.html#3.3.4>,
L<https://www.lua.org/manual/5.4/manual.html#3.3.4>

=cut

]]

print("1..8")

do
    local a = {"ok 1 - repeat", "ok 2", "ok 3"}
    local i = 0
    repeat
        i = i + 1
        if a[i] then
            print(a[i])
        end
    until not a[i]
    if i == 4 then
        print("ok 4")
    else
        print("not ok 4 - " .. i)
    end
end

do
    local a = {"ok 5 - with break", "ok 6", 'stop', 'more'}
    local i = 0
    repeat
        i = i + 1
        if a[i] == 'stop' then break end
        print(a[i])
    until not a[i]
    if a[i] == 'stop' then
        print("ok 7 - break")
    else
        print("not ok 7 - " .. a[i])
    end
end

do
    local function f () return true end

    local i = 1
    repeat
        local v = f()
        if i == 1 then
            print("ok 8 - scope")
        else
            print("not ok")
            break
        end
        i = i + 1
    until v
end

-- Local Variables:
--   mode: lua
--   lua-indent-level: 4
--   fill-column: 100
-- End:
-- vim: ft=lua expandtab shiftwidth=4:
