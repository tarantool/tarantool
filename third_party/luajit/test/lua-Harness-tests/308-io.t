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

=head1 Lua Input/Output Library

=head2 Synopsis

    % prove 308-io.t

=head2 Description

Tests Lua Input/Output Library

See section "Input and Output Facilities" in "Reference Manual"
L<https://www.lua.org/manual/5.1/manual.html#5.7>,
L<https://www.lua.org/manual/5.2/manual.html#6.8>,
L<https://www.lua.org/manual/5.3/manual.html#6.8>,
L<https://www.lua.org/manual/5.4/manual.html#6.8>

=cut

--]]

require'test_assertion'
local profile = require'profile'
local luajit21 = jit and (jit.version_num >= 20100 or jit.version:match'^RaptorJIT')
local has_write51 = _VERSION == 'Lua 5.1' and (not profile.luajit_compat52 or ujit)
local has_lines52 = _VERSION >= 'Lua 5.2' or profile.luajit_compat52
local has_read52 = _VERSION >= 'Lua 5.2' or profile.luajit_compat52
local has_read53 = _VERSION >= 'Lua 5.3' or luajit21
local has_meta53 = _VERSION >= 'Lua 5.3'
local has_meta54 = _VERSION >= 'Lua 5.4'

local lua = _retrieve_progname()

plan'no_plan'

do -- stdin
    matches(io.stdin, '^file %(0?[Xx]?%x+%)$', "variable stdin")
end

do -- stdout
    matches(io.stdout, '^file %(0?[Xx]?%x+%)$', "variable stdout")
end

do -- stderr
    matches(io.stderr, '^file %(0?[Xx]?%x+%)$', "variable stderr")
end

do -- metatable
    local f = io.tmpfile()
    local mt = getmetatable(f)
    is_table(mt, "metatable")

    is_function(mt.__gc)
    is_function(mt.__tostring)
    is_table(mt.__index)

    if has_meta53 then
        equals(mt.__name, 'FILE*')
    else
        is_nil(mt.__name)
    end

    if has_meta54 then
        is_function(mt.__close)
        is_table(mt.__index)
        is_nil(mt.close)
        is_nil(mt.flush)
        is_nil(mt.lines)
        is_nil(mt.read)
        is_nil(mt.seek)
        is_nil(mt.setvbuf)
        is_nil(mt.write)
    else
        is_nil(mt.__close)
        equals(mt.__index, mt)
        is_function(mt.close)
        is_function(mt.flush)
        is_function(mt.lines)
        is_function(mt.read)
        is_function(mt.seek)
        is_function(mt.setvbuf)
        is_function(mt.write)
    end

    is_function(mt.__index.close)
    is_function(mt.__index.flush)
    is_function(mt.__index.lines)
    is_function(mt.__index.read)
    is_function(mt.__index.seek)
    is_function(mt.__index.setvbuf)
    is_function(mt.__index.write)
end

do -- close
    local r, msg = io.close(io.stderr)
    is_nil(r, "close (std)")
    equals(msg, "cannot close standard file")
end

do -- flush
    is_true(io.flush(), "function flush")
end

do -- open
    os.remove('file-308.no')
    local f, msg = io.open("file-308.no")
    is_nil(f, "function open")
    equals(msg, "file-308.no: No such file or directory")

    os.remove('file-308.txt')
    f = io.open('file-308.txt', 'w')
    f:write("file with text\n")
    f:close()
    f = io.open('file-308.txt')
    matches(f, '^file %(0?[Xx]?%x+%)$', "function open")

    is_true(io.close(f), "function close")

    error_matches(function () io.close(f) end,
            "^[^:]+:%d+: attempt to use a closed file",
            "function close (closed)")

    if _VERSION == 'Lua 5.1' then
        todo("not with 5.1")
    end
    error_matches(function () io.open('file-308.txt', 'baz') end,
            "^[^:]+:%d+: bad argument #2 to 'open' %(invalid mode%)",
            "function open (bad mode)")
end

do -- type
    equals(io.type("not a file"), nil, "function type")
    local f = io.open('file-308.txt')
    equals(io.type(f), 'file')
    matches(tostring(f), '^file %(0?[Xx]?%x+%)$')
    io.close(f)
    equals(io.type(f), 'closed file')
    equals(tostring(f), 'file (closed)')
end

do -- input
    equals(io.stdin, io.input(), "function input")
    equals(io.stdin, io.input(nil))
    local f = io.stdin
    matches(io.input('file-308.txt'), '^file %(0?[Xx]?%x+%)$')
    equals(f, io.input(f))
end

do -- output
    equals(io.output(), io.stdout, "function output")
    equals(io.output(nil), io.stdout)
    local f = io.stdout
    matches(io.output('output.new'), '^file %(0?[Xx]?%x+%)$')
    equals(f, io.output(f))
    os.remove('output.new')
end

do -- popen
    local r, f = pcall(io.popen, lua .. [[ -e "print 'standard output'"]])
    if r then
        equals(io.type(f), 'file', "popen (read)")
        equals(f:read(), "standard output")
        is_true(io.close(f))
    else
        diag("io.popen not supported")
    end

    r, f = pcall(io.popen, lua .. [[ -e "for line in io.lines() do print((line:gsub('e', 'a'))) end"]], 'w')
    if r then
        equals(io.type(f), 'file', "popen (write)")
        f:write("# hello\n") -- not tested : hallo
        is_true(io.close(f))
    else
        diag("io.popen not supported")
    end
end

