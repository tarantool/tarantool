local fiber = require('fiber')
local t = require('luatest')
local replica_set = require('luatest.replica_set')
local server = require('luatest.server')

local g = t.group()

g.before_each(function(cg)
    cg.replica_set = replica_set:new{}
    local box_cfg = {
        replication = {
            server.build_listen_uri('server1', cg.replica_set.id),
            server.build_listen_uri('server2', cg.replica_set.id),
        },
        replication_timeout = 0.1,
        replication_synchro_timeout = 60,
    }
    cg.leader = cg.replica_set:build_and_add_server{
        alias = 'server1',
        box_cfg = box_cfg,
    }
    cg.replica = cg.replica_set:build_and_add_server{
        alias = 'server2',
        box_cfg = box_cfg,
    }
    cg.replica_set:start()
    cg.leader:exec(function()
        box.ctl.promote()
        local s = box.schema.space.create('s', {is_sync = true})
        s:create_index('pk')
        local as = box.schema.space.create('as', {is_sync = false})
        as:create_index('pk')
    end)
    cg.replica_set:wait_for_fullmesh()
    cg.leader:wait_for_downstream_to(cg.replica)
end)

g.after_each(function(cg)
    cg.replica_set:drop()
end)

-- Test that the new `age` field of `box.info.synchro.queue` works correctly.
g.test_age_field = function(cg)
    -- The synchronous queue is originally empty.
    cg.leader:exec(function()
        t.assert_equals(box.info.synchro.queue.age, 0)
    end)
    cg.replica:exec(function()
        t.assert_equals(box.info.synchro.queue.age, 0)
    end)

    -- Ensure that the synchronous transactions stay in the queue for a while.
    cg.leader:update_box_cfg{replication_synchro_quorum = 3}
    -- Add the first entry to the synchronous queue.
    local fid1 = cg.leader:exec(function()
        local fiber = require('fiber')

        local f = fiber.new(function() box.space.s:replace{0} end)
        f:set_joinable(true)
        return f:id()
    end)
    local wait_time = 0.1
    fiber.sleep(wait_time)

    local leader_age = cg.leader:exec(function(wait_time)
        t.assert_ge(box.info.synchro.queue.age, wait_time)
        t.assert_le(box.info.synchro.queue.age,
                    box.cfg.replication_synchro_timeout)
        return box.info.synchro.queue.age
    end, {wait_time})
    local replica_age = cg.replica:exec(function()
        t.assert_ge(box.info.synchro.queue.age, 0)
        t.assert_le(box.info.synchro.queue.age,
                    box.cfg.replication_synchro_timeout)
        return box.info.synchro.queue.age
    end)

    -- Add another entry to the synchronous queue.
    local fid2 = cg.leader:exec(function()
        local f = require('fiber').new(function() box.space.s:replace{0} end)
        f:set_joinable(true)
        return f:id()
    end)
    fiber.sleep(wait_time)

    -- The age of the oldest synchronous queue entry must be shown.
    cg.leader:exec(function(age)
        t.assert_ge(box.info.synchro.queue.age, age)
        t.assert_le(box.info.synchro.queue.age,
                    box.cfg.replication_synchro_timeout)
    end, {leader_age})
    cg.replica:exec(function(age)
        t.assert_ge(box.info.synchro.queue.age, age)
        t.assert_le(box.info.synchro.queue.age,
                    box.cfg.replication_synchro_timeout)
    end, {replica_age})

    -- Allow the synchronous queue to advance.
    cg.leader:update_box_cfg{replication_synchro_quorum = ''}
    cg.leader:exec(function(fids)
        for _, fid in ipairs(fids) do
           t.assert(require('fiber').find(fid):join())
        end
    end, {{fid1, fid2}})
    cg.leader:wait_for_downstream_to(cg.replica)

    -- The synchronous queue must become empty by this time.
    cg.leader:exec(function()
        t.assert_equals(box.info.synchro.queue.age, 0)
    end)
    cg.replica:exec(function()
        t.assert_equals(box.info.synchro.queue.age, 0)
    end)
end

