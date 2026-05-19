local replica_set = require('luatest.replica_set')
local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

--
-- A PROMOTE's term map is what its author knew about the other nodes' terms
-- when building it. A promotion made without knowing an already applied one
-- can't be applied on top of it - the two leaders were elected by
-- non-intersecting quorums and decided the fates of the same transactions
-- independently. Such a PROMOTE is rejected as a split brain.
--
-- A pending PROMOTE in that position is poisoned. It is normal - a leader can
-- write its PROMOTE and lose the leadership before the row reaches anybody.
-- The entry stays in its slot though, so its CONFIRM, if one ever comes, is
-- recognized and rejected instead of passing as a nop.
--
local function build_replica_set(cg, node_count, election_mode, quorum)
    cg.replica_set = replica_set:new{}
    local box_cfg = {
        replication = {},
        replication_timeout = 0.1,
        replication_synchro_quorum = quorum,
        replication_synchro_timeout = 60,
        election_mode = election_mode,
    }
    for i = 1, node_count do
        table.insert(box_cfg.replication,
                     server.build_listen_uri('node' .. i, cg.replica_set.id))
    end
    cg.nodes = {}
    for i = 1, node_count do
        cg.nodes[i] = cg.replica_set:build_and_add_server{
            alias = 'node' .. i,
            box_cfg = box_cfg,
        }
    end
    cg.replica_set:start()
    cg.replica_set:wait_for_fullmesh()
    for _, node in pairs(cg.nodes) do
        node:exec(function()
            rawset(_G, 'fiber', require('fiber'))
        end)
    end
    if election_mode == 'off' then
        return
    end
    -- The bootstrap leader is the Raft leader already. An active leader
    -- doesn't issue new PROMOTEs. The tests need a leaderless start.
    for _, node in pairs(cg.nodes) do
        node:exec(function()
            if box.info.election.state == 'leader' then
                box.ctl.demote()
            end
        end)
    end
    for _, node in pairs(cg.nodes) do
        node:exec(function()
            t.helpers.retrying({timeout = 60}, function()
                t.assert_equals(box.info.election.leader, 0)
            end)
        end)
    end
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

-- Start a promotion whose PROMOTE gets stuck in the WAL. The new term is
-- broadcast right away nonetheless.
local function promote_stuck_in_wal(node)
    node:exec(function()
        box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', 0)
        local f = _G.fiber.new(box.ctl.promote)
        f:set_joinable(true)
        rawset(_G, 'promote_fiber', f)
    end)
    node:play_wal_until_synchro_queue_is_busy()
end

-- The promotion gives up as soon as it sees a newer term, without waiting for
-- its PROMOTE to land. The WAL sync makes sure it did.
local function promote_release_wal(node)
    return node:exec(function()
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
        local ok = rawget(_G, 'promote_fiber'):join(60)
        box.ctl.wal_sync()
        return ok
    end)
end

-- A candidate needs the voters to have nothing it lacks. Sync the vclocks
-- before an election.
local function wait_sync(cg)
    for _, node in pairs(cg.nodes) do
        for _, other in pairs(cg.nodes) do
            node:wait_for_vclock_of(other)
        end
    end
end

local function wait_upstream_split_brain(node, upstream_id, message)
    node:exec(function(upstream_id, message)
        t.helpers.retrying({timeout = 60}, function()
            local upstream = box.info.replication[upstream_id].upstream
            t.assert_equals(upstream.status, 'stopped')
            t.assert_str_contains(upstream.message, 'Split-Brain')
            t.assert_str_contains(upstream.message, message)
        end)
    end, {upstream_id, message})
end

--
-- Without elections nothing guards a promotion. Two nodes promoting
-- concurrently is a fork right away, and both sides see it.
--
g.test_fork_without_elections = function(cg)
    build_replica_set(cg, 2, 'off', 1)
    local node1, node2 = cg.nodes[1], cg.nodes[2]
    local node1_id, node2_id = node1:get_instance_id(), node2:get_instance_id()

    promote_stuck_in_wal(node1)
    local term1 = node1:get_election_term()
    -- Node 2 promotes on top of that term. It has never seen the PROMOTE of
    -- node 1, so its own PROMOTE's term map doesn't mention node 1.
    node2:wait_for_election_term(term1)
    node2:exec(function(node1_id, term1)
        box.ctl.promote()
        t.assert_equals(box.info.synchro.queue.owner, box.info.id)
        t.assert_not_equals(box.info.synchro.term_map[node1_id], term1)
    end, {node1_id, term1})
    local term2 = node2:get_election_term()
    t.assert_lt(term1, term2)
    -- Node 1's PROMOTE gets written and applied. The promotion fails
    -- nonetheless - node 1 already knows about the newer term.
    local ok = promote_release_wal(node1)
    t.assert_not(ok)
    node1:exec(function(term1)
        t.assert_equals(box.info.synchro.queue.owner, box.info.id)
        t.assert_equals(box.info.synchro.term_map[box.info.id], term1)
    end, {term1})
    -- Node 1 sees a newer PROMOTE made without knowing its own applied one.
    -- Node 2 sees an older PROMOTE than the applied one. Both are forks.
    wait_upstream_split_brain(node1, node2_id,
                              'made without knowing the term')
    wait_upstream_split_brain(node2, node1_id,
                              'reverting the latest confirmed term')
    node1:exec(function(term1)
        t.assert_equals(box.info.synchro.queue.owner, box.info.id)
        t.assert_equals(box.info.synchro.term_map[box.info.id], term1)
    end, {term1})
    node2:exec(function(term2)
        t.assert_equals(box.info.synchro.queue.owner, box.info.id)
        t.assert_equals(box.info.synchro.term_map[box.info.id], term2)
    end, {term2})
end

--
-- With elections a fork needs a quorum which is not a majority. Then two
-- nodes can get elected and confirm their PROMOTEs without seeing each other.
-- The pending PROMOTEs of each other become poisoned, and their CONFIRMs are
-- rejected.
--
g.test_fork_with_elections = function(cg)
    build_replica_set(cg, 2, 'manual', 1)
    local node1, node2 = cg.nodes[1], cg.nodes[2]
    local node1_id, node2_id = node1:get_instance_id(), node2:get_instance_id()

    promote_stuck_in_wal(node1)
    local term1 = node1:get_election_term()
    node2:wait_for_election_term(term1)
    -- A full partition. Node 1 must not learn about the newer term, or it
    -- would give its promotion up. Even an ack from node 2 would tell it -
    -- the acks carry the sender's term.
    for _, node in pairs(cg.nodes) do
        node:exec(function()
            box.cfg{replication = {}}
        end)
    end
    node2:exec(function(node1_id, term1)
        box.ctl.promote()
        t.assert_equals(box.info.synchro.queue.owner, box.info.id)
        t.assert_not_equals(box.info.synchro.term_map[node1_id], term1)
    end, {node1_id, term1})
    local term2 = node2:get_election_term()
    t.assert_lt(term1, term2)
    -- Node 1 finishes its own promotion successfully - it is alone with the
    -- quorum of one.
    local ok = promote_release_wal(node1)
    t.assert(ok)
    node1:exec(function(term1)
        t.assert_equals(box.info.synchro.queue.owner, box.info.id)
        t.assert_equals(box.info.synchro.term_map[box.info.id], term1)
    end, {term1})
    for _, node in pairs(cg.nodes) do
        node:exec(function(uris)
            box.cfg{replication = uris}
        end, {node.box_cfg.replication})
    end
    -- Node 2 receives the PROMOTE of node 1. It is below the applied term, so
    -- it is poisoned right away, but it stays in its slot. The CONFIRM
    -- following it is rejected.
    wait_upstream_split_brain(node2, node1_id,
                              'reverting the latest confirmed term')
    node2:exec(function(node1_id, term1)
        local promote = box.info.synchro.nodes[node1_id].promote
        t.assert_equals(promote.term, term1)
        t.assert(promote.is_poisoned)
        t.assert_equals(box.info.synchro.queue.owner, box.info.id)
    end, {node1_id, term1})
    -- Node 1 receives the PROMOTE of node 2. It is above the applied term,
    -- but was made without knowing node 1's applied term. The CONFIRM
    -- following it is rejected too.
    wait_upstream_split_brain(node1, node2_id,
                              'made without knowing the term')
    node1:exec(function(node2_id, term2)
        local promote = box.info.synchro.nodes[node2_id].promote
        t.assert_equals(promote.term, term2)
        t.assert(promote.is_poisoned)
        t.assert_equals(box.info.synchro.queue.owner, box.info.id)
    end, {node2_id, term2})
    -- The poisoned entry is a part of the state. It survives a checkpoint,
    -- and the CONFIRM is rejected again when node 1 resends it.
    node2:exec(function()
        box.snapshot()
    end)
    node2:restart()
    wait_upstream_split_brain(node2, node1_id,
                              'reverting the latest confirmed term')
    node2:exec(function(node1_id, term1)
        local promote = box.info.synchro.nodes[node1_id].promote
        t.assert_equals(promote.term, term1)
        t.assert(promote.is_poisoned)
        t.assert_equals(box.info.synchro.queue.owner, box.info.id)
    end, {node1_id, term1})
end

--
-- A poisoned PROMOTE appears in a perfectly healthy replicaset when a leader
-- loses its leadership before its PROMOTE is seen by anybody. Nobody confirms
-- it then, and it stays harmless until its origin promotes again.
--
g.test_poison_without_fork = function(cg)
    build_replica_set(cg, 3, 'manual', 2)
    local node1, node2, node3 = cg.nodes[1], cg.nodes[2], cg.nodes[3]
    local node1_id = node1:get_instance_id()
    local node2_id = node2:get_instance_id()

    promote_stuck_in_wal(node1)
    local term1 = node1:get_election_term()
    node2:wait_for_election_term(term1)
    node2:exec(function()
        box.ctl.promote()
        t.assert_equals(box.info.synchro.queue.owner, box.info.id)
    end)
    local term2 = node2:get_election_term()
    t.assert_lt(term1, term2)
    -- Node 1 knows about the newer term already. Its PROMOTE is written, but
    -- it is not going to be confirmed. Nothing happens to it besides getting
    -- poisoned everywhere.
    local ok = promote_release_wal(node1)
    t.assert_not(ok)
    wait_sync(cg)
    for _, node in pairs(cg.nodes) do
        node:exec(function(node1_id, node2_id, term1)
            t.assert_equals(box.info.synchro.queue.owner, node2_id)
            local promote = box.info.synchro.nodes[node1_id].promote
            t.assert_equals(promote.term, term1)
            t.assert(promote.is_poisoned)
            for id in pairs(box.info.replication) do
                if id ~= box.info.id then
                    t.assert_equals(
                        box.info.replication[id].upstream.status, 'follow')
                end
            end
        end, {node1_id, node2_id, term1})
    end
    -- The replicaset works as usual.
    node2:exec(function()
        box.schema.space.create('s', {is_sync = true}):create_index('p')
        box.space.s:replace{1}
    end)
    wait_sync(cg)
    -- A new promotion doesn't chain from the poisoned entry and doesn't
    -- touch it - nothing knows about it to cover it.
    node3:exec(function()
        box.ctl.promote()
        t.assert_equals(box.info.synchro.queue.owner, box.info.id)
    end)
    wait_sync(cg)
    for _, node in pairs(cg.nodes) do
        node:exec(function(node1_id, term1)
            local promote = box.info.synchro.nodes[node1_id].promote
            t.assert_equals(promote.term, term1)
            t.assert(promote.is_poisoned)
        end, {node1_id, term1})
    end
    -- The origin's own new promotion supersedes its poisoned entry.
    node1:exec(function()
        box.ctl.promote()
        t.assert_equals(box.info.synchro.queue.owner, box.info.id)
    end)
    wait_sync(cg)
    for _, node in pairs(cg.nodes) do
        node:exec(function(node1_id)
            t.assert_equals(box.info.synchro.queue.owner, node1_id)
            t.assert_equals(box.info.synchro.nodes[node1_id].promote, nil)
            t.assert_equals(box.space.s:select(), {{1}})
        end, {node1_id})
    end
end
