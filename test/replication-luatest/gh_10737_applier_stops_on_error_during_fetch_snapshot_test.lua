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

    cg.master:exec(function(replica_uuid)
        box.error.injection.set('ERRINJ_REPLICA_JOIN_DELAY', false)
        -- The replica got registered before the master was killed. A dead
        -- registered member would prevent any new replica from joining.
        box.ctl.replica_gc(replica_uuid)
        local id = box.space._cluster.index.uuid:get(replica_uuid)[1]
        box.space._cluster:delete(id)
    end, {replica_uuid})
    replica:drop()
end

--
-- The join metadata (the synchro queue and Raft states) is applied even before
-- the snapshot data. It changes the local state too, so a retry after it is not
-- safe either.
--
g.test_applier_stops_on_error_after_join_meta = function(cg)
    local replica_uuid = uuid.str()
    local flush_size = cg.master:exec(function()
        -- Make the relay send each row right away. Otherwise the metadata
        -- would stay in the relay's buffer until the snapshot data.
        local tweaks = require('internal.tweaks')
        local flush_size = tweaks.xrow_stream_flush_size
        tweaks.xrow_stream_flush_size = 1
        -- Stop the master after the metadata is sent, before the data.
        box.error.injection.set('ERRINJ_ENGINE_JOIN_DELAY', true)
        return flush_size
    end)
    local replica = server:new{
        alias = 'replica',
        box_cfg = { instance_uuid = replica_uuid,
                    replication = { cg.master.net_box_uri },
                    -- The metadata application is logged as verbose.
                    log_level = 6 },
    }
    replica:start({wait_until_ready = false})
    -- Wait for the replica to apply the metadata. The Raft state comes first
    -- in it, the synchro queue state right after, both sent before the master
    -- stopped. So the latter is applied too, before the master's death is
    -- noticed.
    t.helpers.retrying({timeout = 10}, function()
        t.assert_not_equals(replica:grep_log('RAFT: recover'), nil)
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

    -- The replica didn't get registered - the master was killed before that.
    cg.master:exec(function(flush_size)
        require('internal.tweaks').xrow_stream_flush_size = flush_size
    end, {flush_size})
    replica:drop()
end
