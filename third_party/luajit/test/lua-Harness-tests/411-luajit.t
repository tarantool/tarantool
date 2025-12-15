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

=head1 LuaJIT Stand-alone

=head2 Synopsis

    % prove 411-luajit.t

=head2 Description

See L<https://luajit.org/running.html>

=cut

--]]

require'test_assertion'
local profile = require'profile'

if not jit or ujit then
    skip_all("only with LuaJIT")
end

-- XXX: Unfortunately, Lua patterns do not support optional
-- capture groups, so the helper below implements poor man's
-- optional capture groups for the patterns matching LuaJIT CLI
-- error messages.
local function errbuild(message)
    local errprefix = _TARANTOOL and "" or "[^:]+: "
    return table.concat({"^", errprefix, message})
end

local lua = _retrieve_progname()

if not pcall(io.popen, lua .. [[ -e "a=1"]]) then
    skip_all("io.popen not supported")
end

local compiled_with_jit = jit.opt ~= nil
local has_jutil = pcall(require, 'jit.util')
local has_openresty_listing = profile.openresty or jit.version:match'moonjit'

plan'no_plan'
diag(lua)

local f = io.open('hello-411.lua', 'w')
f:write([[
print 'Hello World'
]])
f:close()

os.execute(lua .. " -b hello-411.lua hello-411.out")
local cmd = lua .. " hello-411.out"
f = io.popen(cmd)
equals(f:read'*l', 'Hello World', "-b")
f:close()

os.execute(lua .. " -bg hello-411.lua hello-411.out")
cmd = lua .. " hello-411.out"
f = io.popen(cmd)
equals(f:read'*l', 'Hello World', "-bg")
f:close()

os.execute(lua .. " -be 'print[[Hello World]]' hello-411.out")
cmd = lua .. " hello-411.out"
f = io.popen(cmd)
equals(f:read'*l', 'Hello World', "-be")
f:close()

os.remove('hello-411.out') -- clean up

if has_jutil then
    cmd = lua .. " -bl hello-411.lua"
    f = io.popen(cmd)
    matches(f:read'*l', '^%-%- BYTECODE %-%- hello%-411%.lua', "-bl hello.lua")
    if has_openresty_listing then
        matches(f:read'*l', '^KGC    0')
        matches(f:read'*l', '^KGC    1')
    end
    matches(f:read'*l', '^0001    %u[%u%d]+%s+')
    matches(f:read'*l', '^0002    %u[%u%d]+%s+')
    matches(f:read'*l', '^0003    %u[%u%d]+%s+')
    f:close()

    os.execute(lua .. " -bl hello-411.lua hello-411.txt")
    f = io.open('hello-411.txt', 'r')
    matches(f:read'*l', '^%-%- BYTECODE %-%- hello%-411%.lua', "-bl hello.lua hello.txt")
    if has_openresty_listing then
        matches(f:read'*l', '^KGC    0')
        matches(f:read'*l', '^KGC    1')
    end
    matches(f:read'*l', '^0001    %u[%u%d]+%s+')
    matches(f:read'*l', '^0002    %u[%u%d]+%s+')
    matches(f:read'*l', '^0003    %u[%u%d]+%s+')
    f:close()
end

if has_openresty_listing then
    cmd = lua .. " -bL hello-411.lua"
    f = io.popen(cmd)
    matches(f:read'*l', '^%-%- BYTECODE %-%- hello%-411%.lua', "-bL hello.lua")
    matches(f:read'*l', '^KGC    0')
    matches(f:read'*l', '^KGC    1')
    matches(f:read'*l', '^0001     %[1%]    %u[%u%d]+%s+')
    matches(f:read'*l', '^0002     %[1%]    %u[%u%d]+%s+')
    matches(f:read'*l', '^0003     %[1%]    %u[%u%d]+%s+')
    f:close()

    os.execute(lua .. " -bL hello-411.lua hello-411.txt")
    f = io.open('hello-411.txt', 'r')
    matches(f:read'*l', '^%-%- BYTECODE %-%- hello%-411%.lua', "-bL hello.lua hello.txt")
    matches(f:read'*l', '^KGC    0')
    matches(f:read'*l', '^KGC    1')
    matches(f:read'*l', '^0001     %[1%]    %u[%u%d]+%s+')
    matches(f:read'*l', '^0002     %[1%]    %u[%u%d]+%s+')
    matches(f:read'*l', '^0003     %[1%]    %u[%u%d]+%s+')
    f:close()
end

os.remove('hello-411.txt') -- clean up

os.execute(lua .. " -b hello-411.lua hello-411.c")
f = io.open('hello-411.c', 'r')
matches(f:read'*l', '^#ifdef __?cplusplus$', "-b hello.lua hello.c")
matches(f:read'*l', '^extern "C"$')
matches(f:read'*l', '^#endif$')
matches(f:read'*l', '^#ifdef _WIN32$')
matches(f:read'*l', '^__declspec%(dllexport%)$')
matches(f:read'*l', '^#endif$')
matches(f:read'*l', '^const.- char luaJIT_BC_hello_411%[%] = {$')
matches(f:read'*l', '^%d+,%d+,%d+,')
f:close()

os.remove('hello-411.c') -- clean up

os.execute(lua .. " -b hello-411.lua hello-411.h")
f = io.open('hello-411.h', 'r')
matches(f:read'*l', '^#define luaJIT_BC_hello_411_SIZE %d+$', "-b hello.lua hello.h")
matches(f:read'*l', '^static const.- char luaJIT_BC_hello_411%[%] = {$')
matches(f:read'*l', '^%d+,%d+,%d+,')
f:close()

os.remove('hello-411.h') -- clean up

cmd = lua .. " -j flush hello-411.lua"
f = io.popen(cmd)
equals(f:read'*l', 'Hello World', "-j flush")
f:close()

cmd = lua .. " -joff hello-411.lua"
f = io.popen(cmd)
equals(f:read'*l', 'Hello World', "-joff")
f:close()

cmd = lua .. " -jon hello-411.lua 2>&1"
f = io.popen(cmd)
if compiled_with_jit then
    equals(f:read'*l', 'Hello World', "-jon")
else
    matches(f:read'*l', errbuild("JIT compiler permanently disabled by build option"), "no jit")
end
f:close()

cmd = lua .. " -j bad hello-411.lua 2>&1"
f = io.popen(cmd)
matches(f:read'*l', errbuild("unknown luaJIT command or jit%.%* modules not installed"), "-j bad")
f:close()

if _TARANTOOL then
    skip("-O is not yet implemented in Tarantool")
elseif compiled_with_jit then
    cmd = lua .. " -O hello-411.lua"
    f = io.popen(cmd)
    equals(f:read'*l', 'Hello World', "-O")
    f:close()

    cmd = lua .. " -O3 hello-411.lua"
    f = io.popen(cmd)
    equals(f:read'*l', 'Hello World', "-O3")
    f:close()

    cmd = lua .. " -Ocse -O-dce -Ohotloop=10 hello-411.lua"
    f = io.popen(cmd)
    equals(f:read'*l', 'Hello World', "-Ocse -O-dce -Ohotloop=10")
    f:close()

    cmd = lua .. " -O+cse,-dce,hotloop=10 hello-411.lua"
    f = io.popen(cmd)
    equals(f:read'*l', 'Hello World', "-O+cse,-dce,hotloop=10")
    f:close()

    cmd = lua .. " -O+bad hello-411.lua 2>&1"
    f = io.popen(cmd)
    matches(f:read'*l', errbuild("unknown or malformed optimization flag '%+bad'"), "-O+bad")
    f:close()
else
    cmd = lua .. " -O0 hello-411.lua 2>&1"
    f = io.popen(cmd)
    matches(f:read'*l', errbuild("attempt to index a nil value"))
    f:close()
end

os.remove('hello-411.lua') -- clean up

done_testing()

-- Local Variables:
--   mode: lua
--   lua-indent-level: 4
--   fill-column: 100
-- End:
-- vim: ft=lua expandtab shiftwidth=4:
