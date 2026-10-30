local t = require('luatest')
local vraft = require('test.replication-luatest.lib.vraft_tools')

local g = t.group()

-- Every wait in the file. Nothing here is expected to take long; a wait
-- running out is a failure, not slowness.
local TIMEOUT = 10

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
    vraft.build_replica_set(cg, {
        replication_timeout = 0.1,
        replication_synchro_quorum = quorum,
        replication_synchro_timeout = TIMEOUT,
        election_mode = election_mode,
    }, {node_count = node_count, glob_timeout = TIMEOUT})
end

-- A restart loses the globals of the server.
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

g.after_each(function(cg)
    for _, node in pairs(cg.nodes) do
        pcall(node.exec, node, function()
            box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', -1)
            box.error.injection.set('ERRINJ_WAL_DELAY', false)
            box.error.injection.set('ERRINJ_APPLIER_WRITER_DELAY', false)
        end)
    end
    cg.replica_set:drop()
end)

--
-- Without elections nothing guards a promotion. Two nodes promoting
-- concurrently is a fork right away, and both sides see it.
--
g.test_fork_without_elections = function(cg)
    build_replica_set(cg, 2, 'off', 1)
    local node1, node2 = cg.nodes[1], cg.nodes[2]
    local node1_id, node2_id = node1:get_instance_id(), node2:get_instance_id()

    vraft.promote_start_wal_blocked(node1, 'promote1')
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
    local ok, err = vraft.promote_join_at_wal_release(node1, 'promote1')
    t.assert_not(ok)
    t.assert_equals(err.code, box.error.INTERFERING_ELECTIONS)
    node1:exec(function(term1)
        t.assert_equals(box.info.synchro.queue.owner, box.info.id)
        t.assert_equals(box.info.synchro.term_map[box.info.id], term1)
    end, {term1})
    -- Node 1 sees a newer PROMOTE made without knowing its own applied one.
    -- Node 2 sees an older PROMOTE than the applied one. Both are forks.
    vraft.wait_upstream_split_brain(node1, node2_id,
                              'made without knowing the term')
    vraft.wait_upstream_split_brain(node2, node1_id,
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

    vraft.promote_start_wal_blocked(node1, 'promote1')
    local term1 = node1:get_election_term()
    node2:wait_for_election_term(term1)
    -- A full partition. Node 1 must not learn about the newer term, or it
    -- would give its promotion up. Even an ack from node 2 would tell it -
    -- the acks carry the sender's term.
    for _, node in pairs(cg.nodes) do
        node:update_box_cfg{replication = {}}
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
    t.assert(vraft.promote_join_at_wal_release(node1, 'promote1'))
    node1:exec(function(term1)
        t.assert_equals(box.info.synchro.queue.owner, box.info.id)
        t.assert_equals(box.info.synchro.term_map[box.info.id], term1)
    end, {term1})
    for _, node in pairs(cg.nodes) do
        node:update_box_cfg{replication = cg.uris}
    end
    -- Node 2 receives the PROMOTE of node 1. It is below the applied term, so
    -- it is poisoned right away, but it stays in its slot. The CONFIRM
    -- following it is rejected.
    vraft.wait_upstream_split_brain(node2, node1_id,
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
    vraft.wait_upstream_split_brain(node1, node2_id,
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
    restart(node2)
    vraft.wait_upstream_split_brain(node2, node1_id,
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

    vraft.promote_start_wal_blocked(node1, 'promote1')
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
    local ok, err = vraft.promote_join_at_wal_release(node1, 'promote1')
    t.assert_not(ok)
    t.assert_equals(err.code, box.error.INTERFERING_ELECTIONS)
    vraft.wait_sync(cg)
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
    vraft.wait_sync(cg)
    -- A new promotion doesn't chain from the poisoned entry and doesn't
    -- touch it - nothing knows about it to cover it.
    node3:exec(function()
        box.ctl.promote()
        t.assert_equals(box.info.synchro.queue.owner, box.info.id)
    end)
    vraft.wait_sync(cg)
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
    vraft.wait_sync(cg)
    for _, node in pairs(cg.nodes) do
        node:exec(function(node1_id)
            t.assert_equals(box.info.synchro.queue.owner, node1_id)
            t.assert_equals(box.info.synchro.nodes[node1_id].promote, nil)
            t.assert_equals(box.space.s:select(), {{1}})
        end, {node1_id})
    end
end

--
-- The third poison: a pending PROMOTE whose confirmed vclock is behind the
-- node's own. It was written by a leader which had not seen something this node
-- has confirmed, so its CONFIRM could only ever be a fork here. A new leader
-- must not chain over such an entry: the chain would inherit the deficient
-- vclock and could never get confirmed on this node either.
--
g.test_chain_skips_entry_confirming_less = function(cg)
    build_replica_set(cg, 3, 'manual', 1)
    local node1, node2, node3 = cg.nodes[1], cg.nodes[2], cg.nodes[3]
    local node1_id, node2_id, node3_id = node1:get_instance_id(),
        node2:get_instance_id(), node3:get_instance_id()

    node1:exec(function()
        box.ctl.promote()
        t.assert_equals(box.info.synchro.queue.owner, box.info.id)
        box.schema.space.create('s', {is_sync = true}):create_index('p')
    end)
    vraft.wait_sync(cg)
    -- Node 3 stops receiving from anybody.
    node3:update_box_cfg{replication = {}}
    -- Node 1 commits a transaction alone, with the quorum of one. Node 2 gets
    -- it and its CONFIRM, node 3 doesn't.
    node1:exec(function()
        box.space.s:replace{1}
    end)
    node2:wait_for_vclock_of(node1)
    local term2 = node2:exec(function(node1_id)
        t.assert_equals(box.space.s:select(), {{1}})
        t.assert_equals(box.info.synchro.queue.owner, node1_id)
        return box.info.election.term
    end, {node1_id})
    -- Node 3 promotes with the CONFIRM stuck. Its PROMOTE reaches node 2
    -- pending, confirming node 1 up to what node 3 has seen - less than node 2
    -- has already confirmed from node 1.
    vraft.promote_start_blocked_at_confirm(node3, 'promote3')
    local term3 = node3:get_election_term()
    t.assert_lt(term2, term3)
    node2:wait_for_election_term(term3)
    node2:exec(function(node1_id, node3_id, term3)
        t.helpers.retrying({timeout = _G.TIMEOUT}, function()
            t.assert_not_equals(box.info.synchro.nodes[node3_id], nil)
            t.assert_not_equals(box.info.synchro.nodes[node3_id].promote, nil)
        end)
        local promote = box.info.synchro.nodes[node3_id].promote
        t.assert_equals(promote.term, term3)
        -- The term is fine, the term map is fine, the confirmed vclock is
        -- behind: the only poison is the third one.
        t.assert_lt(box.info.synchro.queue.term, promote.term)
        for id, term in pairs(box.info.synchro.term_map) do
            t.assert_le(term, promote.term_map[id])
        end
        t.assert_lt(promote.confirmed_vclock[node1_id],
                    box.info.synchro.confirmed_vclock[node1_id])
        t.assert(promote.is_poisoned)
    end, {node1_id, node3_id, term3})
    -- Node 3 must not learn about a newer term now, or it would give its
    -- promotion up. Even an ack would tell it - the acks carry the sender's
    -- term. Nobody receives from node 3 anymore: a full isolation.
    for _, node in pairs({node1, node2}) do
        node:update_box_cfg{replication = {cg.uris[1], cg.uris[2]}}
    end
    -- Node 2 promotes. It doesn't chain over the poisoned entry: a fresh
    -- PROMOTE, confirmed alone, and the transaction stays committed.
    node2:exec(function(node3_id, term3)
        box.ctl.promote()
        t.assert_equals(box.info.synchro.queue.owner, box.info.id)
        t.assert_equals(box.space.s:select(), {{1}})
        t.assert_not_equals(box.info.synchro.term_map[node3_id], term3)
        local promote = box.info.synchro.nodes[node3_id].promote
        t.assert_equals(promote.term, term3)
        t.assert(promote.is_poisoned)
        t.assert_lt(term3, box.info.election.term)
    end, {node3_id, term3})
    node1:wait_for_vclock_of(node2)
    node1:exec(function(node2_id)
        t.assert_equals(box.info.synchro.queue.owner, node2_id)
        t.assert_equals(box.space.s:select(), {{1}})
    end, {node2_id})
    -- Node 3 finishes its promotion on its side. The two leaderships are a
    -- fork, and both sides see it: node 3's CONFIRM confirms less than node 2
    -- has confirmed, and by now its term is below node 2's applied one too -
    -- the first reason the filter meets is the one it names; node 3 gets the
    -- old owner's CONFIRM of the transaction its own promotion rolled back,
    -- before anything of node 2's.
    t.assert(vraft.promote_join_at_wal_release(node3, 'promote3'))
    node3:exec(function()
        t.assert_equals(box.info.synchro.queue.owner, box.info.id)
        t.assert_equals(box.space.s:select(), {})
    end)
    for _, node in pairs(cg.nodes) do
        node:update_box_cfg{replication = cg.uris}
    end
    vraft.wait_upstream_split_brain(node2, node3_id,
                              'reverting the latest confirmed term')
    vraft.wait_upstream_split_brain(node3, node1_id, 'foreign synchro queue owner')
    vraft.wait_upstream_split_brain(node3, node2_id, 'foreign synchro queue owner')
end

--
-- The CONFIRM of a PROMOTE confirming less than the receiver has confirmed is
-- refused for exactly that reason - when nothing else is wrong with the entry:
-- its term is above the receiver's, its term map covers the receiver's applied
-- terms. The same setup as above, but nobody promotes on the other side, so no
-- newer term gets in the way of the vclock check.
--
g.test_confirm_of_entry_confirming_less_is_refused = function(cg)
    build_replica_set(cg, 3, 'manual', 1)
    local node1, node2, node3 = cg.nodes[1], cg.nodes[2], cg.nodes[3]
    local node1_id, node3_id = node1:get_instance_id(), node3:get_instance_id()

    node1:exec(function()
        box.ctl.promote()
        t.assert_equals(box.info.synchro.queue.owner, box.info.id)
        box.schema.space.create('s', {is_sync = true}):create_index('p')
    end)
    vraft.wait_sync(cg)
    node3:update_box_cfg{replication = {}}
    node1:exec(function()
        box.space.s:replace{1}
    end)
    node2:wait_for_vclock_of(node1)
    -- Node 3 promotes without having seen the transaction. Its PROMOTE reaches
    -- the others and is poisoned there by the vclock alone.
    vraft.promote_start_blocked_at_confirm(node3, 'promote3')
    local term3 = node3:get_election_term()
    for _, node in pairs({node1, node2}) do
        node:wait_for_election_term(term3)
        node:exec(function(node1_id, node3_id, term3)
            t.helpers.retrying({timeout = _G.TIMEOUT}, function()
                t.assert_not_equals(box.info.synchro.nodes[node3_id], nil)
                t.assert_not_equals(box.info.synchro.nodes[node3_id].promote,
                                    nil)
            end)
            local promote = box.info.synchro.nodes[node3_id].promote
            t.assert_equals(promote.term, term3)
            t.assert_lt(box.info.synchro.queue.term, promote.term)
            for id, term in pairs(box.info.synchro.term_map) do
                t.assert_le(term, promote.term_map[id])
            end
            t.assert_lt(promote.confirmed_vclock[node1_id],
                        box.info.synchro.confirmed_vclock[node1_id])
            t.assert(promote.is_poisoned)
        end, {node1_id, node3_id, term3})
    end
    -- Node 3 confirms alone. Nobody has a newer term, so its own promotion
    -- goes through on its side; on the other side the CONFIRM is refused for
    -- confirming less than is confirmed there.
    t.assert(vraft.promote_join_at_wal_release(node3, 'promote3'))
    node3:exec(function()
        t.assert_equals(box.info.synchro.queue.owner, box.info.id)
    end)
    for _, node in pairs({node1, node2}) do
        vraft.wait_upstream_split_brain(node, node3_id, 'already processed range')
        node:exec(function(node1_id, node3_id, term3)
            t.assert_equals(box.info.synchro.queue.owner, node1_id)
            t.assert_equals(box.space.s:select(), {{1}})
            local promote = box.info.synchro.nodes[node3_id].promote
            t.assert_equals(promote.term, term3)
            t.assert(promote.is_poisoned)
        end, {node1_id, node3_id, term3})
    end
end

--
-- The leader's own pending PROMOTE gets poisoned while its leadership is
-- intact. The leader is waiting for an ack, and meanwhile the old owner
-- commits a transaction with a quorum not intersecting the leader's. The
-- transaction and its CONFIRM reach the leader: the own PROMOTE confirms less
-- from the old owner than the leader has confirmed now. The promotion is given
-- up with an error, nobody loses the transaction, and the late ack finds no
-- promotion to serve.
--
g.test_own_pending_poisoned_by_late_confirm = function(cg)
    vraft.build_replica_set(cg, {
        replication_timeout = 0.1,
        replication_synchro_quorum = 2,
        replication_synchro_timeout = TIMEOUT,
        election_mode = 'manual',
        -- The old owner keeps leading with no upstreams at all.
        election_fencing_mode = 'off',
    }, {node_count = 4, glob_timeout = TIMEOUT})
    local node1, node2, node3, node4 = unpack(cg.nodes)
    local node1_id, node2_id = node1:get_instance_id(), node2:get_instance_id()

    node2:exec(function()
        box.ctl.promote()
        t.assert_equals(box.info.synchro.queue.owner, box.info.id)
        box.schema.space.create('s', {is_sync = true}):create_index('p')
        box.space.s:replace{1}
    end)
    vraft.wait_sync(cg)
    -- The acks flow against the replication, carrying the sender's term. Node
    -- 2 listens to nobody, and the only one listening to it is node 3, which
    -- itself listens to node 2 alone - no news of a new term reaches them.
    -- Node 1 and node 4 listen to each other only: node 4 votes for node 1 and
    -- relays nothing of node 2 to it.
    node2:update_box_cfg{replication = {}}
    node3:update_box_cfg{replication = {cg.uris[2]}}
    node4:update_box_cfg{replication = {cg.uris[1]}}
    node1:update_box_cfg{replication = {cg.uris[4]}}
    -- Node 4 votes, but its acks are held: node 1 wins the term, writes its
    -- PROMOTE and waits for the quorum which never comes.
    node4:exec(function()
        box.error.injection.set('ERRINJ_APPLIER_WRITER_DELAY', true)
    end)
    vraft.promote_start(node1, 'promote1')
    node1:exec(function()
        t.helpers.retrying({timeout = _G.TIMEOUT}, function()
            t.assert_equals(box.info.election.state, 'leader')
            t.assert_not_equals(box.info.synchro.own_promote, nil)
        end)
    end)
    local term1 = node1:get_election_term()
    -- The old owner commits with node 3 alone. Nobody else knows.
    node2:exec(function()
        box.space.s:replace{2}
        t.assert_equals(box.space.s:select(), {{1}, {2}})
    end)
    node3:wait_for_vclock_of(node2)
    -- Node 1 starts listening to node 3 too. The transaction and its CONFIRM
    -- arrive and apply - the queue is still node 2's on node 1, its own
    -- PROMOTE is pending. The own PROMOTE now confirms less from node 2 than
    -- node 1 has confirmed: poisoned, with node 1 still the leader of its
    -- term. The promotion gives up.
    --
    -- From now on node 1's acks tell node 3 the new term, and node 3's acks
    -- tell node 2. Node 2 steps down then - after its commit, which is all
    -- that matters here.
    node1:update_box_cfg{replication = {cg.uris[4], cg.uris[3]}}
    local ok, err = vraft.promote_join(node1, 'promote1')
    t.assert_not(ok)
    t.assert_equals(err.code, box.error.SPLIT_BRAIN)
    node1:exec(function(node2_id, term1)
        t.assert_equals(box.info.election.state, 'leader')
        t.assert_equals(box.info.election.term, term1)
        t.assert_equals(box.info.synchro.queue.owner, node2_id)
        t.assert_equals(box.space.s:select(), {{1}, {2}})
        t.assert_equals(box.info.synchro.own_promote, nil)
        local promote = box.info.synchro.nodes[box.info.id].promote
        t.assert_equals(promote.term, term1)
        t.assert(promote.is_poisoned)
        t.assert_lt(promote.confirmed_vclock[node2_id],
                    box.info.synchro.confirmed_vclock[node2_id])
    end, {node2_id, term1})
    -- The late ack of node 4 arrives to a promotion given up already. Nothing
    -- to serve.
    node4:exec(function()
        box.error.injection.set('ERRINJ_APPLIER_WRITER_DELAY', false)
    end)
    node1:exec(function(term1)
        t.helpers.retrying({timeout = _G.TIMEOUT}, function()
            t.assert_equals(box.info.election.term, term1)
            t.assert_equals(box.info.synchro.own_promote, nil)
            t.assert(box.info.synchro.nodes[box.info.id].promote.is_poisoned)
        end)
    end, {term1})
    -- Everybody reconnects. Node 2 has stepped down, but still owns the
    -- queue, and promotes again, skipping the poisoned entry of node 1. The
    -- entry stays in its slot everywhere: nothing covers it, and its CONFIRM
    -- never comes.
    for _, node in pairs(cg.nodes) do
        node:update_box_cfg{replication = cg.uris}
    end
    vraft.wait_sync(cg)
    node2:exec(function(node1_id, term1)
        box.ctl.promote()
        t.assert_equals(box.info.synchro.queue.owner, box.info.id)
        t.assert_lt(term1, box.info.synchro.queue.term)
        t.assert(box.info.synchro.nodes[node1_id].promote.is_poisoned)
        box.space.s:replace{3}
    end, {node1_id, term1})
    vraft.wait_sync(cg)
    for _, node in pairs(cg.nodes) do
        node:exec(function(node1_id, node2_id, term1)
            t.assert_equals(box.info.synchro.queue.owner, node2_id)
            t.assert_equals(box.space.s:select(), {{1}, {2}, {3}})
            local promote = box.info.synchro.nodes[node1_id].promote
            t.assert_equals(promote.term, term1)
            t.assert(promote.is_poisoned)
        end, {node1_id, node2_id, term1})
        vraft.assert_all_upstreams_follow(node)
    end
end
