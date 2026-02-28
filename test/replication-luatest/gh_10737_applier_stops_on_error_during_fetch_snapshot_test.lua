local t = require('luatest')
local server = require('luatest.server')

local g = t.group()

g.before_all(function(cg)
    t.tarantool.skip_if_not_debug()
    cg.master = server:new{alias = 'master'}
    cg.master:start()
    cg.master:exec(function()
        box.schema.space.create('test'):create_index('pk')
        box.space.test:insert{1, 'data'}
        box.space.test:insert{2, 'data'}
    end)
end)

g.after_all(function(cg)
    cg.master:stop()
end)

g.test_applier_stops_on_error_during_fetch_snapshot = function(cg)
    -- Do not allow master to register replica. We try to reproduce, that
    -- master restart right during initial join stage.
    cg.master:exec(function()
        box.error.injection.set('ERRINJ_RELAY_INITIAL_JOIN_END_DELAY', true)
    end)

    local replica = server:new{
        alias = 'replica',
        box_cfg = { replication = { cg.master.net_box_uri } },
    }

    replica:start({wait_until_ready = false})
    t.helpers.retrying({}, function()
        -- Since I cannot wait for replica (no net box conn) to achieve
        -- ERRINJ_APPLIER_INITIAL_JOIN_DELAY state, I'll wait for master to
        -- end initial join stage.
        t.assert_not_equals(
            cg.master:grep_log('relay initial join is delayed', nil))
    end)
    cg.master.process:kill('KILL')
    cg.master:restart()

    -- Applier should see that replica has some data and stop itself from
    -- reconnecting.
    t.helpers.retrying({timeout = 1}, function()
        t.assert_not(replica.process:is_alive())
    end)

    replica:stop()
end
