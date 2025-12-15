#! /usr/bin/lua
--
-- lua-Harness : <https://fperrad.frama.io/lua-Harness/>
--
-- Copyright (C) 2010-2021, Perrad Francois
--
-- This code is licensed under the terms of the MIT/X11 license,
-- like Lua itself.
--

--[[

=head1 Lua Stand-alone

=head2 Synopsis

    % prove t/242-luac.t

=head2 Description

See section "Lua Stand-alone" in "Reference Manual"
L<https://www.lua.org/manual/5.1/manual.html#6>,
L<https://www.lua.org/manual/5.2/manual.html#7>,
L<https://www.lua.org/manual/5.3/manual.html#7>,
L<https://www.lua.org/manual/5.4/manual.html#7>

=cut

--]]

require'test_assertion'

if jit then
    skip_all("LuaJIT")
end

if ravi then
    skip_all("ravi")
end

local lua = _retrieve_progname()
local luac = lua .. 'c'

if not io.open(luac, 'r') then
    skip_all "no luac"
end

if not pcall(io.popen, lua .. [[ -e "a=1"]]) then
    skip_all "io.popen not supported"
end

plan'no_plan'
diag(luac)

local signature = "\x1bLua"
local bin_version
if     _VERSION == 'Lua 5.1' then
    bin_version = "\x51"
elseif _VERSION == 'Lua 5.2' then
    bin_version = "\x52"
elseif _VERSION == 'Lua 5.3' then
    bin_version = "\x53"
elseif _VERSION == 'Lua 5.4' then
    bin_version = "\x54"
end
local format = "\x00"
local data = "\x19\x93\r\n\x1a\n"
local size_i = string.char(string.packsize and string.packsize'i' or 0) -- int
local size_T = string.char(string.packsize and string.packsize'T' or 0) -- size_t
local size_I = string.char(string.packsize and string.packsize'I' or 0) -- Instruction
local size_j = string.char(string.packsize and string.packsize'j' or 0) -- lua_Integer
local size_n = string.char(string.packsize and string.packsize'n' or 0) -- lua_Number
local sizes = size_i .. size_T .. size_I .. size_j .. size_n

do -- hello.lua
    local f = io.open('hello-242.lua', 'w')
    f:write([[
print 'Hello World'
]])
    f:close()
end

do -- luac -v
    local cmd = luac .. [[ -v 2>&1]]
    local f = io.popen(cmd)
    matches(f:read'*l', '^Lua', "-v")
    f:close()
end

do -- luac -v --
    local cmd = luac .. [[ -v -- 2>&1]]
    local f = io.popen(cmd)
    matches(f:read'*l', '^Lua', "-v --")
    f:close()
end

do -- luac -u
    local cmd = luac .. [[ -u 2>&1]]
    local f = io.popen(cmd)
    matches(f:read'*l', "^[^:]+: unrecognized option '%-u'", "unknown option")
    matches(f:read'*l', "^usage:")
    f:close()
end

do -- luac --u
    local cmd = luac .. [[ --u 2>&1]]
    local f = io.popen(cmd)
    matches(f:read'*l', "^[^:]+: unrecognized option '%-%-u'", "unknown option")
    matches(f:read'*l', "^usage:")
    f:close()
end

do -- luac -p hello-242.lua
    local cmd = luac .. [[ -p hello-242.lua 2>&1]]
    local f = io.popen(cmd)
    equals(f:read'*l', nil)
    f:close()
end

do -- luac -p - < hello-242.lua
    local cmd = luac .. [[ -p - < hello-242.lua 2>&1]]
    local f = io.popen(cmd)
    equals(f:read'*l', nil)
    f:close()
end

do -- luac -p no_file-242.lua
    local cmd = luac .. [[ -p no_file-242.lua 2>&1]]
    local f = io.popen(cmd)
    matches(f:read'*l', "^[^:]+: cannot open no_file%-242%.lua", "no file")
    f:close()
end

do -- luac -o
    local cmd = luac .. [[ -o 2>&1]]
    local f = io.popen(cmd)
    matches(f:read'*l', "^[^:]+: '%-o' needs argument", "-o needs argument")
    f:close()
end

do -- luac -v -l -l hello-242.lua
    local cmd = luac .. [[ -v -l -l hello-242.lua]]
    local f = io.popen(cmd)
    matches(f:read'*l', '^Lua', "-v -l -l")
    equals(f:read'*l', '')
    matches(f:read'*l', "^main")
    f:close()
end

os.remove('hello-242.lua') -- clean up

do -- luac -l luac.out
    local cmd = luac .. [[ -l luac.out]]
    local f = io.popen(cmd)
    equals(f:read'*l', '', "-l luac.out")
    matches(f:read'*l', "^main")
    f:close()
end

do -- luac -l
    local cmd = luac .. [[ -l]]
    local f = io.popen(cmd)
    equals(f:read'*l', '', "-l")
    matches(f:read'*l', "^main")
    f:close()
end

do -- luac -l - < luac.out
    local cmd = luac .. [[ -l - < luac.out]]
    local f = io.popen(cmd)
    equals(f:read'*l', '', "-l -")
    matches(f:read'*l', "^main")
    f:close()
end

if _VERSION ~= 'Lua 5.1' then
    local f = io.open('luac.out', 'w')
    f:write(signature .. bin_version .. format)
    f:close()
    local cmd = luac .. [[ luac.out 2>&1]]
    f = io.popen(cmd)
    if _VERSION <= 'Lua 5.3' then
        matches(f:read'*l', "truncated precompiled chunk")
    else
        matches(f:read'*l', "bad binary format %(truncated chunk%)")
    end
    f:close()
end

if _VERSION ~= 'Lua 5.1' then -- bad signature
    local f = io.open('luac.out', 'w')
    f:write("\x1bFoo" .. bin_version .. format .. data .. sizes .. "Foo")
    f:close()
    local cmd = luac .. [[ luac.out 2>&1]]
    f = io.popen(cmd)
    if _VERSION <= 'Lua 5.3' then
        matches(f:read'*l', "not a precompiled chunk", "bad signature")
    else
        matches(f:read'*l', "bad binary format %(not a binary chunk%)", "bad signature")
    end
    f:close()
end

if _VERSION ~= 'Lua 5.1' then -- bad version
    local f = io.open('luac.out', 'w')
    f:write(signature .. "\x51" .. format .. data .. sizes .. "Foo")
    f:close()
    local cmd = luac .. [[ luac.out 2>&1]]
    f = io.popen(cmd)
    if _VERSION <= 'Lua 5.3' then
        matches(f:read'*l', "version mismatch in precompiled chunk", "bad version")
    else
        matches(f:read'*l', "bad binary format %(version mismatch%)", "bad version")
    end
    f:close()
end

if _VERSION ~= 'Lua 5.1' then -- bad format
    local f = io.open('luac.out', 'w')
    f:write(signature .. bin_version .. "\x42" .. data .. sizes .. "Foo")
    f:close()
    local cmd = luac .. [[ luac.out 2>&1]]
    f = io.popen(cmd)
    if _VERSION == 'Lua 5.2' then
        matches(f:read'*l', "version mismatch in precompiled chunk", "bad format")
    elseif _VERSION == 'Lua 5.3' then
        matches(f:read'*l', "format mismatch in precompiled chunk", "bad format")
    else
        matches(f:read'*l', "bad binary format %(format mismatch%)", "bad format")
    end
    f:close()
end

if _VERSION == 'Lua 5.2' then -- bad sizes
    local f = io.open('luac.out', 'w')
    f:write(signature .. bin_version .. format .. "\xde\xad\xbe\xef\x00" .. data .. "Foo")
    f:close()
    local cmd = luac .. [[ luac.out 2>&1]]
    f = io.popen(cmd)
    matches(f:read'*l', "incompatible precompiled chunk", "incompatible 5.2")
    f:close()
