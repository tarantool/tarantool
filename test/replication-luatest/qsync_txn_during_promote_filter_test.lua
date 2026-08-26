local replica_set = require('luatest.replica_set')
local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

--
-- An incoming synchro request has to be filtered before being applied. Some of
-- the validation checks compare the request against the LSNs of the queued
-- transactions, so the filtering sometimes is supposed to start by waiting
-- until all the pending WAL writes of the queue are finished.
--
-- The queue needs to be protected from new writes during that. Otherwise the
-- waiting in the filter would never end until the writes stop coming for some
-- other reason, thus also not allowing the synchro request to proceed.
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
            box.error.injection.set('ERRINJ_WAL_DELAY', false)
        end)
    end
    cg.replica_set:drop()
end)

g.test_txn_during_promote_filter = function(cg)
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
    -- The PROMOTE arrives at node 1 and gets stuck in the filtering, waiting
    -- for the WAL write of transaction 1 to finish. The wait happens under
    -- the limbo latch.
    cg.node1:exec(function()
        t.helpers.retrying({timeout = 60}, function()
            t.assert(box.info.synchro.queue.busy)
        end)
    end)
    -- Transaction 2 - tries to enter the queue while the PROMOTE is being
    -- filtered. It must be rejected right away. Otherwise a stream of such txns
    -- would indefinitely restart the filter's waiting, thus starving the
    -- PROMOTE.
    --
    -- Node 1 is already read-only here - it has learned the new raft term
    -- before receiving the PROMOTE row and stepped down from the leadership.
    -- Hence the transaction goes into a replica-local space, which is writable
    -- regardless.
    cg.node1:exec(function()
        t.assert(box.info.ro)
        local f = _G.fiber.new(function() box.space.l:replace{3} end)
        f:set_joinable(true)
        _G.fiber.yield()
        t.assert_equals(box.info.synchro.queue.len, 1)
        local ok, err = f:join(60)
        t.assert_not(ok)
        t.assert_equals(err.code, box.error.SYNC_ROLLBACK)
    end)
    -- Let the WAL write of transaction 1 finish. The filter wait ends, and
    -- the PROMOTE gets applied, rolling transaction 1 back.
    cg.node1:exec(function(node2_id)
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
        local ok, err = rawget(_G, 'txn1_fiber'):join(60)
        t.assert_not(ok)
        t.assert_equals(err.code, box.error.SYNC_ROLLBACK)
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
