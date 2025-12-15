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

=head1 Lua for statement

=head2 Synopsis

    % prove 015-forlist.t

=head2 Description

See section "For Statement" in "Reference Manual"
L<https://www.lua.org/manual/5.1/manual.html#2.4.5>,
L<https://www.lua.org/manual/5.2/manual.html#3.3.5>,
L<https://www.lua.org/manual/5.3/manual.html#3.3.5>,
L<https://www.lua.org/manual/5.4/manual.html#3.3.5>

=cut

--]]

print("1..18")

do
    local a = {"ok 1 - for ipairs", "ok 2 - for ipairs", "ok 3 - for ipairs"}

    for _, v in ipairs(a) do
        print(v)
    end

    for i, v in ipairs(a) do
        print("ok " .. tostring(3+i) .. " - for ipairs")
    end

    for k in pairs(a) do
        print("ok " .. tostring(6+k) .. " - for pairs")
    end
end

do
    local r = false
    local t = {a=10, b=100}

    for i, v in ipairs(t) do
        print(i, v)
        r = true
    end
    if r then
        print("not ok 10 - for ipairs (hash)")
    else
        print("ok 10 - for ipairs (hash)")
    end

    local i = 1
    for k in pairs(t) do
        if k == 'a' or k == 'b' then
            print("ok " .. tostring(10+i) .. " - for pairs (hash)")
        else
            print("not ok " .. tostring(10+i) .. " - " .. k)
        end
        i = i + 1
    end
end

do
    local a = {"ok 13 - for break", "ok 14 - for break", "stop", "more"}
    local i
    for _, v in ipairs(a) do
        if v == "stop" then break end
        print(v)
        i = _
    end
    if i == 2 then
        print("ok 15 - break")
    else
        print("not ok 15 - " .. i)
    end
end

do
    local a = {"ok 16 - for & upval", "ok 17 - for & upval", "ok 18 - for & upval"}
    local b = {}
    for i, v in ipairs(a) do
        b[i] = function () return v end
    end
    for i, v in ipairs(a) do
        local r = b[i]()
        if r == a[i] then
            print(r)
        else
            print("not " .. a[i])
            print("#", r)
        end
    end
end

-- Local Variables:
--   mode: lua
--   lua-indent-level: 4
--   fill-column: 100
-- End:
-- vim: ft=lua expandtab shiftwidth=4:
