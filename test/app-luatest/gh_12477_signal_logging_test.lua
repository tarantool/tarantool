local server = require('luatest.server')
local popen = require('popen')
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
    local ffi = require("ffi")
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
    test_signal(popen.signal.SIGHUP)

    -- Restart after each next test.
    test_signal(popen.signal.SIGINT)
    cg.server:start()
    test_signal(popen.signal.SIGTERM)
    cg.server:start()
end
