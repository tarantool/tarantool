local t = require('luatest')
local server = require('luatest.server')
local replica_set = require('luatest.replica_set')

local g = t.group()

local TIMEOUT = 60

--
-- gh-13668: a replica's SUBSCRIBE vclock is taken by the relay as acked, but
-- the SUBSCRIBE carries no Raft term. An ack from a replica living in a newer
-- term must not count - the term ends the leadership instead, that is what the
-- relay does with the heartbeats, whose term is processed before their vclock.
--
-- The bug was that the relay was using SUBSCRIBE's vclock as an actual ack,
-- not seeing that it came from a newer term, which allowed the instance to
-- confirm transactions still in an older term, causing a split-brain.
--

g.before_all(function()
    t.tarantool.skip_if_not_debug()
end)

g.before_each(function(cg)
    cg.replica_set = replica_set:new{}
    local box_cfg = {
        replication_timeout = 0.1,
        replication_synchro_quorum = 2,
        replication_synchro_timeout = TIMEOUT,
        election_mode = 'manual',
        election_fencing_mode = 'off',
        replication = {},
    }
    for i = 1, 3 do
        table.insert(box_cfg.replication,
                     server.build_listen_uri('node' .. i, cg.replica_set.id))
    end
    cg.uris = table.copy(box_cfg.replication)
    cg.nodes = {}
    for i = 1, 3 do
        cg.nodes[i] = cg.replica_set:build_and_add_server{
            alias = 'node' .. i,
            box_cfg = box_cfg,
        }
    end
    cg.replica_set:start()
    cg.replica_set:wait_for_fullmesh()
    --
    -- The held acks make the connections silent one way which might cause a
    -- disconnect when the replication timeout is too small. For this test it is
    -- important that the disconnect and reconnect don't happen unexpectedly as
    -- this would produce a sudden SUBSCRIBE, which is exactly what needs to be
    -- tested here in a controllable way.
    --
    -- The timeout is raised on fresh connections: a read in flight keeps the
    -- budget it started with, while the heartbeats of the peer already come at
    -- the new pace. And the bootstrap can't use the huge timeout right away -
    -- its retries would be as rare.
    --
    for _, node in pairs(cg.nodes) do
        node:update_box_cfg{replication = {}}
    end
    for _, node in pairs(cg.nodes) do
        node:update_box_cfg{
            replication_timeout = TIMEOUT,
            replication = cg.uris,
        }
    end
    cg.replica_set:wait_for_fullmesh()
end)

g.after_each(function(cg)
    cg.replica_set:drop()
end)

