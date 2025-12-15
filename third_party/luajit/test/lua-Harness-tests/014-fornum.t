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

    % prove 014-fornum.t

=head2 Description

See section "For Statement" in "Reference Manual"
L<https://www.lua.org/manual/5.1/manual.html#2.4.5>,
L<https://www.lua.org/manual/5.2/manual.html#3.3.5>,
L<https://www.lua.org/manual/5.3/manual.html#3.3.5>,
L<https://www.lua.org/manual/5.4/manual.html#3.3.5>

=cut

--]]

print("1..36")

for i = 1.0, 3.0, 0.5 do
    print("ok " .. tostring(2*i-1) .. " - for 1.0, 3.0, 0.5")
end

for i = 1.0, 3.0, 0.5 do
    local function f ()
        print("ok " .. tostring(2*i+4) .. " - for 1.0, 3.0, 0.5 lex")
    end
    f()
end

local function f (i)
    print("ok " .. tostring(2*i+9) .. " - for 1.0, 3.0, 0.5 !lex")
end
for i = 1.0, 3.0, 0.5 do
    f(i)
end

for i = 3, 5 do
    print("ok " .. tostring(13+i) .. " - for 3, 5")
    i = i + 1
end

for i = 5, 1, -1 do
    print("ok " .. tostring(24-i) .. " - for 5, 1, -1")
end

for i = 5, 5 do
    print("ok " .. tostring(19+i) .. " - for 5, 5")
end

for i = 5, 5, -1 do
    print("ok " .. tostring(20+i) .. " - for 5, 5, -1")
end

do
    local v = false
    for i = 5, 3 do
        v = true
    end
    if v then
        print("not ok 26 - for 5, 3")
    else
        print("ok 26 - for 5, 3")
    end
end

do
    local v = false
    for i = 5, 7, -1 do
        v = true
    end
    if v then
        print("not ok 27 - for 5, 7, -1")
    else
        print("ok 27 - for 5, 7, -1")
    end
end

do
    local v = false
    if _VERSION <= 'Lua 5.3' then
        for i = 5, 7, 0 do
            v = true
            break -- avoid infinite loop with LuaJIT
        end
    end
    if jit then
        print("not ok 28 - for 5, 7, 0 # TODO # LuaJIT intentional.")
    elseif v then
        print("not ok 28 - for 5, 7, 0")
    else
        print("ok 28 - for 5, 7, 0")
    end
end

do
    local v = nil
    for i = 1, 10, 2 do
        if i > 4 then break end
        print("ok " .. tostring((i+57)/2) .. " - for break")
        v = i
    end
    if v == 3 then
        print("ok 31 - break")
    else
        print("not ok 31 - " .. v)
    end
end

do
    local function first() return 1 end
    local function limit() return 8 end
    local function step()  return 2 end
    for i = first(), limit(), step() do
        print("ok " .. tostring((i+63)/2) .. " - with functions")
    end
end

do
    local a = {}
    for i = 1, 10 do
        a[i] = function () return i end
    end
    local v = a[5]()
    if v == 5 then
        print("ok 36 - for & upval")
    else
        print("not ok 36 - for & upval")
        print("#", v)
    end
end

-- Local Variables:
--   mode: lua
--   lua-indent-level: 4
--   fill-column: 100
-- End:
-- vim: ft=lua expandtab shiftwidth=4:
