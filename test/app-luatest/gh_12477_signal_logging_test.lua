local server = require('luatest.server')
local popen = require('popen')
local ffi = require('ffi')
local fio = require('fio')
local t = require('luatest')

local g = t.group()

-- Count the lines matching a pattern in the server log; grep_log() returns
-- only the last match, so the tests that assert how many times a line was
-- written read the file directly.
local function count_log_lines(cg, pattern)
    local f = assert(io.open(cg.server.log_file, 'r'))
    local n = 0
    for line in f:lines() do
        if line:match(pattern) then
            n = n + 1
        end
    end
    f:close()
    return n
end

g.before_each(function(cg)
    cg.server = server:new({
        box_cfg = {
            app_threads = 1,
            pid_file = fio.pathjoin(fio.tempdir(), 'tarantool.pid'),
        },
        net_box_credentials = {user = 'admin'},
    })
    cg.server:start()
    -- Allow requiring the C modules built for the test suite.
    cg.server:exec(function()
        local fio = require('fio')
        local tarantool = require('tarantool')
        local mod_pattern = '?.' .. tarantool.build.mod_format
        local test_dir = fio.abspath(fio.pathjoin(
            os.getenv('BUILDDIR') or '.', 'test', 'app-luatest', 'lib'))
        package.cpath = fio.pathjoin(test_dir, mod_pattern) .. ';' ..
                        package.cpath
    end)
end)

g.after_each(function(cg)
    if cg.server ~= nil then
        -- Stop the module thread if the test started one and did not stop
        -- it (an early failure), so the module is not unloaded under it
        -- at shutdown. The exec raises when the server is already dead,
        -- and the pcall keeps that from skipping the drop() below.
        pcall(function()
            cg.server:exec(function()
                local mod = package.loaded['gh_12477_signal_logging']
                if mod ~= nil then
                    mod.stop_a_thread()
                end
            end)
        end)
        cg.server:drop()
    end
end)

-- Test all the signals handled by libev are logged.
g.test_ev_signals = function(cg)
    -- Send a signal with own sender PID and check if it's logged.
    ffi.cdef('int kill(int pid, int signum);')
    local function test_signal(signum)
        -- The pattern to search for in the log. The sender PID is the
        -- capture: `grep_log()` returns the match, not the line.
        local signal_log_pattern = 'got signal #' .. signum .. ' .* ' ..
                                   'from PID (%d+)'
        -- The process can be terminated by the signal - get it now.
        local log_filename = cg.server:exec(function()
            return rawget(_G, 'box_cfg_log_file') or box.cfg.log
        end)
        ffi.C.kill(cg.server.process.pid, signum)
        t.helpers.retrying({}, function()
            local pid = tonumber(cg.server:grep_log(signal_log_pattern, nil,
                                                    {filename = log_filename}))
            t.assert(pid ~= nil)
            -- macOS and *BSD may report 0 instead of the sender PID.
            t.assert(pid == box.info.pid or
                     (jit.os ~= 'Linux' and pid == 0))
        end)
    end

    -- Test signals with no unwanted side effects.
    test_signal(popen.signal.SIGUSR1)
    test_signal(popen.signal.SIGUSR2)
    test_signal(popen.signal.SIGHUP)

    -- Restart after each next test.
    test_signal(popen.signal.SIGINT)
    cg.server:restart()
    test_signal(popen.signal.SIGTERM)
    cg.server:restart()
end

g.test_sigurg_redirection = function(cg)
    t.skip_if(jit.os ~= 'Linux', "Uses a Linux-specific syscall: tgkill")

    -- Create a "foreign" thread in a dynamically linked module.
    local pid, tid = cg.server:exec(function()
        local mod = require('gh_12477_signal_logging')
        local tid = mod.run_a_thread()
        return box.info.pid, tid
    end)
    t.assert_equals(pid, cg.server.process.pid)

    -- Run a long-running fiber with a big slice.
    cg.server:exec(function()
        -- The test times out if SIGURG is not handled.
        require('fiber').set_max_slice(999999)
    end)
    local infinite_loop = [[
        local fiber = require('fiber')
        while true do
            fiber.testcancel()
            fiber.check_slice()
            os.execute('sleep 0.1')
        end
    ]]
    local loop = cg.server.net_box:eval(infinite_loop, nil, {is_async = true})
    -- A new request should not return if the fiber started execution.
    t.assert_equals(cg.server.net_box:ping({timeout = 0.1}), false)

    -- Send SIGURG to the foreign thread: no assertion fail expected.
    ffi.cdef('int tgkill(int tgid, int tid, int sig);')
    ffi.C.tgkill(pid, tid, popen.signal.SIGURG)
    local signal_log_pattern = 'got signal #' .. popen.signal.SIGURG ..
                               ' (.*) from PID ' .. box.info.pid
    t.helpers.retrying({}, function()
        t.assert(cg.server:grep_log(signal_log_pattern))
    end)
    -- Also, the new fiber should fail.
    t.assert_equals(loop:is_ready(), true)
    local res, err = loop:result()
    t.assert_equals(res, nil)
    t.assert_equals(err.message, 'fiber slice is exceeded')

    -- The first half is done with the foreign thread: stop it before the
    -- server restart below, which would unload the module during shutdown.
    cg.server:exec(function()
        require('gh_12477_signal_logging').stop_a_thread()
    end)

    -- Restart the server so that grep_log() discards the first half's line
    -- at the start banner; otherwise the search below finds that line.
    cg.server:restart()
    pid = cg.server.process.pid
    cg.server:exec(function()
        require('fiber').set_max_slice(999999)
    end)

    -- The same check, but send the signal to the main (TX) thread.
    loop = cg.server.net_box:eval(infinite_loop, nil, {is_async = true})
    t.assert_equals(cg.server.net_box:ping({timeout = 0.1}), false)
    ffi.C.tgkill(pid, pid, popen.signal.SIGURG)
    t.helpers.retrying({}, function()
        t.assert(cg.server:grep_log(signal_log_pattern))
    end)
    t.assert_equals(loop:is_ready(), true)
    local res, err = loop:result()
    t.assert_equals(res, nil)
    t.assert_equals(err.message, 'fiber slice is exceeded')
end

-- Two SIGURGs from different senders before the next slice check: the PID of
-- the first sender is logged, the second signal's PID is not.
g.test_sigurg_first_sender = function(cg)
    t.skip_if(jit.os ~= 'Linux', "The sender PID is not reliable here")

    cg.server:exec(function()
        require('fiber').set_max_slice(999999)
        rawset(_G, '__sigurg_ready', require('fiber').channel(1))
    end)
    local infinite_loop = [[
        local fiber = require('fiber')
        local ready = rawget(_G, '__sigurg_ready')
        while true do
            fiber.testcancel()
            fiber.check_slice()
            ready:put(true)
            os.execute('sleep 0.2')
        end
    ]]
    local loop = cg.server.net_box:eval(infinite_loop, nil, {is_async = true})
    t.assert_equals(cg.server.net_box:ping({timeout = 0.1}), false)
    local pid = cg.server.process.pid

    -- The first signal comes from the luatest runner, the second from a
    -- child process. Wait for the loop to enter its sleep, so both signals
    -- arrive in the same 200 ms window after a check.
    ffi.cdef('int kill(int pid, int signum);')
    cg.server:exec(function() rawget(_G, '__sigurg_ready'):get() end)
    ffi.C.kill(pid, popen.signal.SIGURG)
    os.execute('sleep 0.02')
    os.execute('kill -URG ' .. pid)

    local pattern = 'got signal #' .. popen.signal.SIGURG ..
                    ' .* from PID (%d+)'
    t.helpers.retrying({}, function()
        local sender = tonumber(cg.server:grep_log(pattern))
        t.assert_equals(sender, box.info.pid)
    end)
    -- The line is logged once: ordinary box work after it must not repeat
    -- it (the log-once reset).
    cg.server:exec(function()
        local s = box.schema.space.create('tmp')
        s:create_index('pk')
        for i = 1, 2000 do s:insert{i} end
        s:select{}
        s:drop()
    end)
    t.helpers.retrying({}, function()
        t.assert_equals(count_log_lines(cg, pattern), 1)
    end)
    t.assert_equals(loop:is_ready(), true)
    local _, err = loop:result()
    t.assert_equals(err.message, 'fiber slice is exceeded')
end

-- A non-main cord must not consume the SIGURG: the app thread's
-- fiber.check_slice() sees the field but must not log or clear it; the
-- main cord logs it after the SIGUSR2 marker.
g.test_sigurg_non_main_check = function(cg)
    t.skip_if(jit.os ~= 'Linux', "The sender PID is not reliable here")

    cg.server:exec(function()
        require('fiber').set_max_slice(999999)
    end)
    local pid = cg.server.process.pid

    -- Put a SIGURG into the main cord's handler, then check the slice on
    -- the app thread before the main cord does.
    ffi.cdef('int kill(int pid, int signum);')
    ffi.C.kill(pid, popen.signal.SIGURG)
    cg.server:eval('require("fiber").check_slice()', {}, {_thread_id = 1})

    -- Mark the log with SIGUSR2 (logged by libev, no slice check); the
    -- SIGURG line must come after the marker, i.e. from the main cord.
    ffi.C.kill(pid, popen.signal.SIGUSR2)
    local usr2_pattern = 'got signal #' .. popen.signal.SIGUSR2
    t.helpers.retrying({}, function()
        t.assert(cg.server:grep_log(usr2_pattern))
    end)
    cg.server:exec(function() require('fiber').check_slice() end)

    local sigurg_pattern = 'got signal #' .. popen.signal.SIGURG
    t.helpers.retrying({}, function()
        t.assert(cg.server:grep_log(sigurg_pattern))
    end)

    local sigurg_pos, marker_pos, pos = nil, nil, 0
    local f = assert(io.open(cg.server.log_file, 'r'))
    for line in f:lines() do
        pos = pos + 1
        if line:find(sigurg_pattern, 1, true) then
            sigurg_pos = pos
        end
        if line:find(usr2_pattern, 1, true) then
            marker_pos = pos
        end
    end
    f:close()
    t.assert_not_equals(sigurg_pos, nil)
    t.assert_not_equals(marker_pos, nil)
    t.assert_gt(sigurg_pos, marker_pos)
end