end

if _VERSION == 'Lua 5.2' then -- bad data / tail
    sizes = string.dump(load "a = 1"):sub(7, 12)
    local f = io.open('luac.out', 'w')
    f:write(signature .. bin_version .. format .. sizes .. "\x19\x99\r\n\x1a\n")
    f:close()
    local cmd = luac .. [[ luac.out 2>&1]]
    f = io.popen(cmd)
    matches(f:read'*l', "corrupted precompiled chunk", "corrupted 5.2")
    f:close()
end

if _VERSION == 'Lua 5.3' then -- bad data
    local f = io.open('luac.out', 'w')
    f:write(signature .. bin_version .. format .. "\x19\x99\r\n\x1a\n" .. sizes)
    f:close()
    local cmd = luac .. [[ luac.out 2>&1]]
    f = io.popen(cmd)
    matches(f:read'*l', "corrupted precompiled chunk", "corrupted")
    f:close()
end

if _VERSION == 'Lua 5.3' then -- bad sizes
    local f = io.open('luac.out', 'w')
    f:write(signature .. bin_version .. format .. data .. "\xde\xad\xbe\xef\x00")
    f:close()
    local cmd = luac .. [[ luac.out 2>&1]]
    f = io.popen(cmd)
    matches(f:read'*l', "int size mismatch in precompiled chunk", "bad sizes")
    f:close()
end

if _VERSION == 'Lua 5.3' then -- bad endianess
    local f = io.open('luac.out', 'w')
    f:write(signature .. bin_version .. format .. data .. sizes .. "\0\0\0\0\0\0\0\0")
    f:close()
    local cmd = luac .. [[ luac.out 2>&1]]
    f = io.popen(cmd)
    matches(f:read'*l', "endianness mismatch in precompiled chunk", "bad endian")
    f:close()
end

if _VERSION == 'Lua 5.3' then -- bad float format
    local endian = string.dump(load "a = 1"):sub(18, 18 + string.packsize'n')
    local f = io.open('luac.out', 'w')
    f:write(signature .. bin_version .. format .. data .. sizes .. endian .. "\0\0\0\0\0\0\0\0")
    f:close()
    local cmd = luac .. [[ luac.out 2>&1]]
    f = io.popen(cmd)
    matches(f:read'*l', "float format mismatch in precompiled chunk")
    f:close()
end

do -- cover.lua
    local f = io.open('cover-242.lua', 'w')
    f:write([[
local a = false
b = a + 1
pi = 3.14
s = "all escaped \1\a\b\f\n\r\t\v\\\""
local t = { "a", "b", "c", "d", [true] = 1 }
local f = table.concat
local function f ()
    while true do
        print(a)
    end
end
local function g (...) -- segfault with Lua 5.4.0-beta
    return {...}
end
s = nil
]])
    f:close()

    local cmd = luac .. [[ -o cover-242.out cover-242.lua 2>&1]]
    f = io.popen(cmd)
    equals(f:read'*l', nil, "-o cover-242.out cover-242.lua")
    f:close()

    cmd = luac .. [[ -l cover-242.out]]
    f = io.popen(cmd)
    equals(f:read'*l', '', "-l cover-242.out")
    matches(f:read'*l', "^main")
    f:close()

    cmd = luac .. [[ -l -l cover-242.out]]
    f = io.popen(cmd)
    equals(f:read'*l', '', "-l -l cover-242.out")
    matches(f:read'*l', "^main")
    f:close()
end

os.remove('luac.out') -- clean up
os.remove('cover-242.lua') -- clean up
os.remove('cover-242.out') -- clean up
done_testing()

-- Local Variables:
--   mode: lua
--   lua-indent-level: 4
--   fill-column: 100
-- End:
-- vim: ft=lua expandtab shiftwidth=4:
