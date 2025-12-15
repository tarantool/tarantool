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

=head1 Lua Library

=head2 Synopsis

    % prove 320-stdin.t

=head2 Description

Tests Lua Basic & IO Libraries with stdin

=cut

--]]

require'test_assertion'

local lua = _retrieve_progname()

if not pcall(io.popen, lua .. [[ -e "a=1"]]) then
    skip_all "io.popen not supported"
end

plan'no_plan'

do
    local f = io.open('lib-320.lua', 'w')
    f:write[[
function norm (x, y)
    return (x^2 + y^2)^0.5
end

function twice (x)
    return 2*x
end
]]
    f:close()

    local cmd = lua .. [[ -e "dofile(); n = norm(3.4, 1.0); print(twice(n))" < lib-320.lua]]
    f = io.popen(cmd)
    near(f:read'*n', 7.088, 0.001, "function dofile (stdin)")
    f:close()

    os.remove('lib-320.lua') -- clean up
end

do
    local f = io.open('foo-320.lua', 'w')
    f:write[[
function foo (x)
    return x
end
]]
    f:close()

    local cmd = lua .. [[ -e "foo = nil; f = loadfile(); print(foo); f(); print(foo('ok'))" < foo-320.lua]]
    f = io.popen(cmd)
    equals(f:read'*l', 'nil', "function loadfile (stdin)")
    equals(f:read'*l', 'ok')
    f:close()

    os.remove('foo-320.lua') -- clean up
end

do
    local f = io.open('file-320.txt', 'w')
    f:write("file with text\n")
    f:close()

    local cmd = lua .. [[ -e "print(io.read'*l'); print(io.read'*l'); print(io.type(io.stdin))" < file-320.txt]]
    f = io.popen(cmd)
    equals(f:read'*l', 'file with text', "function io.read *l")
    equals(f:read'*l', 'nil')
    equals(f:read'*l', 'file')
    f:close()

    cmd = lua .. [[ -e "for line in io.lines() do print(line) end" < file-320.txt]]
    f = io.popen(cmd)
    equals(f:read'*l', 'file with text', "function io.lines")
    equals(f:read'*l', nil)
    f:close()

    os.remove('file-320.txt') -- clean up
end

do
    local f = io.open('number-320.txt', 'w')
    f:write("6.0     -3.23   15e3\n")
    f:write("4.3     234     1000001\n")
    f:close()

    local cmd = lua .. [[ -e "while true do local n1, n2, n3 = io.read('*number', '*number', '*number'); if not n1 then break end; print(math.max(n1, n2, n3)) end" < number-320.txt]]
    f = io.popen(cmd)
    equals(f:read'*n', 15000, "function io:read *number")
    equals(f:read'*n', 1000001)
    f:close()

    os.remove('number-320.txt') -- clean up
end

if debug then
    local f = io.open('dbg-320.txt', 'w')
    f:write("print 'ok'\n")
    f:write("error 'dbg'\n")
    f:write("cont\n")
    f:close()

    local cmd = lua .. [[ -e "debug.debug()" < dbg-320.txt]]
    f = io.popen(cmd)
    equals(f:read'*l', 'ok', "function debug.debug")
    equals(f:read'*l', nil)
    f:close()

    os.remove('dbg-320.txt') -- clean up
else
    diag("no debug")
end

done_testing()

-- Local Variables:
--   mode: lua
--   lua-indent-level: 4
--   fill-column: 100
-- End:
-- vim: ft=lua expandtab shiftwidth=4:
