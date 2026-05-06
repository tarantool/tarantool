local server = require('luatest.server')
local popen = require('popen')
local ffi = require('ffi')
local fio = require('fio')
local t = require('luatest')

local g = t.group()

g.before_each(function(cg)
    cg.server = server:new({box_cfg = {
        pid_file = fio.pathjoin(fio.tempdir(), 'tarantool.pid')}
    })
    cg.server:start()
end)

g.after_each(function(cg)
    if cg.server ~= nil then
        cg.server:drop()
    end
end)

-- Test all the signals handled by libev are logged.
g.test_ev_signals = function(cg)
    -- Send a signal with own sender PID and check if it's logged.
    ffi.cdef('int kill(int pid, int signum);')
    local function test_signal(signum)
        -- The pattern to search for in the log.
        local signal_log_pattern = 'got signal #' .. signum .. ' (.*) ' ..
                                   'from PID ' .. box.info.pid
        -- The process can be terminated by the signal - get it now.
        local log_filename = cg.server:exec(function()
            return rawget(_G, 'box_cfg_log_file') or box.cfg.log
        end)
        ffi.C.kill(cg.server.process.pid, signum)
        t.helpers.retrying({}, function()
            t.assert(cg.server:grep_log(signal_log_pattern, nil,
                                        {filename = log_filename}))
        end)
    end

    -- Test signals with no unwanted side effects.
    test_signal(popen.signal.SIGUSR1)
    test_signal(popen.signal.SIGUSR2)
    test_signal(popen.signal.SIGHUP)

    -- Restart after each next test.
    test_signal(popen.signal.SIGINT)
    cg.server:start()
    test_signal(popen.signal.SIGTERM)
    cg.server:start()
end

local g_sigurg_redirection = t.group('sigurg-redirection')

g_sigurg_redirection.before_all(function(cg)
    cg.server = server:new()
    cg.server.start = function(...)
        getmetatable(cg.server).start(...)
        -- Set package.cpath to allow loading C modules from the test directory.
        cg.server:exec(function()
            local fio = require('fio')
            local tarantool = require('tarantool')
            local mod_pattern = '?.' .. tarantool.build.mod_format
            local test_dir = fio.abspath(
                fio.pathjoin(os.getenv('BUILDDIR') or '.',
                'test', 'app-luatest'))
            local test_mod_cpath = fio.pathjoin(test_dir, mod_pattern)
            package.cpath = test_mod_cpath .. ';' .. package.cpath
        end)
    end
    cg.server:start()
end)

g_sigurg_redirection.after_all(function(cg)
    cg.server:drop()
end)

g_sigurg_redirection.test_sigurg_redirection = function(cg)
    t.skip_if(jit.os ~= 'Linux', "Used a linux-specific syscall: tgkill")

    -- Create a "foreign" thread in a dynamically linked module.
    local pid, tid = cg.server:exec(function()
        local lib = box.lib.load('gh_12477_signal_logging')
        local run_a_thread = lib:load('run_a_thread')
        local tid = run_a_thread()
        return box.info.pid, tid
    end)
    t.assert_equals(pid, cg.server.process.pid)

    -- Run a new fiber with a big slice.
    local function start_a_long_fiber()
        local fiber = require('fiber')
        -- The test timeouts if SIGURG not handled.
        fiber.set_max_slice(999999)
        fiber.new(function()
            while true do
                fiber.check_slice()
            end
        end)
    end
    cg.server:exec(start_a_long_fiber)
    -- Send SIGURG to the foreign thread: no assertion fail expected.
    ffi.cdef('int tgkill(int tgid, int tid, int sig);')
    ffi.C.tgkill(pid, tid, popen.signal.SIGURG)
    -- Also, the new fiber should fail immediately.
    local signal_log_pattern = 'got signal #' .. popen.signal.SIGURG ..
                               ' (.*) from PID ' .. box.info.pid
    t.helpers.retrying({}, function()
        t.assert(cg.server:grep_log(signal_log_pattern))
    end)
    t.helpers.retrying({}, function()
        t.assert(cg.server:grep_log('fiber slice is exceeded'))
    end)

    -- The same check, but send the signal to the main (TX) thread.
    cg.server:exec(start_a_long_fiber)
    ffi.C.tgkill(pid, pid, popen.signal.SIGURG)
    t.helpers.retrying({}, function()
        t.assert(cg.server:grep_log(signal_log_pattern))
    end)
    t.helpers.retrying({}, function()
        t.assert(cg.server:grep_log('fiber slice is exceeded'))
    end)
end
