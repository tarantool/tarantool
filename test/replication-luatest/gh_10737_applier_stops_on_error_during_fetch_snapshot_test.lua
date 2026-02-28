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
    -- Block _cluster WAL write after snapshot is sent but before OK marker.
    cg.master:exec(function()
        box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', 1)
    end)

    local replica = server:new{
        alias = 'replica',
        box_cfg = { replication = { cg.master.net_box_uri } },
    }

    replica:start({wait_until_ready = false})
    t.helpers.retrying({}, function()
        t.assert_not_equals(cg.master:grep_log('initial data sent', nil), nil)
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
