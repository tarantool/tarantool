local t = require('luatest')
local cluster = require('luatest.replica_set')
local fio = require('fio')

local g = t.group('gh_5158')

g.before_each(function(cg)
    cg.cluster = cluster:new({})
    cg.master = cg.cluster:build_and_add_server{
        alias = 'master',
        box_cfg = {
            checkpoint_count = 5,
        },
    }
    cg.replica = cg.cluster:build_and_add_server{
        alias = 'replica',
        box_cfg = {
            replication = {
                cg.master.net_box_uri,
            },
        },
    }
    cg.cluster:start()
end)

g.after_each(function(cg)
    cg.cluster:drop()
end)

g.test_invalid_recovery_from_xlog = function(cg)
    cg.master:exec(function()
        box.schema.space.create("test")
        box.snapshot()
        box.space.test:create_index("pk")
        box.snapshot()
    end)

    -- Wait until everything is replicated from the master to the replica
    t.helpers.retrying({}, function()
        cg.replica:wait_for_vclock_of(cg.master)
    end)

    -- Delete all *.xlogs on the replica
    cg.replica:stop()
    local glob = fio.pathjoin(cg.replica.workdir, '*.xlog')
    local xlogs = fio.glob(glob)
    for _, file in pairs(xlogs) do fio.unlink(file) end

    -- We start the replica without replication so that the replica
    -- does not catch up to the maximum vclock. Next, set
    -- ERRINJ_WAL_DELAY_COUNTDOWN. And then we replicate.
    -- The replica acknowledges that it has received part of the data
    -- due to ERRINJ_WAL_DELAY_COUNTDOWN. As a result, on the intermediate
    -- xlog on which the assert is fired (signature < prev_signature),
    -- the GC is started.
    --
    -- A batch from WAL can immediately come to the replica. Accordingly,
    -- ERRINJ_WAL_DELAY_COUNTDOWN stops the entire batch (the test
    -- freezes), because the entire batch has arrived and the vclock
    -- will catch up to the maximum value. Therefore, it is necessary
    -- to use wal_queue_max_size. If wal_queue_max_size is equal to 1,
    -- then transactions will come to WAL one at a time. The
    -- replication_sync_timeout is set to 0.1 to avoid waiting 300
    -- seconds when trying to synchronize with the master after updating
    -- the replica configuration.
    cg.replica.box_cfg.replication = nil
    cg.replica:start()
    cg.replica:exec(function(uri)
        box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', 2)
        box.cfg{wal_queue_max_size = 1}
        box.cfg{replication = uri, replication_sync_timeout = 0.1}
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
    end, {cg.master.net_box_uri})

    -- Check that the master isn't dead
    t.assert_equals(cg.master:exec(function()
        return box.info.status
    end), 'running')
    t.helpers.retrying({}, function()
        cg.replica:assert_follows_upstream(1)
    end)
end
