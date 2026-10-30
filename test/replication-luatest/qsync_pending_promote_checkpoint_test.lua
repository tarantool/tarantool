local t = require('luatest')
local vraft = require('test.replication-luatest.lib.vraft_tools')

local g = t.group()

-- Every wait in the file. Nothing here is expected to take long; a wait
-- running out is a failure, not slowness.
local TIMEOUT = 10

--
-- A pending PROMOTE is a part of the limbo state. It has to survive a
-- checkpoint, or the CONFIRM applying it would find nothing to apply on the
-- recovered instance, or on a replica bootstrapped from the checkpoint, and
-- the promotion would silently never happen there.
--
-- The CONFIRM names the PROMOTE LSN, and the entry keeps it in its confirmed
-- vclock, in the component of its author. The checkpoint renumbers the row
-- headers, so the PROMOTE LSN travels in the row body. The pending PROMOTE
-- replacing its own author is the harder case: the author's component holds
-- the confirm boundary in the checkpoint's state row and the PROMOTE LSN,
-- above the boundary, in the pending row.
--
-- Several pending PROMOTEs can share a checkpoint - a poisoned one, kept to
-- refuse its CONFIRM, beside a live one. The rows go ascending by term, so the
-- state row isn't necessarily the first.
--
local function restart(node)
    node:restart()
    node:exec(function(timeout)
        rawset(_G, 'TIMEOUT', timeout)
    end, {TIMEOUT})
end

-- The error injections and the debug-only box.info.synchro introspection.
g.before_all(function()
    t.tarantool.skip_if_not_debug()
end)

local function build_replica_set(cg, node_count)
    vraft.build_replica_set(cg, {
        replication_timeout = 0.1,
        replication_synchro_quorum = 2,
        replication_synchro_timeout = TIMEOUT,
        election_mode = 'manual',
        -- A follower restarts while the leader has a pending PROMOTE.
        -- Losing a follower must not make the leader step down.
        election_fencing_mode = 'off',
    }, {node_count = node_count, glob_timeout = TIMEOUT})
end

g.after_each(function(cg)
    for _, node in pairs(cg.nodes) do
        pcall(node.exec, node, function()
            box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', -1)
            box.error.injection.set('ERRINJ_WAL_DELAY', false)
        end)
    end
    cg.replica_set:drop()
end)

-- The node becomes the queue owner and steps off the Raft leadership keeping
-- the queue - with the elections on the demote writes nothing to the limbo.
-- The next PROMOTE replaces this node. The other nodes get its rows, so they
-- can vote for the next candidate.
local function promote_and_step_off(cg, node)
    node:exec(function()
        box.ctl.promote()
        t.assert_equals(box.info.synchro.queue.owner, box.info.id)
        box.ctl.demote()
        t.assert_equals(box.info.synchro.queue.owner, box.info.id)
    end)
    for _, other in pairs(cg.nodes) do
        other:wait_for_vclock_of(node)
    end
end

-- The node gets elected and writes its PROMOTE, but its CONFIRM is blocked.
-- The PROMOTE stays pending everywhere. Return its term and PROMOTE LSN.
local function promote_start_pending(cg, node, fiber_name)
    vraft.promote_start_blocked_at_confirm(node, fiber_name)
    local term = node:get_election_term()
    local promote_lsn = node:exec(function()
        return box.info.synchro.own_promote.lsn
    end)
    for _, other in pairs(cg.nodes) do
        other:wait_for_vclock_of(node)
    end
    return term, promote_lsn
end

-- The pending PROMOTE of the author is there, intact, with its PROMOTE LSN in
-- the author's component of its confirmed vclock - that is what the CONFIRM
-- is going to match.
local function assert_pending_promote(node, author_id, term, promote_lsn)
    node:exec(function(author_id, term, promote_lsn)
        local promote = box.info.synchro.nodes[author_id].promote
        t.assert_equals(promote.term, term)
        t.assert_not(promote.is_poisoned)
        t.assert_equals(promote.confirmed_vclock[author_id], promote_lsn)
        t.assert_lt(box.info.synchro.queue.term, term)
    end, {author_id, term, promote_lsn})