do -- lines
    for line in io.lines('file-308.txt') do
        equals(line, "file with text", "function lines(filename)")
    end

    error_matches(function () io.lines('file-308.no') end,
            "No such file or directory",
            "function lines(no filename)")
end

do -- tmpfile
    local  f = io.tmpfile()
    equals(io.type(f), 'file', "function tmpfile")
    f:write("some text")
    f:close()
end

do -- write
    io.write() -- not tested
    io.write('# text', 12, "\n") -- not tested :  # text12
end

do -- :close
    local r, msg = io.stderr:close()
    is_nil(r, "method close (std)")
    equals(msg, "cannot close standard file")

    local f = io.open('file-308.txt')
    is_true(f:close(), "method close")
end

do -- :flush
    equals(io.stderr:flush(), true, "method flush")

    local f = io.open('file-308.txt')
    f:close()
    error_matches(function () f:flush() end,
            "^[^:]+:%d+: attempt to use a closed file",
            "method flush (closed)")
end

do -- :read & :write
    local f = io.open('file-308.txt')
    f:close()
    error_matches(function () f:read() end,
            "^[^:]+:%d+: attempt to use a closed file",
            "method read (closed)")

    f = io.open('file-308.txt')
    local s = f:read()
    equals(s:len(), 14, "method read")
    equals(s, "file with text")
    s = f:read()
    equals(s, nil)
    f:close()

    f = io.open('file-308.txt')
    error_matches(function () f:read('*z') end,
            "^[^:]+:%d+: bad argument #1 to 'read' %(invalid %w+%)",
            "method read (invalid)")
    f:close()

    f = io.open('file-308.txt')
    local s1, s2 = f:read('*l', '*l')
    equals(s1:len(), 14, "method read *l")
    equals(s1, "file with text")
    equals(s2, nil)
    f:close()

    if has_read52 then
        f = io.open('file-308.txt')
        s1, s2 = f:read('*L', '*L')
        equals(s1:len(), 15, "method read *L")
        equals(s1, "file with text\n")
        equals(s2, nil)
        f:close()
    else
        diag("no read *L")
    end

    f = io.open('file-308.txt')
    local n1, n2 = f:read('*n', '*n')
    equals(n1, nil, "method read *n")
    equals(n2, nil)
    f:close()

    f = io.open('file-308.num', 'w')
    f:write('1\n')
    f:write('0xFF\n')
    f:write(string.rep('012', 90) .. '\n')
    f:close()

    f = io.open('file-308.num')
    n1, n2 = f:read('*n', '*n')
    equals(n1, 1, "method read *n")
    equals(n2, 255, "method read *n")
    local n = f:read('*n')
    if _VERSION < 'Lua 5.3' then
        is_number(n)
    else
        is_nil(n, "method read *n too long")
    end
    f:close()

    os.remove('file-308.num') -- clean up

    f = io.open('file-308.txt')
    s = f:read('*a')
    equals(s:len(), 15, "method read *a")
    equals(s, "file with text\n")
    f:close()

    if has_read53 then
        f = io.open('file-308.txt')
        s = f:read('a')
        equals(s:len(), 15, "method read a")
        equals(s, "file with text\n")
        f:close()
    else
        diag("* mandatory")
    end

    f = io.open('file-308.txt')
    equals(f:read(0), '', "method read number")
    array_equals({f:read(5, 5, 15)}, {'file ', 'with ', "text\n"})
    f:close()
end

do -- :lines
    local f = io.open('file-308.txt')
    for line in f:lines() do
        equals(line, "file with text", "method lines")
    end
    equals(io.type(f), 'file')
    f:close()
    equals(io.type(f), 'closed file')

    if has_lines52 then
        f = io.open('file-308.txt')
        for two_char in f:lines(2) do
            equals(two_char, "fi", "method lines (with read option)")
            break
        end
        f:close()
    else
        diag("no lines with option")
    end
end

do -- :seek
    local f = io.open('file-308.txt')
    f:close()

    error_matches(function () f:seek('end', 0) end,
            "^[^:]+:%d+: attempt to use a closed file",
            "method seek (closed)")

    f = io.open('file-308.txt')
    error_matches(function () f:seek('bad', 0) end,
            "^[^:]+:%d+: bad argument #1 to 'seek' %(invalid option 'bad'%)",
            "method seek (invalid)")

    f = io.open('file-308.bin', 'w')
    f:write('ABCDE')
    f:close()
    f = io.open('file-308.bin')
    equals(f:seek('end', 0), 5, "method seek")
    f:close()
    os.remove('file-308.bin') --clean up
end

do -- :setvbuf
    local  f = io.open('file-308.txt')
    equals(f:setvbuf('no'), true, "method setvbuf 'no'")

    equals(f:setvbuf('full', 4096), true, "method setvbuf 'full'")

    equals(f:setvbuf('line', 132), true, "method setvbuf 'line'")
    f:close()
end

os.remove('file-308.txt') -- clean up

do -- :write
    local  f = io.open('file-308.out', 'w')
    f:close()
    error_matches(function () f:write('end') end,
            "^[^:]+:%d+: attempt to use a closed file",
            "method write (closed)")

    f = io.open('file-308.out', 'w')
    if has_write51 then
        equals(f:write('end'), true, "method write")
    else
        equals(f:write('end'), f, "method write")
    end
    f:close()

    os.remove('file-308.out') --clean up
end

done_testing()

-- Local Variables:
--   mode: lua
--   lua-indent-level: 4
--   fill-column: 100
-- End:
-- vim: ft=lua expandtab shiftwidth=4:
