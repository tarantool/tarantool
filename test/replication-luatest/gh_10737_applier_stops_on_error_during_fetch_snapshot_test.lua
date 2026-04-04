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
        t.assert_not_equals(cg.master:grep_log('initial data sent'), nil)
    end)
    cg.master.process:kill("KILL");
    cg.master:restart()

    -- Applier should see that replica has some data and stop itself from
    -- reconnecting.
    t.helpers.retrying({timeout = 1}, function()
        t.assert_not(replica.process:is_alive())
    end)

    cg.master:exec(function()
        box.error.injection.set('ERRINJ_WAL_DELAY_COOLDOWN', -1)
    end)
    replica:drop()
end

g.test_applier_stops_on_error_during_fetch_snapshot_rebootstrap = function(cg)
    local replica = server:new{
        alias = 'replica',
        box_cfg = { replication = { g.master.net_box_uri } },
    }
    replica:start()
    replica:stop()

    cg.master:exec(function()
        for _, tuple in box.space._gc_consumers:pairs() do
            box.space._gc_consumers:delete{tuple[1]}
        end
    end)
    cg.master:restart()

    cg.master:exec(function()
        box.cfg{wal_cleanup_delay = 0, checkpoint_count = 1}
        box.space.test:insert{3, 'data'}
        box.snapshot()
        box.error.injection.set('ERRINJ_REPLICA_JOIN_DELAY', true)
    end)

    replica:start({wait_until_ready = false})
    t.helpers.retrying({}, function()
        t.assert_not_equals(replica:grep_log('initiating rebootstrap'), nil)
        t.assert_not_equals(cg.master:grep_log('initial data sent'), nil)
    end)
    cg.master.process:kill("KILL");
    cg.master:restart()

    t.helpers.retrying({timeout = 1}, function()
        t.assert_not(replica.process:is_alive())
    end)

    cg.master:exec(function()
        box.error.injection.set('ERRINJ_REPLICA_JOIN_DELAY', false)
    end)
    replica:drop()
end
