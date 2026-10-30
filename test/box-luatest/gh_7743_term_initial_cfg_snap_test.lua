local server = require('luatest.server')
local t = require('luatest')
local fio = require('fio')

local g = t.group('gh-7743')

--
-- There was a bug when SIGTERM during the initial snapshot writing wouldn't
-- cancel the snapshot thread nor would wait for its termination. As a result
-- the thread could try to call its on-exit callback which was stored on a
-- deleted fiber's stack and that would crash.
--
g.test_sigterm_during_initial_snapshot = function()
    t.tarantool.skip_if_not_debug()
    g.server = server:new({
        alias = 'master',
        env = {
            -- Snap delay is not set to false on shutdown deliberately. The
            -- snapshot thread should abort anyway, because the errinj delay
            -- uses usleep() which is a pthread cancellation point.
            TARANTOOL_RUN_BEFORE_BOX_CFG = [[
                box.ctl.set_on_shutdown_timeout(1000)
                box.error.injection.set('ERRINJ_SNAP_WRITE_DELAY', true)
            ]]
        }
    })
    g.server:start({wait_until_ready = false})
    local logname = fio.pathjoin(g.server.workdir, 'master.log')
    t.helpers.retrying({}, function()
        assert(g.server:grep_log('saving snapshot', nil, {filename = logname}))
    end)
    g.server:stop()
    local panic_msg = "failed to create a checkpoint"
    t.assert(g.server:grep_log(panic_msg, nil, {filename = logname}))
end
