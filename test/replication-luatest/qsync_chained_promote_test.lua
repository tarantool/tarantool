local t = require('luatest')
local replica_link = require('test.replication-luatest.lib.replica_link')
local vraft = require('test.replication-luatest.lib.vraft_tools')

local g = t.group()

-- Every wait in the file. Nothing here is expected to take long; a wait
-- running out is a failure, not slowness.
local TIMEOUT = 10

--
-- A newly elected leader holding somebody's pending PROMOTE chains its own on
-- top: the new PROMOTE inherits the previous owner, the confirm boundary and
-- the confirmed state of the pending one, and the pending one's PROMOTE LSN
-- with them. Whichever of the two CONFIRMs applies first on a node, and
-- whether the other ever does, the node ends in the same state.
--
local function build_replica_set(cg, node_count, quorum)
    vraft.build_replica_set(cg, {
        replication_timeout = 0.1,
        replication_synchro_quorum = quorum,
        replication_synchro_timeout = TIMEOUT,
        election_mode = 'manual',
    }, {node_count = node_count, glob_timeout = TIMEOUT})
end

-- The same, unwired: the test builds the topology itself, with the links in
-- between. The fencing is off: the old leader keeps leading with its only
-- upstream silent.
local function build_unwired_replica_set(cg, node_count, quorum)
    vraft.build_unwired_replica_set(cg, {
        replication_timeout = 0.1,
        replication_synchro_quorum = quorum,
        replication_synchro_timeout = TIMEOUT,
        election_mode = 'manual',
        election_fencing_mode = 'off',
    }, {node_count = node_count, glob_timeout = TIMEOUT})
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
        end)
    end
    cg.replica_set:drop()
    if cg.links ~= nil then
        for _, link in pairs(cg.links) do
            link:stop()
        end
    end
end)

local has_type = replica_link.has_type
local take_out = replica_link.take_out
local is_raft = replica_link.is_type(box.iproto.type.RAFT)

--
-- The base of a chain is slow with its CONFIRM; the chain gets confirmed
-- first. The base's late CONFIRM finds its PROMOTE LSN confirmed already - the
-- chain carried it in its confirmed vclock - and is a nop everywhere: not a
-- split brain, not a second application. A healthy majority cluster.
--
g.test_late_confirm_of_chain_base_is_nop = function(cg)
    build_replica_set(cg, 3, 2)
    local node1, node2, node3 = cg.nodes[1], cg.nodes[2], cg.nodes[3]
    local node2_id, node3_id = node2:get_instance_id(), node3:get_instance_id()

    node1:exec(function()
        box.ctl.promote()
        t.assert_equals(box.info.synchro.queue.owner, box.info.id)
        box.schema.space.create('s', {is_sync = true}):create_index('p')
        box.space.s:replace{1}
    end)
    vraft.wait_sync(cg)
    -- Node 2's PROMOTE gathers its quorum and replicates everywhere, the
    -- CONFIRM is stuck.
    vraft.promote_start_blocked_at_confirm(node2, 'promote2')
    local term2 = node2:get_election_term()
    local promote2_lsn = node2:exec(function()
        return box.info.synchro.own_promote.lsn
    end)
    node1:wait_for_vclock_of(node2)
    node3:wait_for_vclock_of(node2)
    -- Node 3 chains over the pending PROMOTE of node 2 and confirms. The chain
    -- covers node 2's promotion: its term is applied, its slot is dropped,
    -- node 2 gives its own promotion up.
    node3:exec(function(node2_id, term2)
        t.assert_equals(box.info.synchro.nodes[node2_id].promote.term, term2)
        box.ctl.promote()
        t.assert_equals(box.info.synchro.queue.owner, box.info.id)
        t.assert_equals(box.info.synchro.term_map[node2_id], term2)
        t.assert_equals(box.info.synchro.nodes[node2_id].promote, nil)
    end, {node2_id, term2})
    local term3 = node3:get_election_term()
    t.assert_lt(term2, term3)
    node1:wait_for_vclock_of(node3)
    node1:exec(function(node2_id, node3_id)
        t.assert_equals(box.info.synchro.queue.owner, node3_id)
        t.assert_equals(box.info.synchro.nodes[node2_id].promote, nil)
    end, {node2_id, node3_id})
    -- Node 2's WAL is released. Node 2 has seen node 3's term in the Raft
    -- messages meanwhile and gave its promotion up, but the CONFIRM is in the
    -- WAL queue already and lands. It is first in node 2's own WAL, so node 2
    -- applies its own promotion, and node 3's chain over it right after. On
    -- the other nodes the CONFIRM is late: its PROMOTE LSN is confirmed
    -- already by the chain, and it is a nop.
    local ok, err = vraft.promote_join_at_wal_release(node2, 'promote2')
    t.assert_not(ok)
    t.assert_equals(err.code, box.error.INTERFERING_ELECTIONS)
    vraft.wait_sync(cg)
    for _, node in pairs(cg.nodes) do
        node:exec(function(node2_id, node3_id, term2, term3, promote2_lsn)
            t.assert_equals(box.info.synchro.queue.owner, node3_id)
            t.assert_equals(box.info.synchro.queue.term, term3)
            t.assert_equals(box.info.synchro.term_map[node2_id], term2)
            t.assert_le(promote2_lsn,
                        box.info.synchro.confirmed_vclock[node2_id])
            t.assert_equals(box.info.synchro.nodes[node2_id].promote, nil)
            t.assert_equals(box.space.s:select(), {{1}})
        end, {node2_id, node3_id, term2, term3, promote2_lsn})
        vraft.assert_all_upstreams_follow(node)
    end
end

--
-- The base of a chain replaces its own author: the owner re-promotes, so the
-- previous owner and the base's origin are one node, and one component of the
-- confirmed vclock carries both roles - the confirm boundary and, above it,
-- the base's PROMOTE LSN. The chain must keep the PROMOTE LSN: a chain
-- confirming less than its base is never confirmable, and the promotion would
-- hang.
--
g.test_chain_over_base_replacing_its_author = function(cg)
    build_replica_set(cg, 3, 2)
    local node1, node2, node3 = cg.nodes[1], cg.nodes[2], cg.nodes[3]
    local node1_id, node3_id = node1:get_instance_id(), node3:get_instance_id()

    node1:exec(function()
        box.ctl.promote()
        t.assert_equals(box.info.synchro.queue.owner, box.info.id)
        box.schema.space.create('s', {is_sync = true}):create_index('p')
        box.space.s:replace{1}
    end)
    vraft.wait_sync(cg)
    -- An active leader doesn't promote again. Node 1 steps off the Raft
    -- leadership keeping the queue - the demote writes nothing to the limbo
    -- with the elections on - and promotes anew: a PROMOTE replacing node 1
    -- by node 1. It gathers its quorum and replicates everywhere, the CONFIRM
    -- is stuck.
    node1:exec(function()
        box.ctl.demote()
        t.assert_equals(box.info.synchro.queue.owner, box.info.id)
    end)
    vraft.promote_start_blocked_at_confirm(node1, 'promote1')
    local term2 = node1:get_election_term()
    local promote1_lsn = node1:exec(function()
        return box.info.synchro.own_promote.lsn
    end)
    node2:wait_for_vclock_of(node1)
    node3:wait_for_vclock_of(node1)
    -- Node 3 chains over the pending PROMOTE of node 1 and confirms. The
    -- confirmation is the point: the chain carries node 1's PROMOTE LSN and
    -- is confirmable.
    node3:exec(function(node1_id, term2)
        t.assert_equals(box.info.synchro.queue.owner, node1_id)
        t.assert_equals(box.info.synchro.nodes[node1_id].promote.term, term2)
        box.ctl.promote()
        t.assert_equals(box.info.synchro.queue.owner, box.info.id)
        t.assert_equals(box.info.synchro.term_map[node1_id], term2)
        t.assert_equals(box.info.synchro.nodes[node1_id].promote, nil)
    end, {node1_id, term2})
    local term3 = node3:get_election_term()
    t.assert_lt(term2, term3)
    node2:wait_for_vclock_of(node3)
    node2:exec(function(node1_id, node3_id)
        t.assert_equals(box.info.synchro.queue.owner, node3_id)
        t.assert_equals(box.info.synchro.nodes[node1_id].promote, nil)
    end, {node1_id, node3_id})
    -- Node 1's WAL is released. Node 1 has seen node 3's term meanwhile and
    -- gave its promotion up, the CONFIRM lands nonetheless: applied on node 1
    -- first, a nop on the others, the chain has the PROMOTE LSN confirmed
    -- already.
    local ok, err = vraft.promote_join_at_wal_release(node1, 'promote1')
    t.assert_not(ok)
    t.assert_equals(err.code, box.error.INTERFERING_ELECTIONS)
    vraft.wait_sync(cg)
    for _, node in pairs(cg.nodes) do
        node:exec(function(node1_id, node3_id, term2, term3, promote1_lsn)
            t.assert_equals(box.info.synchro.queue.owner, node3_id)
            t.assert_equals(box.info.synchro.queue.term, term3)
            t.assert_equals(box.info.synchro.term_map[node1_id], term2)
            t.assert_le(promote1_lsn,
                        box.info.synchro.confirmed_vclock[node1_id])
            t.assert_equals(box.info.synchro.nodes[node1_id].promote, nil)
            t.assert_equals(box.space.s:select(), {{1}})
        end, {node1_id, node3_id, term2, term3, promote1_lsn})
        vraft.assert_all_upstreams_follow(node)
    end
end

--
-- A chain of three: two leaders in a row are slow with their CONFIRMs, each
-- chaining the one before, and the third confirms the whole chain. The chain
-- carries its base's base through the links: the terms and the PROMOTE LSNs
-- of both. The late CONFIRMs land afterwards: on their own nodes they apply
-- the own promotion first and the longer chains over it, elsewhere they are
-- nops. The same state everywhere in the end.
--
-- Four nodes: a node with a stuck WAL can't vote or ack, and two of them are
-- stuck at a time.
--
g.test_late_confirms_of_two_chain_bases_are_nops = function(cg)
    build_replica_set(cg, 4, 2)
    local node1, node2, node3, node4 = unpack(cg.nodes)
    local node2_id, node3_id = node2:get_instance_id(), node3:get_instance_id()
    local node4_id = node4:get_instance_id()

    node1:exec(function()
        box.ctl.promote()
        t.assert_equals(box.info.synchro.queue.owner, box.info.id)
        box.schema.space.create('s', {is_sync = true}):create_index('p')
        box.space.s:replace{1}
    end)
    vraft.wait_sync(cg)
    -- Node 2's PROMOTE gathers its quorum and replicates everywhere, the
    -- CONFIRM is stuck.
    vraft.promote_start_blocked_at_confirm(node2, 'promote2')
    local term2 = node2:get_election_term()
    local promote2_lsn = node2:exec(function()
        return box.info.synchro.own_promote.lsn
    end)
    for _, node in pairs({node1, node3, node4}) do
        node:wait_for_vclock_of(node2)
    end
    -- Node 3 chains over the pending PROMOTE of node 2, its PROMOTE
    -- replicates to the nodes still writing, the CONFIRM is stuck.
    vraft.promote_start_blocked_at_confirm(node3, 'promote3')
    local term3 = node3:get_election_term()
    t.assert_lt(term2, term3)
    local promote3_lsn = node3:exec(function(node2_id, term2)
        local promote = box.info.synchro.nodes[box.info.id].promote
        t.assert_equals(promote.term_map[node2_id], term2)
        return box.info.synchro.own_promote.lsn
    end, {node2_id, term2})
    for _, node in pairs({node1, node4}) do
        node:wait_for_vclock_of(node3)
        node:exec(function(node2_id, node3_id, term2, term3)
            t.assert_equals(box.info.synchro.nodes[node2_id].promote.term,
                            term2)
            t.assert_equals(box.info.synchro.nodes[node3_id].promote.term,
                            term3)
        end, {node2_id, node3_id, term2, term3})
    end
    -- Node 4 chains over node 3's PROMOTE and confirms with node 1's ack. The
    -- chain of three applies: both bases covered, their slots dropped, their
    -- PROMOTE LSNs confirmed.
    node4:exec(function(node2_id, node3_id, term2, term3, promote2_lsn,
                        promote3_lsn)
        box.ctl.promote()
        t.assert_equals(box.info.synchro.queue.owner, box.info.id)
        t.assert_equals(box.info.synchro.term_map[node2_id], term2)
        t.assert_equals(box.info.synchro.term_map[node3_id], term3)
        t.assert_equals(box.info.synchro.nodes[node2_id].promote, nil)
        t.assert_equals(box.info.synchro.nodes[node3_id].promote, nil)
        t.assert_le(promote2_lsn, box.info.synchro.confirmed_vclock[node2_id])
        t.assert_le(promote3_lsn, box.info.synchro.confirmed_vclock[node3_id])
        box.space.s:replace{2}
    end, {node2_id, node3_id, term2, term3, promote2_lsn, promote3_lsn})
    local term4 = node4:get_election_term()
    t.assert_lt(term3, term4)
    node1:wait_for_vclock_of(node4)
    node1:exec(function(node2_id, node3_id, node4_id)
        t.assert_equals(box.info.synchro.queue.owner, node4_id)
        t.assert_equals(box.info.synchro.nodes[node2_id].promote, nil)
        t.assert_equals(box.info.synchro.nodes[node3_id].promote, nil)
        t.assert_equals(box.space.s:select(), {{1}, {2}})
    end, {node2_id, node3_id, node4_id})
    -- The WALs are released, the base first. Both promotions have been given
    -- up on the newer terms, the CONFIRMs land nonetheless. Node 2 applies
    -- its own promotion, then node 4's chain over it. Node 3 applies its own
    -- promotion with node 2's inside, then node 4's chain. On the other
    -- nodes the CONFIRMs are nops: the PROMOTE LSNs are confirmed already.
    local ok, err = vraft.promote_join_at_wal_release(node2, 'promote2')
    t.assert_not(ok)
    t.assert_equals(err.code, box.error.INTERFERING_ELECTIONS)
    ok, err = vraft.promote_join_at_wal_release(node3, 'promote3')
    t.assert_not(ok)
    t.assert_equals(err.code, box.error.INTERFERING_ELECTIONS)
    vraft.wait_sync(cg)
    for _, node in pairs(cg.nodes) do
        node:exec(function(node2_id, node3_id, node4_id, term2, term3, term4,
                           promote2_lsn, promote3_lsn)
            t.assert_equals(box.info.synchro.queue.owner, node4_id)
            t.assert_equals(box.info.synchro.queue.term, term4)
            t.assert_equals(box.info.synchro.term_map[node2_id], term2)
            t.assert_equals(box.info.synchro.term_map[node3_id], term3)
            t.assert_le(promote2_lsn,
                        box.info.synchro.confirmed_vclock[node2_id])
            t.assert_le(promote3_lsn,
                        box.info.synchro.confirmed_vclock[node3_id])
            t.assert_equals(box.info.synchro.nodes[node2_id].promote, nil)
            t.assert_equals(box.info.synchro.nodes[node3_id].promote, nil)
            t.assert_equals(box.space.s:select(), {{1}, {2}})
        end, {node2_id, node3_id, node4_id, term2, term3, term4, promote2_lsn,
              promote3_lsn})
        vraft.assert_all_upstreams_follow(node)
    end
end

--
-- The chain and its CONFIRM reach the base's author ahead of the Raft term,
-- while it is still the leader of its term waiting for the base's quorum. A
-- forwarding relay sends the rows and the Raft state pushes through the same
-- socket, and only the relay's own care keeps the term ahead of the PROMOTE.
-- The chain's CONFIRM consumes the author's entry: nothing is poisoned, the
-- own PROMOTE is gone, applied as a part of the chain. The promotion must
-- fail with a clear error naming the new owner, the leadership stays until
-- the term arrives.
--
-- Node 1 replicates from node 2 only, through a link. Node 2 replicates from
-- node 1 through another link and from node 3 directly, node 3 from node 2
-- only. Node 1's PROMOTE goes through to node 2 and on to node 3, but node 2's
-- acks are held, so the PROMOTE never gathers its quorum. Node 3 knows the
-- PROMOTE and chains it.
--
g.test_own_pending_consumed_by_chain_ahead_of_term = function(cg)
    build_unwired_replica_set(cg, 3, 2)
    local node1, node2, node3 = unpack(cg.nodes)
    local node1_id, node3_id = node1:get_instance_id(), node3:get_instance_id()
    local link_1_from_2 = vraft.make_link(cg, 1, 2, TIMEOUT)
    local link_2_from_1 = vraft.make_link(cg, 2, 1, TIMEOUT)
    node1:update_box_cfg{replication = {link_1_from_2.uri}}
    node2:update_box_cfg{replication = {link_2_from_1.uri, cg.uris[3]}}
    node3:update_box_cfg{replication = {cg.uris[2]}}
    for _, node in pairs(cg.nodes) do
        vraft.wait_for_all_upstreams_follow(node)
    end
    local from_node2 = link_1_from_2.down
    local acks_from_node2 = link_2_from_1.up
    -- Node 1 wins the term with node 2's vote and writes its PROMOTE. The row
    -- reaches node 2 and node 3, node 2's ack of it is held: the PROMOTE stays
    -- pending everywhere.
    acks_from_node2:hold()
    vraft.promote_start(node1, 'promote1')
    node1:exec(function()
        t.helpers.retrying({timeout = _G.TIMEOUT}, function()
            t.assert_equals(box.info.election.state, 'leader')
            t.assert_not_equals(box.info.synchro.own_promote, nil)
        end)
    end)
    local term1 = node1:get_election_term()
    node2:wait_for_vclock_of(node1)
    node3:wait_for_vclock_of(node1)
    for _, node in pairs(cg.nodes) do
        node:exec(function(node1_id, term1)
            local promote = box.info.synchro.nodes[node1_id].promote
            t.assert_equals(promote.term, term1)
            t.assert_not(promote.is_poisoned)
        end, {node1_id, term1})
    end
    -- Node 1 hears nothing from now on. Node 3 takes the next term with node
    -- 2's vote and chains node 1's PROMOTE: the chain carries node 1's term
    -- and is confirmed with node 2's ack. Node 1's slot is dropped where the
    -- chain applies.
    from_node2:hold()
    node3:exec(function(node1_id, term1)
        box.ctl.promote()
        t.assert_equals(box.info.synchro.queue.owner, box.info.id)
        t.assert_equals(box.info.synchro.term_map[node1_id], term1)
        t.assert_equals(box.info.synchro.nodes[node1_id].promote, nil)
    end, {node1_id, term1})
    local term2 = node3:get_election_term()
    node2:wait_for_vclock_of(node3)
    node2:exec(function(node1_id, node3_id, term1, term2)
        t.assert_equals(box.info.synchro.queue.owner, node3_id)
        t.assert_equals(box.info.synchro.queue.term, term2)
        t.assert_equals(box.info.synchro.term_map[node1_id], term1)
        t.assert_equals(box.info.synchro.nodes[node1_id].promote, nil)
    end, {node1_id, node3_id, term1, term2})
    -- Node 2 relays the promotion to node 1: its Raft state with the new
    -- term, the PROMOTE, the CONFIRM. Keep the Raft pushes, let the rows go.
    -- The hold stays, so a later push can't reach node 1 before the checks.
    from_node2:wait_held(function(queue)
        return has_type(queue, box.iproto.type.RAFT_PROMOTE) and
               has_type(queue, box.iproto.type.RAFT_CONFIRM)
    end)
    local pushes = take_out(from_node2.queue, is_raft)
    t.assert_gt(#pushes, 0)
    from_node2:release_first(#from_node2.queue)
    -- Node 1 is still the leader of the old term, holding the queue of the
    -- new owner. Its own PROMOTE is gone, applied as a part of the chain: the
    -- promotion fails naming the owner.
    local ok, err = vraft.promote_join(node1, 'promote1')
    t.assert_not(ok)
    t.assert_equals(err.code, box.error.INTERFERING_PROMOTE)
    t.assert_equals(err.replica_id, node3_id)
    node1:exec(function(node1_id, node3_id, term1, term2)
        t.assert_equals(box.info.election.state, 'leader')
        t.assert_equals(box.info.election.term, term1)
        t.assert(box.info.ro)
        t.assert_equals(box.info.synchro.queue.owner, node3_id)
        t.assert_equals(box.info.synchro.queue.term, term2)
        t.assert_equals(box.info.synchro.term_map[node1_id], term1)
        t.assert_equals(box.info.synchro.nodes[node1_id].promote, nil)
        t.assert_equals(box.info.synchro.own_promote, nil)
    end, {node1_id, node3_id, term1, term2})
    -- The term arrives, node 1 steps down.
    for i, push in ipairs(pushes) do
        table.insert(from_node2.queue, i, push)
    end
    from_node2:release()
    acks_from_node2:release()
    node1:wait_for_election_term(term2)
    node1:exec(function()
        t.assert_equals(box.info.election.state, 'follower')
    end)
    -- The set healed, everyone agrees.
    node1:update_box_cfg{replication = {link_1_from_2.uri, cg.uris[3]}}
    node3:update_box_cfg{replication = {cg.uris[1], cg.uris[2]}}
    for _, node in pairs(cg.nodes) do
        vraft.wait_for_all_upstreams_follow(node)
    end
    vraft.wait_sync(cg)
    for _, node in pairs(cg.nodes) do
        node:exec(function(node1_id, node3_id, term1, term2)
            t.assert_equals(box.info.election.leader, node3_id)
            t.assert_equals(box.info.synchro.queue.owner, node3_id)
            t.assert_equals(box.info.synchro.queue.term, term2)
            t.assert_equals(box.info.synchro.term_map[node1_id], term1)
            t.assert_equals(box.info.synchro.nodes[node1_id].promote, nil)
        end, {node1_id, node3_id, term1, term2})
        vraft.assert_all_upstreams_follow(node)
    end
end
