local t = require('luatest')
local uuid = require('uuid')
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
    local replica_uuid = uuid.str()
    cg.master:exec(function()
        box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', 0)
    end)
    local replica = server:new{
        alias = 'replica',
        box_cfg = { instance_uuid = replica_uuid,
                    replication = { cg.master.net_box_uri } },
    }

    replica:start({wait_until_ready = false})
    -- Block _cluster WAL write after snapshot is sent but before OK marker.
    local initial_cluster_count = cg.master:exec(function()
        return box.space._cluster:count()
    end)
    cg.master:exec(function(initial_count)
        t.helpers.retrying({timeout=10}, function(initial_count)
            t.assert(box.error.injection.get('ERRINJ_WAL_DELAY'))
            local is_cluster_write = box.space._cluster:count() > initial_count
            if not is_cluster_write then
                box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', 0)
                box.error.injection.set('ERRINJ_WAL_DELAY', false)
            end
            t.assert(is_cluster_write)
        end, initial_count)
    end, {initial_cluster_count})

    t.helpers.retrying({timeout=10}, function()
        t.assert_not_equals(cg.master:grep_log('initial data sent'), nil)
    end)
    cg.master.process:kill("KILL");
    cg.master:restart()

    -- Applier should see that replica has some data and stop itself from
    -- reconnecting.
    t.helpers.retrying({timeout = 10}, function()
        t.assert_not(replica.process:is_alive())
        t.assert_not_equals(replica:grep_log('Error occurred during' ..
        ' fetching snapshot, but some data has been applied.' ..
        ' Stopping applier'), nil)
        t.assert_equals(replica:grep_log('will retry every'), nil)
    end)

    cg.master:exec(function(replica_uuid)
        box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', -1)
        box.ctl.replica_gc(replica_uuid)
    end, {replica_uuid})
    replica:drop()
end

g.test_applier_stops_on_error_during_fetch_snapshot_rebootstrap = function(cg)
    local replica_uuid = uuid.str()
    local replica = server:new{
        alias = 'replica',
        box_cfg = { instance_uuid = replica_uuid,
                    replication = { cg.master.net_box_uri } },
    }
    replica:start()
    replica:stop()

    cg.master:exec(function(replica_uuid)
        box.ctl.replica_gc(replica_uuid)
        box.cfg{wal_cleanup_delay = 0, checkpoint_count = 1}
        box.space.test:insert{3, 'data'}
        box.snapshot()
        box.error.injection.set('ERRINJ_REPLICA_JOIN_DELAY', true)
    end, {replica_uuid})

    replica:start({wait_until_ready = false})
    t.helpers.retrying({timeout=10}, function()
        t.assert_not_equals(replica:grep_log('initiating rebootstrap'), nil)
        t.assert_not_equals(cg.master:grep_log('initial data sent'), nil)
    end)
    cg.master.process:kill("KILL");
    cg.master:restart()

    t.helpers.retrying({timeout = 10}, function()
        t.assert_not(replica.process:is_alive())
        t.assert_not_equals(replica:grep_log('Error occurred during' ..
        ' fetching snapshot, but some data has been applied.' ..
        ' Stopping applier'), nil)
        t.assert_equals(replica:grep_log('will retry every'), nil)
    end)

    cg.master:exec(function()
        box.error.injection.set('ERRINJ_REPLICA_JOIN_DELAY', false)
    end)
    replica:drop()
end
