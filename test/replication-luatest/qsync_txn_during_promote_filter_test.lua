local replica_set = require('luatest.replica_set')
local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

--
-- The synchro requests deciding the fate of the queued transactions have to be
-- validated against the LSNs of these transactions before being applied. So
-- their filtering starts by waiting until all the pending WAL writes of the
-- queue are finished.
--
-- The queue needs to be protected from new writes during that, and until the
-- request is written itself. Otherwise the waiting in the filter would never
-- end until the writes stop coming for some other reason, thus also not
-- allowing the synchro request to proceed.
--
-- A PROMOTE waiting for acks doesn't decide anything though - it only becomes
-- pending, and the transactions keep flowing until its CONFIRM does.
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
    -- receives the new leader's PROMOTE in the end.
    cg.node1 = cg.replica_set:build_and_add_server{
        alias = 'node1',
        box_cfg = box_cfg,
    }
    -- Node 2 - the second leader. Takes the limbo over while node 1 has a
    -- transaction with an unfinished WAL write.
    box_cfg.read_only = true
    cg.node2 = cg.replica_set:build_and_add_server{
        alias = 'node2',
        box_cfg = box_cfg,
    }
    cg.replica_set:start()
    cg.replica_set:wait_for_fullmesh()
    cg.node1:exec(function()
        rawset(_G, 'fiber', require('fiber'))
        box.schema.space.create('s', {is_sync = true}):create_index('p')
        box.schema.space.create('l', {is_local = true}):create_index('p')
        -- A fully confirmed transaction, so both nodes agree on the confirmed
        -- LSN and the coming PROMOTE passes the filter cleanly.
        box.space.s:replace{1}
    end)
    cg.node2:wait_for_vclock_of(cg.node1)
end)

g.after_each(function(cg)
    -- The injections must be dropped explicitly. An instance left with the
    -- injection on would hang on shutdown.
    for _, instance in pairs({cg.node1, cg.node2}) do
        pcall(instance.exec, instance, function()
            box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', -1)
            box.error.injection.set('ERRINJ_WAL_DELAY', false)
        end)
    end
    cg.replica_set:drop()
end)

g.test_txn_during_promote_and_confirm = function(cg)
    -- Transaction 1 - enters the queue, but its WAL write is blocked. The queue
    -- now has an entry with an unknown LSN.
    cg.node1:exec(function()
        box.error.injection.set('ERRINJ_WAL_DELAY', true)
        local lsn = box.info.lsn
        local f = _G.fiber.new(function() box.space.s:replace{2} end)
        f:set_joinable(true)
        rawset(_G, 'txn1_fiber', f)
        _G.fiber.yield()
        t.assert_equals(box.info.synchro.queue.len, 1)
        t.assert_equals(box.info.lsn, lsn)
    end)
    -- The takeover happens entirely on node 2 - it doesn't need anything from
    -- node 1 whose WAL is blocked (assume it got votes from some other nodes).
    cg.node2:exec(function()
        box.cfg{read_only = false, replication_synchro_quorum = 1}
        box.ctl.promote()
    end)
    -- The PROMOTE arrives at node 1. It only becomes pending, so it doesn't
    -- need to know the LSNs of the queued transactions and goes straight to
    -- its WAL write, where it gets stuck - the WAL is blocked. The write
    -- happens under the limbo latch.
    cg.node1:exec(function()
        t.helpers.retrying({timeout = 60}, function()
            t.assert(box.info.synchro.queue.busy)
        end)
    end)
    -- Transaction 2 - enters the queue while the PROMOTE is being written. It
    -- is allowed - the transactions keep flowing until the PROMOTE's CONFIRM
    -- decides their fate.
    --
    -- Node 1 is already read-only here - it has learned the new raft term
    -- before receiving the PROMOTE row and stepped down from the leadership.
    -- Hence the transaction goes into a replica-local space, which is writable
    -- regardless.
    cg.node1:exec(function()
        t.assert(box.info.ro)
        local f = _G.fiber.new(function() box.space.l:replace{3} end)
        f:set_joinable(true)
        rawset(_G, 'txn2_fiber', f)
        _G.fiber.yield()
        t.assert_equals(box.info.synchro.queue.len, 2)
    end)
    -- Let the WAL writes through one by one, until the PROMOTE is committed,
    -- and the next synchro request is being processed under the latch. That is
    -- the PROMOTE's CONFIRM - node 2 got the quorum right away. The CONFIRM
    -- decides the fates, so the queue is fenced off while it waits for the
    -- pending WAL writes in its filter, and while it is being written itself.
    -- The test doesn't care which one of these is caught.
    cg.node1:exec(function(node2_id)
        t.helpers.retrying({timeout = 60}, function()
            t.assert(box.error.injection.get('ERRINJ_WAL_DELAY'))
            local node = box.info.synchro.nodes[node2_id]
            if node ~= nil and node.promote ~= nil and
               box.info.synchro.queue.busy then
                return
            end
            box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', 0)
            box.error.injection.set('ERRINJ_WAL_DELAY', false)
            t.fail('Not the CONFIRM yet')
        end)
    end, {cg.node2:get_instance_id()})
    -- Transaction 3 - tries to enter the queue while the CONFIRM is in
    -- progress. It must be rejected right away. Otherwise a stream of such
    -- txns would indefinitely restart the filter's waiting, thus starving the
    -- CONFIRM.
    cg.node1:exec(function()
        local f = _G.fiber.new(function() box.space.l:replace{4} end)
        f:set_joinable(true)
        _G.fiber.yield()
        t.assert_equals(box.info.synchro.queue.len, 2)
        local ok, err = f:join(60)
        t.assert_not(ok)
        t.assert_equals(err.code, box.error.SYNC_ROLLBACK)
    end)
    -- Release the CONFIRM. It applies the PROMOTE, rolling both transactions
    -- back - they are beyond the PROMOTE's confirm boundary.
    cg.node1:exec(function(node2_id)
        box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', -1)
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
        for _, name in pairs({'txn1_fiber', 'txn2_fiber'}) do
            local ok, err = rawget(_G, name):join(60)
            t.assert_not(ok)
            t.assert_equals(err.code, box.error.SYNC_ROLLBACK)
        end
        t.helpers.retrying({timeout = 60}, function()
            t.assert_equals(box.info.synchro.queue.owner, node2_id)
        end)
        t.assert_equals(box.info.synchro.queue.len, 0)
        t.assert_equals(box.space.s:select(), {{1}})
        t.assert_equals(box.space.l:select(), {})
    end, {cg.node2:get_instance_id()})
    -- The old leader's transaction 1 is nopified on the new leader.
    cg.node2:wait_for_vclock_of(cg.node1)
    cg.node2:exec(function()
        t.assert_equals(box.info.synchro.queue.owner, box.info.id)
        t.assert_equals(box.space.s:select(), {{1}})
    end)
end
