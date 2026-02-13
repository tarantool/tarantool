local server = require('luatest.server')
local replica_set = require('luatest.replica_set')
local t = require('luatest')
local g = t.group()

--
-- gh-12205: volatile limbo transactions couldn't be rolled back by an external
-- PROMOTE coming from a new leader. That PROMOTE tried to wait for all entries
-- being persisted before rolling anything back. And the last entries couldn't
-- be persisted because they were waiting for the older txns to get finished.
-- Which in turn would never happen, since these older txns were supposed to get
-- rolled back by this stuck PROMOTE. This was a deadlock.
--
g.before_all(function(cg)
    cg.replica_set = replica_set:new()
    cg.replication = {
        server.build_listen_uri('instance1', cg.replica_set.id),
        server.build_listen_uri('instance2', cg.replica_set.id),
    }
    local box_cfg = {
        replication = cg.replication,
        replication_timeout = 0.1,
        replication_synchro_queue_max_size = 1,
        replication_synchro_timeout = 1000,
        election_mode = 'manual',
    }
    cg.instance1 = cg.replica_set:build_and_add_server{
        alias = 'instance1',
        box_cfg = box_cfg,
    }
    box_cfg.election_mode = 'voter'
    cg.instance2 = cg.replica_set:build_and_add_server{
        alias = 'instance2',
        box_cfg = box_cfg,
    }
    cg.replica_set:start()
    cg.replica_set:wait_for_fullmesh()
    cg.instance1:exec(function()
        box.ctl.promote()
        box.schema.space.create('test', {is_sync = true})
        box.space.test:create_index('pk')
    end)
    cg.instance2:wait_for_vclock_of(cg.instance1)
end)

g.after_all(function(cg)
    cg.replica_set:drop()
end)

g.test_case = function(cg)
    --
    -- Old leader spawns some transactions, some of which get stuck in volatile
    -- states.
    --
    cg.instance1:exec(function()
        local fiber = require('fiber')
        local s = box.space.test
        -- Make sure no CONFIRM happens.
        box.cfg{replication_synchro_quorum = 3}
        rawset(_G, 'f1', fiber.new(s.insert, s, {1}))
        _G.f1:set_joinable(true)
        rawset(_G, 'f2', fiber.new(s.insert, s, {2}))
        _G.f2:set_joinable(true)
        rawset(_G, 'f3', fiber.new(s.insert, s, {3}))
        _G.f3:set_joinable(true)
        fiber.yield()
        -- 1 is waiting for quorum, 2 are volatile.
        t.assert_equals(box.info.synchro.queue.len, 1)
    end)
    cg.instance2:wait_for_vclock_of(cg.instance1)
    --
    -- New leader gets elected. It will confirm one txn that it actually saw.
    -- The other txns (the volatile ones) the old leader must rollback.
    --
    cg.instance2:exec(function()
        box.cfg{election_mode = 'manual'}
        t.helpers.retrying({timeout = 120}, box.ctl.promote)
        t.assert_equals(box.space.test:select(), {{1}})
    end)
    cg.instance1:wait_for_vclock_of(cg.instance2)
    cg.instance1:exec(function()
        local function join_as_rollback(f)
            local timeout = 120
            local ok, err = f:join(timeout)
            t.assert_not(ok)
            t.assert_equals(err.code, box.error.SYNC_ROLLBACK)
        end
        -- This was confirmed by the new leader.
        t.assert((_G.f1:join()))
        -- These never even reached the journal.
        join_as_rollback(_G.f2)
        join_as_rollback(_G.f3)
        t.assert_equals(box.info.synchro.queue.len, 0)
        t.assert_equals(box.space.test:select(), {{1}})
    end)
    --
    -- Make sure the replication still works.
    --
    cg.instance2:exec(function()
        box.space.test:replace{4}
    end)
    cg.instance1:wait_for_vclock_of(cg.instance2)
    cg.instance1:exec(function()
        box.cfg{replication_synchro_quorum = box.NULL}
        t.assert_equals(box.space.test:select(), {{1}, {4}})
        t.helpers.retrying({timeout = 120}, box.ctl.promote)
        box.space.test:replace{5}
    end)
    cg.instance1:wait_for_vclock_of(cg.instance1)
    cg.instance2:exec(function()
        t.assert_equals(box.space.test:select(), {{1}, {4}, {5}})
    end)
end
