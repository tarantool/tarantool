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

=head1 Lua Operating System Library

=head2 Synopsis

    % prove 309-os.t

=head2 Description

Tests Lua Operating System Library

See section "Operating System Facilities" in "Reference Manual"
L<https://www.lua.org/manual/5.1/manual.html#5.8>,
L<https://www.lua.org/manual/5.2/manual.html#6.9>,
L<https://www.lua.org/manual/5.3/manual.html#6.9>,
L<https://www.lua.org/manual/5.4/manual.html#6.9>

=cut

--]]

require'test_assertion'
local profile = require'profile'
local luajit20 = jit and (jit.version_num < 20100 and not jit.version:match'^RaptorJIT')
local has_execute51 = _VERSION == 'Lua 5.1' and (not profile.luajit_compat52 or ujit)
local lua = _retrieve_progname()

plan'no_plan'

do -- clock
    local clk = os.clock()
    is_number(clk, "function clock")
    truthy(clk <= os.clock())
end

do -- date
    local d = os.date('!*t', 0)
    equals(d.year, 1970, "function date")
    equals(d.month, 1)
    equals(d.day, 1)
    equals(d.hour, 0)
    equals(d.min, 0)
    equals(d.sec, 0)
    equals(d.wday, 5)
    equals(d.yday, 1)
    equals(d.isdst, false)

    equals(os.date('!%d/%m/%y %H:%M:%S', 0), '01/01/70 00:00:00', "function date")

    matches(os.date('%H:%M:%S'), '^%d%d:%d%d:%d%d', "function date")

    if (_VERSION == 'Lua 5.1' and not jit) or luajit20 then
        todo("not with 5.1")
    end
    equals(os.date('%Oy', 0), '70')

    if _VERSION == 'Lua 5.1' then
        todo("not with 5.1")
    end
    error_matches(function () os.date('%Ja', 0) end,
            "^[^:]+:%d+: bad argument #1 to 'date' %(invalid conversion specifier '%%Ja'%)",
            "function date (invalid)")
end

do -- difftime
    equals(os.difftime(1234, 1200), 34, "function difftime")
end

do -- execute
    if has_execute51 then
        local res = os.execute()
        equals(res, 1, "function execute -> shell is available")

        truthy(os.execute('__IMPROBABLE__') > 0, "function execute __IMPROBABLE__")

        local cmd = lua .. [[ -e "print '# hello from external Lua'"]]
        equals(os.execute(cmd), 0, "function execute")
    else
        local res = os.execute()
        is_true(res, "function execute -> shell is available")

        local r, s, n = os.execute('__IMPROBABLE__')
        is_nil(r, "function execute __IMPROBABLE__")
        equals(s, 'exit')
        is_number(n)

        local cmd = lua .. [[ -e "print '# hello from external Lua'"]]
        r, s, n = os.execute(cmd)
        is_true(r, "function execute")
        equals(s, 'exit')
        equals(n, 0)
    end
end

do -- exit called with execute
    if has_execute51 then
        local cmd = lua .. [[ -e "print '# hello from external Lua'; os.exit(2)"]]
        truthy(os.execute(cmd) > 0, "function exit called with execute")
    else
        local cmd = lua .. [[ -e "print '# hello from external Lua'; os.exit(2)"]]
        local r, s, n = os.execute(cmd)
        is_nil(r, "function exit called with execute")
        equals(s, 'exit')
        equals(n, 2, "exit value")

        cmd = lua .. [[ -e "print '# hello from external Lua'; os.exit(false)"]]
        r, s, n = os.execute(cmd)
        is_nil(r, "function exit called with execute")
        equals(s, 'exit')
        equals(n, 1, "exit value")

        cmd = lua .. [[ -e "print '# hello from external Lua'; os.exit(true, true)"]]
        r, s, n = os.execute(cmd)
        is_true(r, "exit called with execute")
        equals(s, 'exit')
        equals(n, 0, "exit value")
    end
end

