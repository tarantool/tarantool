local replica_set = require('luatest.replica_set')
local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

--
-- gh-13095: there was the following bug.
--
-- A synchronous transaction, whose submission into the limbo got blocked on
-- the queue being full, was parked inside the submission code waiting for free
-- space, not yet sent to the journal.
--
-- The protection against new transactions joining the queue during an ongoing
-- synchro request was only applied at the submission start.
--
-- It could happen that a transaction started the submission while there was no
-- fencing yet, then it got blocked on the synchro queue being full, and then
-- the fencing was activated.
--
-- The parked transaction then could be woken up in the middle of, for example,
-- a PROMOTE WAL write, not notice the fencing, but notice free space in the
-- synchro queue. Then it would happily become 'submitted' and get sent to WAL
-- right after the PROMOTE.
--
-- After the PROMOTE was written, the transaction itself would be rolled back in
-- memory as a part of the leadership change process.
--
-- Local recovery would resurrect it as a pending synchronous transaction again.
-- Also replicas would apply it even without any restarts.
--
g.before_each(function(cg)
    t.tarantool.skip_if_not_debug()
    cg.replica_set = replica_set:new{}
    local box_cfg = {
        replication = {
            server.build_listen_uri('node1', cg.replica_set.id),
            server.build_listen_uri('node2', cg.replica_set.id),
        },
        replication_timeout = 0.1,
        replication_synchro_quorum = 2,
        replication_synchro_timeout = 60,
        election_mode = 'manual',
    }
    -- Node 1 - the first leader. Owns the limbo, writes the transactions, and
    -- applies the new leader's PROMOTE in the end.
    cg.node1 = cg.replica_set:build_and_add_server{
        alias = 'node1',
        box_cfg = box_cfg,
    }
    -- Node 2 - the second leader. Gets elected while node 1 has a parked
    -- transaction, and its PROMOTE covers the first transaction.
    cg.node2 = cg.replica_set:build_and_add_server{
        alias = 'node2',
        box_cfg = box_cfg,
    }
    cg.replica_set:start()
    cg.replica_set:wait_for_fullmesh()
    cg.node1:exec(function()
        rawset(_G, 'fiber', require('fiber'))
        box.ctl.promote()
        box.schema.space.create('s', {is_sync = true}):create_index('p')
        -- One synchronous transaction already overflows the queue.
        box.cfg{replication_synchro_queue_max_size = 1}
    end)
    cg.node2:wait_for_vclock_of(cg.node1)
end)

g.after_each(function(cg)
    -- The injections must be dropped explicitly. A frozen limbo worker doesn't
    -- react to the fiber cancellation, so an instance left with the injection
    -- on would hang on shutdown.
    for _, instance in pairs({cg.node1, cg.node2}) do
        pcall(instance.exec, instance, function()
            box.error.injection.set('ERRINJ_TXN_LIMBO_WORKER_DELAY', false)
            box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', -1)
            box.error.injection.set('ERRINJ_WAL_DELAY', false)
        end)
    end
    cg.replica_set:drop()
end)

g.test_txn_during_promote_write = function(cg)
    -- Freeze the CONFIRM writing on node 1, so the first transaction stays in
    -- the queue even after gathering the quorum.
    cg.node1:exec(function()
        box.error.injection.set('ERRINJ_TXN_LIMBO_WORKER_DELAY', true)
    end)
    -- Transaction 1 - is written to the WAL of both nodes and fills the queue
    -- up to its max size.
    local lsn = cg.node1:exec(function()
        local lsn = box.info.lsn
        local f = _G.fiber.create(function() box.space.s:replace{1} end)
        f:set_joinable(true)
        rawset(_G, 'txn1_fiber', f)
        t.helpers.retrying({timeout = 60}, function()
            t.assert_gt(box.info.lsn, lsn)
        end)
        return box.info.lsn
    end)
    cg.node2:wait_for_vclock_of(cg.node1)
    -- Transaction 2 - the queue is full, so it parks inside the submission
    -- waiting for free space. It has no WAL row yet and is not accounted in
    -- the queue.
    cg.node1:exec(function(lsn)
        local f = _G.fiber.create(function() box.space.s:replace{2} end)
        f:set_joinable(true)
        rawset(_G, 'txn2_fiber', f)
        t.assert_equals(box.info.lsn, lsn)
        t.assert_equals(box.info.synchro.queue.len, 1)
        -- Catch the coming WAL writes.
        box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', 0)
    end, {lsn})
    -- Elect node 2. Its promotion is blocked on node 1's vote, whose WAL
    -- write is caught above, so it can't be waited on synchronously here.
    cg.node2:exec(function()
        local f = require('fiber').create(function() box.ctl.promote() end)
        f:set_joinable(true)
        rawset(_G, 'promote_fiber', f)
    end)
    cg.node1:exec(function(lsn)
        --
        -- Let the WAL writes (the election term and vote bumps) through one
        -- by one until the one made under the limbo latch is reached - that
        -- is the PROMOTE of the new leader.
        --
        -- Note that the CONFIRM from the beginning of this test is still
        -- blocked. The limbo worker isn't processing anything right now.
        --
        t.helpers.retrying({timeout = 60}, function()
            t.assert(box.error.injection.get('ERRINJ_WAL_DELAY'))
            if box.info.synchro.queue.busy then
                return
            end
            box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', 0)
            box.error.injection.set('ERRINJ_WAL_DELAY', false)
            t.fail('Not the synchro request write yet')
        end)
        -- Give the parked transaction 2 a reason to wake up in the middle of
        -- the PROMOTE write: raise the max size, which on its own doesn't
        -- wake anybody up, and then poke the queue with a synchro parameter
        -- change, which does.
        box.cfg{replication_synchro_queue_max_size = 1024 * 1024}
        box.cfg{replication_synchro_timeout = 59}
        _G.fiber.sleep(0.1)
        -- The transaction must not have gone to the journal while the
        -- PROMOTE is in progress.
        t.assert_equals(box.info.synchro.queue.len, 1)
        t.assert_equals(box.info.lsn, lsn)
        -- Release the PROMOTE.
        box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', -1)
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
    end, {lsn})
    cg.node2:exec(function()
        local ok, err = rawget(_G, 'promote_fiber'):join(60)
        t.assert_equals(err, nil)
        t.assert(ok)
    end)
    -- The PROMOTE commits the first transaction and rolls the second one
    -- back, without it ever getting into the journal.
    cg.node1:exec(function(lsn, node2_id)
        local ok, err = rawget(_G, 'txn1_fiber'):join(60)
        t.assert_equals(err, nil)
        t.assert(ok)
        ok, err = rawget(_G, 'txn2_fiber'):join(60)
        t.assert_not(ok)
        t.assert_equals(err.code, box.error.SYNC_ROLLBACK)
        -- Nothing went into node 1's own WAL after the first transaction.
        t.assert_equals(box.info.lsn, lsn)
        t.assert_equals(box.info.synchro.queue.len, 0)
        t.assert_equals(box.info.synchro.queue.owner, node2_id)
        t.assert_equals(box.space.s:select(), {{1}})
    end, {lsn, cg.node2:get_instance_id()})
end