-- Test that the new `confirm_lag` field of `box.info.synchro.queue` works
-- correctly.
g.test_confirm_lag_field = function(cg)
    -- The were no synchronous transactions since the start.
    cg.leader:exec(function()
        t.assert_equals(box.info.synchro.queue.confirm_lag, 0)
    end)
    cg.replica:exec(function()
        t.assert_equals(box.info.synchro.queue.confirm_lag, 0)
    end)

    -- Ensure that the synchronous transaction stays in the queue for a while.
    cg.leader:update_box_cfg{replication_synchro_quorum = 3}
    -- Add an entry to the synchronous queue.
    local sync_fid = cg.leader:exec(function()
        local fiber = require('fiber')

        local f = fiber.new(function() box.space.s:replace{0} end)
        f:set_joinable(true)
        return f:id()
    end)
    local wait_time = 0.1
    fiber.sleep(wait_time)

    -- Nothing has been confirmed yet.
    cg.leader:exec(function()
        t.assert_equals(box.info.synchro.queue.confirm_lag, 0)
    end)
    cg.replica:exec(function()
        t.assert_equals(box.info.synchro.queue.confirm_lag, 0)
    end)

    -- Add an asynchronous transaction to the synchronous queue right before
    -- allowing it to advance: it must not affect the confirm lag.
    local async_fid = cg.leader:exec(function()
        local fiber = require('fiber')

        local f = fiber.new(function() box.space.as:replace{0} end)
        f:set_joinable(true)
        t.helpers.retrying({timeout = 60}, function()
            t.assert_equals(box.info.synchro.queue.len, 2)
        end)
        return f:id()
    end)
    cg.leader:wait_for_downstream_to(cg.replica)

    -- Allow the synchronous queue to advance.
    cg.leader:exec(function(sync_fid, async_fid)
        local fiber = require('fiber')

        box.cfg{replication_synchro_quorum = ''}
        t.assert(fiber.find(sync_fid):join())
        t.assert(fiber.find(async_fid):join())
    end, {sync_fid, async_fid})

    -- The confirm lag must now be shown.
    cg.leader:exec(function(wait_time)
        t.assert_ge(box.info.synchro.queue.confirm_lag, wait_time)
        t.assert_le(box.info.synchro.queue.confirm_lag,
                    box.cfg.replication_synchro_timeout)
        return box.info.synchro.queue.confirm_lag
    end, {wait_time})
    cg.replica:exec(function()
        t.assert_ge(box.info.synchro.queue.confirm_lag, 0)
        t.assert_le(box.info.synchro.queue.confirm_lag,
                    box.cfg.replication_synchro_timeout)
        return box.info.synchro.queue.confirm_lag
    end)
end

-- Test that new `confirm_lag` field of `box.info.synchro.queue` works
-- correctly when prepared (not committed) asynchronous transactions are present
-- in the synchronous queue.
g.test_confirm_lag_field_with_prepared_async_tx = function(cg)
    t.tarantool.skip_if_not_debug()
    -- Ensure that the synchronous transaction stays in the queue for a while.
    cg.leader:update_box_cfg{wal_queue_max_size = 1,
                             replication_synchro_quorum = 2,
                             replication = ''}

    -- Add an entry to the synchronous queue.
    local sync_fid = cg.leader:exec(function()
        local fiber = require('fiber')

        -- Block the WAL on the confirm record.
        box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', 1)
        local f = fiber.new(function() box.space.s:replace{0} end)
        f:set_joinable(true)
        return f:id()
    end)
    local wait_time = 0.1
    fiber.sleep(wait_time)

    -- Allow the synchronous queue to advance.
    cg.leader:exec(function(sync_fid)
        local fiber = require('fiber')

        box.cfg{replication_synchro_quorum = ''}
        -- Wait for the WAL to get blocked on the confirm record.
        t.helpers.retrying({timeout = 60}, function()
            t.assert(box.error.injection.get('ERRINJ_WAL_DELAY'))
        end)
        -- Block the WAL on the asynchronous transaction record.
        box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', 0)
        fiber.new(function() box.space.as:replace{0} end)
        -- Let the confirm record to get written.
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
        -- Make sure the WAL gets blocked on the prepared (not committed)
        -- asynchronous transaction record.
        t.helpers.retrying({timeout = 60}, function()
            t.assert(box.error.injection.get('ERRINJ_WAL_DELAY'))
        end)

        t.assert(fiber.find(sync_fid):join())
        t.assert(box.info.synchro.queue.len, 0)

        -- Unblock the WAL thread.
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
    end, {sync_fid})

    -- The confirm lag must now be shown.
    cg.leader:exec(function(wait_time)
        t.assert_ge(box.info.synchro.queue.confirm_lag, wait_time)
        t.assert_le(box.info.synchro.queue.confirm_lag,
                    box.cfg.replication_synchro_timeout)
        return box.info.synchro.queue.confirm_lag
    end, {wait_time})
end

g.after_test('test_confirm_lag_field_with_prepared_async_tx', function(cg)
    t.tarantool.skip_if_not_debug()
    cg.leader:exec(function()
        box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', -1)
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
    end)
end)