do -- exit called with popen
    local cmd = lua .. [[ -e "print 'reached'; os.exit(); print 'not reached';"]]
    local res, f = pcall(io.popen, cmd)
    if res then
        equals(f:read'*l', 'reached', "exit called with popen")
        equals(f:read'*l', nil)
        if has_execute51 then
            local code = f:close()
            is_true(code, "exit code")
        else
            local r, s, n = f:close()
            is_true(r)
            equals(s, 'exit', "exit code")
            equals(n, 0, "exit value")
        end
    else
        diag("io.popen not supported")
    end

    cmd = lua .. [[ -e "print 'reached'; os.exit(3); print 'not reached';"]]
    res, f = pcall(io.popen, cmd)
    if res then
        equals(f:read'*l', 'reached', "exit called with popen")
        equals(f:read'*l', nil)
        if has_execute51 then
            local code = f:close()
            is_true(code, "exit code")
        else
            local r, s, n = f:close()
            is_nil(r)
            equals(s, 'exit', "exit code")
            equals(n, 3, "exit value")
        end
    else
        diag("io.popen not supported")
    end
end

do -- getenv
    equals(os.getenv('__IMPROBABLE__'), nil, "function getenv")

    local home = os.getenv('HOME') or os.getenv('HOMEPATH')
    is_string(home, "function getenv")
end

do -- remove
    local f = io.open('file-309.rm', 'w')
    f:write("file to remove")
    f:close()
    local r = os.remove("file-309.rm")
    is_true(r, "function remove")

    local msg
    r, msg = os.remove('file-309.rm')
    is_nil(r, "function remove")
    matches(msg, '^file%-309%.rm: No such file or directory')
end

do -- rename
    local f = io.open('file-309.old', 'w')
    f:write("file to rename")
    f:close()
    os.remove('file-309.new')
    local r = os.rename('file-309.old', 'file-309.new')
    is_true(r, "function rename")
    os.remove('file-309.new') -- clean up

    local msg
    r, msg = os.rename('file-309.old', 'file-309.new')
    is_nil(r, "function rename")
    matches(msg, 'No such file or directory')
end

do -- setlocale
    equals(os.setlocale('C', 'all'), 'C', "function setlocale")
    equals(os.setlocale(), 'C')
    equals(os.setlocale('unk_loc', 'all'), nil, "function setlocale (unknown locale)")
end

do -- time
    matches(os.time(), '^%d+%.?%d*$', "function time")
    matches(os.time(nil), '^%d+%.?%d*$', "function time")
    matches(os.time({
        sec = 0,
        min = 0,
        hour = 0,
        day = 1,
        month = 1,
        year = 2000,
        isdst = false,
    }), '^946%d+$', "function time")

    error_matches(function () os.time{} end,
            "^[^:]+:%d+: field '%w+' missing in date table",
            "function time (missing field)")

    error_matches(function () os.time({ day = 'bad', year = 'bad' }) end,
            "^[^:]+:%d+: field '%w+'",
            "function time (bad field)")

    if _VERSION < 'Lua 5.3' then
        todo("only with integer")
    end
    error_matches(function () os.time({ day = 1.5, year = 1.5 }) end,
            "^[^:]+:%d+: field '%w+' is not an integer",
            "function time (not integer)")

    if string.packsize and string.packsize('l') == 8 then
        skip('64bit platforms')
    else
        if _VERSION < 'Lua 5.3' then
            todo"only with 5.3"
        end
        error_matches(function () os.time({
            sec = 0,
            min = 0,
            hour = 0,
            day = 1,
            month = 1,
            year = 1000,
            isdst = false,
        }) end,
                "^[^:]+:%d+: time result cannot be represented in this installation",
                "function time (invalid)")
    end
end

do -- tmpname
    local fname = os.tmpname()
    is_string(fname, "function tmpname")
    truthy(fname ~= os.tmpname())
end

done_testing()

-- Local Variables:
--   mode: lua
--   lua-indent-level: 4
--   fill-column: 100
-- End:
-- vim: ft=lua expandtab shiftwidth=4:
