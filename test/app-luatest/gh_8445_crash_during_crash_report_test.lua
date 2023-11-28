local fio = require('fio')
local t = require('luatest')
local g = t.group('gh-8445')

g.before_each(function(cg)
    cg.tempdir = fio.tempdir()
end)

g.after_each(function(cg)
    fio.rmtree(cg.tempdir)
end)

-- Check that forked Tarantool doesn't crash when preparing a crash report.
g.test_crash_during_crash_report = function(cg)
    local ffi = require('ffi')
    local popen = require('popen')

    -- Use `cd' and `shell = true' due to lack of cwd option in popen (gh-5633).
    local exe = arg[-1]
    local dir = cg.tempdir
    local script = [[
        local tweaks = require('internal.tweaks')
        local log = require('log')
        box.cfg{}
        tweaks.crash_produce_coredump = false
        log.info('pid = ' .. box.info.pid)
    ]]
    local ph = popen.new({string.format('cd %s && %s -e "%s"',
                                         dir, exe, script)},
                         {stdout = popen.opts.DEVNULL,
                          stderr = popen.opts.PIPE, shell = true})
    t.assert(ph)

    -- Wait for box.cfg{} completion.
    local output = ''
    t.helpers.retrying({}, function()
        local chunk = ph:read({stderr = true, timeout = 0.05})
        if chunk ~= nil then
           output = output .. chunk
        end
        t.assert_str_contains(output, "pid = ")
    end)

    -- ph:info().pid won't work, because it returns pid of the shell rather than
    -- pid of the Tarantool.
    local pid = tonumber(string.match(output, "pid = (%d+)"))
    t.assert(pid)
    ffi.C.kill(pid, popen.signal.SIGILL)
    ph:wait()
    output = ph:read({stderr = true})
    t.assert_str_contains(output, "Please file a bug")

    -- Check that there were no fatal signals during crash report.
    t.assert_not_str_contains(output, "Fatal 11 while backtracing")
    ph:close()
end