--
-- Three nodes, quorum 2. Node 1 leads and writes a transaction, the acks of
-- nodes 2 and 3 are held. Node 2 takes the next term with node 3's vote. Node
-- 3 reconnects to node 1: its SUBSCRIBE vclock has the transaction, but must
-- not count. Its first real ack, read after that, carries the term: node 1
-- must step down on it, with the transaction still pending.
--
g.test_resubscribe_ack_from_newer_term = function(cg)
    local node1, node2, node3 = unpack(cg.nodes)
    local node2_id = node2:get_instance_id()
    local node3_id = node3:get_instance_id()
    node1:exec(function()
        box.ctl.promote()
        box.schema.space.create('s', {is_sync = true}):create_index('p')
    end)
    node2:wait_for_vclock_of(node1)
    node3:wait_for_vclock_of(node1)
    local term = node1:get_election_term()
    for _, node in pairs(cg.nodes) do
        node:exec(function(timeout)
            -- Make sure a transaction can't be removed by an old leader on
            -- timeout. It could mess with the logic of queue state checking.
            require('compat').replication_synchro_timeout = 'new'
            box.cfg{txn_synchro_timeout = timeout}
        end, {TIMEOUT})
    end
    -- Node 1 listens to nobody: the only news it gets are the acks.
    node1:update_box_cfg{replication = {}}
    for _, node in pairs({node2, node3}) do
        node:exec(function()
            box.error.injection.set('ERRINJ_APPLIER_WRITER_DELAY', true)
        end)
    end
    -- A transaction is sent  to both followers, but they don't ack it.
    node1:exec(function()
        box.atomic({wait = 'submit'}, function()
            box.space.s:replace{1}
        end)
        t.assert_equals(box.info.synchro.queue.len, 1)
        -- Make sure the txn gets an LSN before waiting on vclock replication.
        box.ctl.wal_sync()
    end)
    node2:wait_for_vclock_of(node1)
    node3:wait_for_vclock_of(node1)
    --
    -- Node 2 takes the next term with node 3's vote. Its promotion then waits
    -- for the transaction's acks, held as well - it stays in progress.
    --
    node2:exec(function(timeout)
        local f = require('fiber').new(box.ctl.promote)
        f:set_joinable(true)
        rawset(_G, 'promote', f)
        t.helpers.retrying({timeout = timeout}, function()
            t.assert_equals(box.info.election.state, 'leader')
        end)
    end, {TIMEOUT})
    node3:exec(function(timeout, term)
        t.helpers.retrying({timeout = timeout}, function()
            t.assert_gt(box.info.election.term, term)
        end)
    end, {TIMEOUT, term})
    --
    -- Node 3 reconnects to node 1. Its SUBSCRIBE vclock has the transaction,
    -- and node 3 lives in the newer term. The vclock must not count as an
    -- ack. Node 3's first real ack, let through once the subscription is up,
    -- carries the term, and the term must end node 1's leadership.
    --
    node1:exec(function()
        -- Make sure no real acks reach node1's TX thread.
        box.error.injection.set('ERRINJ_RELAY_READ_ACK_DELAY', true)
    end)
    -- Node3 sends SUBSCRIBE to node1.
    node3:update_box_cfg{replication = {cg.uris[2]}}
    node3:update_box_cfg{replication = {cg.uris[1], cg.uris[2]}}
    node1:exec(function(timeout, node3_id)
        t.helpers.retrying({timeout = timeout}, function()
            local downstream = box.info.replication[node3_id].downstream
            t.assert_equals(downstream.status, 'follow')
        end)
    end, {TIMEOUT, node3_id})
    -- Acks are unblocked from node3 to node2. The node1 still ignores them
    -- as its relay reads are blocked by another injection.
    node3:exec(function()
        box.error.injection.set('ERRINJ_APPLIER_WRITER_DELAY', false)
    end)
    -- Node2 gets ack from node3 and its PROMOTE wins, including the first
    -- transaction of node1.
    node2:exec(function(timeout)
        t.assert(_G.promote:join(timeout))
        rawset(_G, 'promote', nil)
    end, {TIMEOUT})
    -- The node1 makes a second txn. It will go through WAL to the relays. The
    -- relays will then send the txn to the peers, and report the last seen
    -- vclock to TX thread - the SUBSCRIBE vclock. If node1 would see this as an
    -- ack, it would confirm at least the first txn now.
    local lsn = node1:exec(function()
        box.atomic({wait = 'submit'}, function()
            box.space.s:replace{2}
        end)
        t.assert_equals(box.info.synchro.queue.len, 2)
        -- The submit returns before the row gets its LSN.
        box.ctl.wal_sync()
        return box.info.lsn
    end)
    node3:wait_for_vclock_of(node1)
    -- Node1 now receives the real acks carrying the new term. It immediately
    -- resigns. Its synchro queue still has the transactions, because it doesn't
    -- restore the replication from the other nodes and didn't get the new
    -- PROMOTE yet.
    node1:exec(function(timeout, lsn, term)
        box.error.injection.set('ERRINJ_RELAY_READ_ACK_DELAY', false)
        t.helpers.retrying({timeout = timeout}, function()
            local info = box.info
            t.assert_equals({
                queue_len = info.synchro.queue.len,
                lsn = info.lsn,
                state = info.election.state,
                is_term_newer = info.election.term > term,
            }, {
                queue_len = 2,
                lsn = lsn,
                state = 'follower',
                is_term_newer = true,
            })
        end)
    end, {TIMEOUT, lsn, term})
    --
    -- Everybody reconnects. Node 2's PROMOTE reaches node 1: the first
    -- transaction is confirmed by it, the second is rolled back.
    --
    node2:exec(function()
        box.error.injection.set('ERRINJ_APPLIER_WRITER_DELAY', false)
    end)
    node1:update_box_cfg{replication = cg.uris}
    node1:wait_for_vclock_of(node2)
    node3:wait_for_vclock_of(node2)
    for _, node in pairs(cg.nodes) do
        node:exec(function(node2_id)
            t.assert_equals(box.info.synchro.queue.owner, node2_id)
            t.assert_equals(box.info.synchro.queue.len, 0)
            t.assert_equals(box.space.s:select(), {{1}})
        end, {node2_id})
    end
    -- Confirmed by node 2's PROMOTE, not by an own CONFIRM. This means the
    -- node made no new WAL writes of its own origin. And that means its own
    -- LSN didn't change.
    node1:exec(function(lsn)
        t.assert_equals(box.info.lsn, lsn)
    end, {lsn})
end
