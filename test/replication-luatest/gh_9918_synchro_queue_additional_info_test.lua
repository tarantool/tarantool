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
    end)
    cg.replica_set:wait_for_fullmesh()
    cg.leader:wait_for_downstream_to(cg.replica)
end)

g.after_each(function(cg)
    cg.replica_set:drop()
end)

-- Test that new `age` field of `box.info.synchro.queue` works correctly.
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