end

-- The loser's PROMOTE gets poisoned: stuck in the WAL while the winner
-- promotes on top of its term. Released, it lands below the applied term and
-- is poisoned everywhere, the winner owns the queue. Return the poisoned
-- PROMOTE's term.
local function poison_promote_of(cg, loser, winner, fiber_name)
    vraft.promote_start_wal_blocked(loser, fiber_name)
    local term = loser:get_election_term()
    winner:wait_for_election_term(term)
    winner:exec(function()
        box.ctl.promote()
        t.assert_equals(box.info.synchro.queue.owner, box.info.id)
    end)
    local ok, err = vraft.promote_join_at_wal_release(loser, fiber_name)
    t.assert_not(ok)
    t.assert_equals(err.code, box.error.INTERFERING_ELECTIONS)
    for _, other in pairs(cg.nodes) do
        other:wait_for_vclock_of(loser)
    end
    return term
end

local function assert_poisoned_promote(node, author_id, term)
    node:exec(function(author_id, term)
        local promote = box.info.synchro.nodes[author_id].promote
        t.assert_equals(promote.term, term)
        t.assert(promote.is_poisoned)
    end, {author_id, term})
end

local function wait_promote_applied(node, author_id, term)
    node:exec(function(author_id, term)
        t.helpers.retrying({timeout = _G.TIMEOUT}, function()
            t.assert_equals(box.info.synchro.queue.term, term)
        end)
        t.assert_equals(box.info.synchro.queue.owner, author_id)
        t.assert_equals(box.info.synchro.nodes[author_id].promote, nil)
    end, {author_id, term})
end

-- Nobody is writable while a promotion is in progress, so a new replica can't
-- get registered. An anonymous one needs no registration, and gets the limbo
-- state like any other. It joins from the given upstream alone: a join syncs
-- the upstream's WAL first, so the promoting node with its WAL blocked can't
-- serve one.
local function join_anon_replica(cg, upstream_uri)
    local node = cg.replica_set:build_and_add_server{
        alias = 'anon',
        box_cfg = {
            replication = {upstream_uri},
            replication_timeout = 0.1,
            replication_anon = true,
            read_only = true,
        },
    }
    node:start()
    node:exec(function(timeout)
        rawset(_G, 'TIMEOUT', timeout)
    end, {TIMEOUT})
    return node
end

-- Node 2 owns the queue, the pending PROMOTE of node 1 replaces node 2.
g.test_pending_survives_restart = function(cg)
    build_replica_set(cg, 2)
    local node1, node2 = cg.nodes[1], cg.nodes[2]
    local node1_id = node1:get_instance_id()
    promote_and_step_off(cg, node2)
    local term, promote_lsn = promote_start_pending(cg, node1, 'promote1')
    assert_pending_promote(node2, node1_id, term, promote_lsn)
    node2:exec(function()
        box.snapshot()
    end)
    restart(node2)
    assert_pending_promote(node2, node1_id, term, promote_lsn)
    t.assert(vraft.promote_join_at_wal_release(node1, 'promote1'))
    wait_promote_applied(node2, node1_id, term)
end

-- Node 1 owns the queue, the pending PROMOTE of node 1 replaces node 1 itself.
g.test_pending_replacing_its_author_survives_restart = function(cg)
    build_replica_set(cg, 2)
    local node1, node2 = cg.nodes[1], cg.nodes[2]
    local node1_id = node1:get_instance_id()
    promote_and_step_off(cg, node1)
    local term, promote_lsn = promote_start_pending(cg, node1, 'promote1')
    assert_pending_promote(node2, node1_id, term, promote_lsn)
    node2:exec(function()
        box.snapshot()
    end)
    restart(node2)
    assert_pending_promote(node2, node1_id, term, promote_lsn)
    t.assert(vraft.promote_join_at_wal_release(node1, 'promote1'))
    wait_promote_applied(node2, node1_id, term)
end

-- Node 2 owns the queue, the pending PROMOTE of node 1 replaces node 2. The
-- new replica joins from node 2.
g.test_pending_survives_join = function(cg)
    build_replica_set(cg, 2)
    local node1, node2 = cg.nodes[1], cg.nodes[2]
    local node1_id = node1:get_instance_id()
    promote_and_step_off(cg, node2)
    local term, promote_lsn = promote_start_pending(cg, node1, 'promote1')
    local anon = join_anon_replica(cg, cg.uris[2])
    assert_pending_promote(anon, node1_id, term, promote_lsn)
    t.assert(vraft.promote_join_at_wal_release(node1, 'promote1'))
    wait_promote_applied(anon, node1_id, term)
end

-- Node 1 owns the queue, the pending PROMOTE of node 1 replaces node 1 itself.
-- The new replica joins from node 2.
g.test_pending_replacing_its_author_survives_join = function(cg)
    build_replica_set(cg, 2)
    local node1 = cg.nodes[1]
    local node1_id = node1:get_instance_id()
    promote_and_step_off(cg, node1)
    local term, promote_lsn = promote_start_pending(cg, node1, 'promote1')
    local anon = join_anon_replica(cg, cg.uris[2])
    assert_pending_promote(anon, node1_id, term, promote_lsn)
    t.assert(vraft.promote_join_at_wal_release(node1, 'promote1'))
    wait_promote_applied(anon, node1_id, term)
end

-- Node 2 owns the queue. The poisoned PROMOTE of node 1, below the queue's
-- term, and the pending PROMOTE of node 3 replacing node 2, above it, share
-- the checkpoint with the state row between them.
g.test_two_pending_survive_restart = function(cg)
    build_replica_set(cg, 3)
    local node1, node2, node3 = unpack(cg.nodes)
    local node1_id, node3_id = node1:get_instance_id(), node3:get_instance_id()
    local term1 = poison_promote_of(cg, node1, node2, 'promote1')
    local term3, promote3_lsn = promote_start_pending(cg, node3, 'promote3')
    assert_poisoned_promote(node2, node1_id, term1)
    assert_pending_promote(node2, node3_id, term3, promote3_lsn)
    node2:exec(function()
        t.assert_equals(box.info.synchro.queue.owner, box.info.id)
        box.snapshot()
    end)
    restart(node2)
    node2:exec(function()
        t.assert_equals(box.info.synchro.queue.owner, box.info.id)
    end)
    assert_poisoned_promote(node2, node1_id, term1)
    assert_pending_promote(node2, node3_id, term3, promote3_lsn)
    -- The live one gets confirmed, the poisoned one stays.
    t.assert(vraft.promote_join_at_wal_release(node3, 'promote3'))
    wait_promote_applied(node2, node3_id, term3)
    assert_poisoned_promote(node2, node1_id, term1)
end

-- Node 2 owns the queue, the poisoned PROMOTE of node 1 and the pending
-- PROMOTE of node 3 replacing node 2 are around. The new replica joins from
-- node 2.
g.test_two_pending_survive_join = function(cg)
    build_replica_set(cg, 3)
    local node1, node2, node3 = unpack(cg.nodes)
    local node1_id, node3_id = node1:get_instance_id(), node3:get_instance_id()
    local node2_id = node2:get_instance_id()
    local term1 = poison_promote_of(cg, node1, node2, 'promote1')
    local term3, promote3_lsn = promote_start_pending(cg, node3, 'promote3')
    local anon = join_anon_replica(cg, cg.uris[2])
    anon:exec(function(node2_id)
        t.assert_equals(box.info.synchro.queue.owner, node2_id)
    end, {node2_id})
    assert_poisoned_promote(anon, node1_id, term1)
    assert_pending_promote(anon, node3_id, term3, promote3_lsn)
    -- The live one gets confirmed, the poisoned one stays.
    t.assert(vraft.promote_join_at_wal_release(node3, 'promote3'))
    wait_promote_applied(anon, node3_id, term3)
    assert_poisoned_promote(anon, node1_id, term1)
end
