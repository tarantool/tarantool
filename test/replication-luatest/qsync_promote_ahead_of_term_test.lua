local t = require('luatest')
local replica_link = require('test.replication-luatest.lib.replica_link')
local vraft = require('test.replication-luatest.lib.vraft_tools')

local g = t.group()

-- Every wait in the file. Nothing here is expected to take long; a wait
-- running out is a failure, not slowness.
local TIMEOUT = 60

--
-- A PROMOTE can reach a node ahead of the Raft term it was made in. A
-- forwarding relay sends the rows and the Raft state pushes through the same
-- socket, and only the relay's own care keeps the term ahead of the PROMOTE.
-- The node holding such a PROMOTE then enters that term, or a newer one, by
-- its own elections, and the two have to be reconciled: the PROMOTE taken
-- ahead of the term wins, the own promotion fails with a clear error, right
-- away, and nothing asserts.
--
-- The order is made by hand. The node under test replicates from its upstream
-- through a link holding the upstream's packets: the Raft pushes are taken
-- out of the queue and delivered after the rows.
--
-- Winning a term another node already took needs a non-majority quorum: with
-- the quorum of one a self vote wins. The limbo is then what keeps the two
-- leaders of the term from both claiming the queue.
--

-- The debug-only box.info.synchro introspection.
g.before_all(function()
    t.tarantool.skip_if_not_debug()
end)

-- The nodes node1..nodeN of the quorum, unwired: the test builds the
-- topology itself, with the links in between.
local function build_unwired_replica_set(cg, node_count, quorum)
    vraft.build_replica_set(cg, {
        replication_timeout = 0.1,
        replication_synchro_quorum = quorum,
        replication_synchro_timeout = TIMEOUT,
        election_mode = 'manual',
        election_fencing_mode = 'off',
    }, {node_count = node_count, glob_timeout = TIMEOUT})
    for _, node in pairs(cg.nodes) do
        node:update_box_cfg{replication = {}}
    end
    cg.links = {}
end

-- The links are in cg.links, stopped after the test.
local function make_link(cg, downstream, upstream, timeout)
    local link = vraft.make_link(cg, downstream, upstream, timeout)
    table.insert(cg.links, link)
    return link
end

g.after_each(function(cg)
    cg.replica_set:drop()
    for _, link in pairs(cg.links) do
        link:stop()
    end
end)

local has_type = replica_link.has_type
local take_out = replica_link.take_out
local is_raft = replica_link.is_type(box.iproto.type.RAFT)

-- Let the packets through one by one until a PROMOTE is at the front of the
-- queue. Only the Raft pushes of the election and the heartbeats are expected
-- before it.
local function pass_until_promote(dir)
    while true do
        dir:wait_held(1)
        local packet = dir.queue[1]
        if packet.type == box.iproto.type.RAFT_PROMOTE then
            return
        end
        t.assert(packet.type == box.iproto.type.RAFT or
                 packet.type == box.iproto.type.OK, packet.type)
        dir:release_first(1)
    end
end

local function assert_poisoned_promote(node, author_id, term)
    node:exec(function(author_id, term)
        local promote = box.info.synchro.nodes[author_id].promote
        t.assert_equals(promote.term, term)
        t.assert(promote.is_poisoned)
    end, {author_id, term})
end

local function assert_queue_owner(node, owner_id, term)
    node:exec(function(owner_id, term)
        t.assert_equals(box.info.synchro.queue.owner, owner_id)
        t.assert_equals(box.info.synchro.queue.term, term)
    end, {owner_id, term})
end

--
-- The leader of the older term getting a newer PROMOTE with its own one still
-- pending: the own PROMOTE is poisoned, the promotion fails with a clear
-- error while the leadership stays, and the node steps down once the term
-- arrives.
--
-- Node 1 replicates from node 2 only, through a link. Node 2 replicates from
-- node 1 through another link and from node 3 directly, node 3 from node 2
-- only. So node 1 learns about node 3 through node 2 alone, and node 1's
-- PROMOTE can be kept from node 2: node 1's Raft pushes go through, so node 2
-- votes for it, while the PROMOTE itself stays in the queue, unknown to the
-- others and pending on node 1.
--
g.test_own_pending_unknown_to_new_leader = function(cg)
    build_unwired_replica_set(cg, 3, 2)
    local node1, node2, node3 = unpack(cg.nodes)
    local node1_id, node3_id = node1:get_instance_id(), node3:get_instance_id()
    local link_1_from_2 = make_link(cg, 1, 2, TIMEOUT)
    local link_2_from_1 = make_link(cg, 2, 1, TIMEOUT)
    node1:update_box_cfg{replication = {link_1_from_2.uri}}
    node2:update_box_cfg{replication = {link_2_from_1.uri, cg.uris[3]}}
    node3:update_box_cfg{replication = {cg.uris[2]}}
    for _, node in pairs(cg.nodes) do
        vraft.wait_for_all_upstreams_follow(node)
    end
    local to_node2 = link_2_from_1.down
    local from_node2 = link_1_from_2.down
    -- Node 1 wins the term: its Raft pushes go through, node 2 votes. The
    -- PROMOTE stays in the queue, pending on node 1 alone.
    to_node2:hold()
    vraft.promote_start(node1, 'promote1')
    pass_until_promote(to_node2)
    local term1 = node1:get_election_term()
    node1:exec(function(term1)
        t.assert_equals(box.info.election.state, 'leader')
        local promote = box.info.synchro.nodes[box.info.id].promote
        t.assert_equals(promote.term, term1)
        t.assert_not(promote.is_poisoned)
    end, {term1})
    for _, node in pairs({node2, node3}) do
        node:wait_for_election_term(term1)
        node:exec(function(node1_id)
            local slot = box.info.synchro.nodes[node1_id]
            t.assert(slot == nil or slot.promote == nil)
        end, {node1_id})
    end
    -- Node 1 hears nothing from now on - the acks of node 2 carry its term
    -- too. Node 3 takes the next term with node 2's vote and claims the queue
    -- with node 2's ack, knowing nothing of node 1's PROMOTE.
    from_node2:hold()
    link_2_from_1.up:hold()
    node3:exec(function()
        box.ctl.promote()
        t.assert_equals(box.info.synchro.queue.owner, box.info.id)
    end)
    local term2 = node3:get_election_term()
    node2:wait_for_vclock_of(node3)
    assert_queue_owner(node2, node3_id, term2)
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
    -- new owner: the own PROMOTE is poisoned, the promotion fails.
    local ok, err = vraft.promote_join(node1, 'promote1')
    t.assert_not(ok)
    t.assert_equals(err.code, box.error.INTERFERING_ELECTIONS)
    node1:exec(function(term1)
        t.assert_equals(box.info.election.state, 'leader')
        t.assert_equals(box.info.election.term, term1)
        t.assert(box.info.ro)
    end, {term1})
    assert_queue_owner(node1, node3_id, term2)
    assert_poisoned_promote(node1, node1_id, term1)
    -- The term arrives, node 1 steps down. Its PROMOTE reaches the others
    -- below the queue's term and gets poisoned there too.
    for i, push in ipairs(pushes) do
        table.insert(from_node2.queue, i, push)
    end
    from_node2:release()
    to_node2:release()
    link_2_from_1.up:release()
    node1:wait_for_election_term(term2)
    node1:exec(function()
        t.assert_equals(box.info.election.state, 'follower')
    end)
    node2:wait_for_vclock_of(node1)
    node3:wait_for_vclock_of(node1)
    -- The set healed, everyone agrees.
    node1:update_box_cfg{replication = {link_1_from_2.uri, cg.uris[3]}}
    node3:update_box_cfg{replication = {cg.uris[1], cg.uris[2]}}
    for _, node in pairs(cg.nodes) do
        vraft.wait_for_all_upstreams_follow(node)
    end
    vraft.wait_sync(cg)
    for _, node in pairs(cg.nodes) do
        node:exec(function(node3_id)
            t.assert_equals(box.info.election.leader, node3_id)
        end, {node3_id})
        assert_queue_owner(node, node3_id, term2)
        assert_poisoned_promote(node, node1_id, term1)
        vraft.assert_all_upstreams_follow(node)
    end
end

--
-- A node holding a foreign pending PROMOTE of a term it hasn't reached yet
-- wins that term itself. Its own PROMOTE can't be built over the pending one
-- - the two would be a fork of one term. The promotion must fail with a
-- clear error right away, not wait for a claim which never comes and time
-- out.
--
g.test_promote_over_foreign_pending = function(cg)
    build_unwired_replica_set(cg, 2, 1)
    local node1, node2 = unpack(cg.nodes)
    local node2_id = node2:get_instance_id()
    local link_1_from_2 = make_link(cg, 1, 2, TIMEOUT)
    node1:update_box_cfg{replication = {link_1_from_2.uri}}
    node2:update_box_cfg{replication = {cg.uris[1]}}
    for _, node in pairs(cg.nodes) do
        vraft.wait_for_all_upstreams_follow(node)
    end
    local from_node2 = link_1_from_2.down
    local term1 = node1:get_election_term()
    -- Node 2 takes the next term alone and claims the queue. Node 1 hears
    -- nothing of it yet.
    from_node2:hold()
    node2:exec(function()
        box.ctl.promote()
        t.assert_equals(box.info.synchro.queue.owner, box.info.id)
    end)
    local term2 = node2:get_election_term()
    -- Node 1 gets the PROMOTE alone: ahead of the term, without its CONFIRM.
    from_node2:wait_held(function(queue)
        return has_type(queue, box.iproto.type.RAFT_PROMOTE) and
               has_type(queue, box.iproto.type.RAFT_CONFIRM)
    end)
    local pushes = take_out(from_node2.queue, is_raft)
    t.assert_gt(#pushes, 0)
    t.assert_equals(from_node2.queue[1].type, box.iproto.type.RAFT_PROMOTE)
    from_node2:release_first(1)
    node1:exec(function(node2_id, term1, term2)
        t.helpers.retrying({timeout = _G.TIMEOUT}, function()
            t.assert_not_equals(box.info.synchro.nodes[node2_id], nil)
            t.assert_not_equals(box.info.synchro.nodes[node2_id].promote, nil)
        end)
        local promote = box.info.synchro.nodes[node2_id].promote
        t.assert_equals(promote.term, term2)
        t.assert_not(promote.is_poisoned)
        t.assert_equals(box.info.election.term, term1)
        t.assert_lt(box.info.synchro.queue.term, term2)
    end, {node2_id, term1, term2})
    -- Node 1 wins the same term with its own vote. Its PROMOTE can't be
    -- built over the pending one: the promotion fails at once.
    local ok, err = node1:exec(function()
        return pcall(box.ctl.promote)
    end)
    t.assert_not(ok)
    t.assert_equals(err.code, box.error.INTERFERING_ELECTIONS)
    node1:exec(function(term2)
        t.assert_equals(box.info.election.state, 'leader')
        t.assert_equals(box.info.election.term, term2)
        local slot = box.info.synchro.nodes[box.info.id]
        t.assert(slot == nil or slot.promote == nil)
        t.assert_lt(box.info.synchro.queue.term, term2)
    end, {term2})
    -- The rest arrives: node 2's claim applies, node 1 steps off its
    -- forked leadership. Nothing of node 1 is left anywhere.
    for i, push in ipairs(pushes) do
        table.insert(from_node2.queue, i, push)
    end
    from_node2:release()
    node1:exec(function(node2_id, term2)
        t.helpers.retrying({timeout = _G.TIMEOUT}, function()
            t.assert_equals(box.info.synchro.queue.owner, node2_id)
            t.assert_equals(box.info.synchro.queue.term, term2)
        end)
        box.ctl.demote()
        t.assert_equals(box.info.election.state, 'follower')
    end, {node2_id, term2})
    vraft.wait_sync(cg)
    for _, node in pairs(cg.nodes) do
        node:exec(function(node2_id, term2)
            t.assert_equals(box.info.synchro.queue.owner, node2_id)
            t.assert_equals(box.info.synchro.queue.term, term2)
            t.assert_equals(box.info.synchro.nodes[node2_id].promote, nil)
        end, {node2_id, term2})
        vraft.assert_all_upstreams_follow(node)
    end
end

--
-- The same with the foreign PROMOTE taking effect at once: made with the
-- elections off, it has no CONFIRM to wait for and claims the queue on
-- arrival. The node then wins the term the queue is already claimed in. The
-- promotion must fail with a clear error, with the leadership intact, since
-- the own PROMOTE of a used term can't be made.
--
g.test_promote_over_foreign_claim = function(cg)
    build_unwired_replica_set(cg, 2, 1)
    local node1, node2 = unpack(cg.nodes)
    local node2_id = node2:get_instance_id()
    local link_1_from_2 = make_link(cg, 1, 2, TIMEOUT)
    node1:update_box_cfg{replication = {link_1_from_2.uri}}
    node2:update_box_cfg{replication = {cg.uris[1]}}
    for _, node in pairs(cg.nodes) do
        vraft.wait_for_all_upstreams_follow(node)
    end
    local from_node2 = link_1_from_2.down
    local term1 = node1:get_election_term()
    -- Node 2 takes the next term alone with the elections off: its PROMOTE
    -- claims the queue right away, nothing pending. Node 1 hears nothing of
    -- it yet.
    node2:update_box_cfg{election_mode = 'off'}
    from_node2:hold()
    node2:exec(function()
        box.ctl.promote()
        t.assert_equals(box.info.synchro.queue.owner, box.info.id)
        t.assert_equals(box.info.synchro.nodes[box.info.id].promote, nil)
    end)
    local term2 = node2:get_election_term()
    -- Node 1 gets the PROMOTE alone, ahead of the term. The claim applies.
    from_node2:wait_held(function(queue)
        return has_type(queue, box.iproto.type.RAFT_PROMOTE)
    end)
    local pushes = take_out(from_node2.queue, is_raft)
    t.assert_gt(#pushes, 0)
    t.assert_equals(from_node2.queue[1].type, box.iproto.type.RAFT_PROMOTE)
    from_node2:release_first(1)
    node1:exec(function(node2_id, term1, term2)
        t.helpers.retrying({timeout = _G.TIMEOUT}, function()
            t.assert_equals(box.info.synchro.queue.term, term2)
        end)
        t.assert_equals(box.info.synchro.queue.owner, node2_id)
        t.assert_equals(box.info.synchro.nodes[node2_id].promote, nil)
        t.assert_equals(box.info.election.term, term1)
    end, {node2_id, term1, term2})
    -- Node 1 wins the same term with its own vote. The term is taken in the
    -- queue: the promotion fails at once, the leadership stays. The Raft
    -- worker's own attempt to make the PROMOTE is refused too.
    local ok, err = node1:exec(function()
        return pcall(box.ctl.promote)
    end)
    t.assert_not(ok)
    t.assert_equals(err.code, box.error.INTERFERING_ELECTIONS)
    node1:exec(function(term2)
        t.assert_equals(box.info.election.state, 'leader')
        t.assert_equals(box.info.election.term, term2)
        t.assert(box.info.ro)
        local slot = box.info.synchro.nodes[box.info.id]
        t.assert(slot == nil or slot.promote == nil)
    end, {term2})
    assert_queue_owner(node1, node2_id, term2)
    t.helpers.retrying({timeout = TIMEOUT}, function()
        t.assert(node1:grep_log('Instance with replica id ' .. node2_id ..
                                ' was promoted first'))
    end)
    -- Node 1 steps off its forked leadership, then the term arrives.
    node1:exec(function()
        box.ctl.demote()
        t.assert_equals(box.info.election.state, 'follower')
    end)
    for i, push in ipairs(pushes) do
        table.insert(from_node2.queue, i, push)
    end
    from_node2:release()
    vraft.wait_sync(cg)
    for _, node in pairs(cg.nodes) do
        node:exec(function(node2_id, term2)
            t.assert_equals(box.info.election.term, term2)
            t.assert_equals(box.info.synchro.nodes[node2_id].promote, nil)
        end, {node2_id, term2})
        assert_queue_owner(node, node2_id, term2)
        vraft.assert_all_upstreams_follow(node)
    end
end

--
-- A claim of a newer term applies while the own PROMOTE is being built: the
-- applier holds the limbo latch, writing the claim, when the Raft worker comes
-- for it. The worker must give the promotion up on what it finds under the
-- latch - the term it leads is below the queue's - with the clear error, and
-- write nothing. The promotion fails with the same error.
--
-- The claim has to be invisible until it applies: a claim seen pending or
-- applied at or above the term fails the promotion before the worker even
-- gets there. So it is an instant PROMOTE, made with the elections off, in a
-- term above node 1's: node 2 takes node 1's term from its candidacy push -
-- the disabled Raft still follows the terms - and promotes past it.
--
-- Three nodes, quorum 2. Node 1 runs for a term with node 3's vote, which a
-- link holds until node 1's applier is in the claim's write. Node 1 gets the
-- claim through another link, ahead of its term. Node 2 replicates from node 1
-- through a third link: its acks carry its term and would end node 1's
-- leadership, so they are held too.
--
g.test_newer_claim_applied_while_waiting_for_latch = function(cg)
    build_unwired_replica_set(cg, 3, 2)
    local node1, node2, node3 = unpack(cg.nodes)
    local node2_id = node2:get_instance_id()
    local link_1_from_2 = make_link(cg, 1, 2, TIMEOUT)
    local link_1_from_3 = make_link(cg, 1, 3, TIMEOUT)
    local link_2_from_1 = make_link(cg, 2, 1, TIMEOUT)
    node1:update_box_cfg{replication = {link_1_from_2.uri, link_1_from_3.uri}}
    node2:update_box_cfg{replication = {link_2_from_1.uri}}
    node3:update_box_cfg{replication = {cg.uris[1]}}
    for _, node in pairs(cg.nodes) do
        vraft.wait_for_all_upstreams_follow(node)
    end
    local from_node2 = link_1_from_2.down
    local from_node3 = link_1_from_3.down
    local acks_from_node2 = link_2_from_1.up
    -- Node 1 runs for the next term. Node 3 votes, the vote is held. Node 2
    -- follows the term without voting.
    node2:update_box_cfg{election_mode = 'off'}
    from_node2:hold()
    from_node3:hold()
    acks_from_node2:hold()
    node1:exec(function()
        -- The vote comes when the test lets it, the term must not run out.
        box.cfg{election_timeout = _G.TIMEOUT}
    end)
    vraft.promote_start(node1, 'promote1')
    node1:wait_for_election_state('candidate')
    local term1 = node1:get_election_term()
    node2:wait_for_election_term(term1)
    -- Node 2 takes the term after and claims the queue at once. Node 1 hears
    -- nothing of it yet.
    node2:exec(function()
        box.ctl.promote()
        t.assert_equals(box.info.synchro.queue.owner, box.info.id)
        t.assert_equals(box.info.synchro.nodes[box.info.id].promote, nil)
    end)
    local term2 = node2:get_election_term()
    t.assert_lt(term1, term2)
    -- The claim reaches node 1, the Raft pushes stay held. The term row was
    -- node 1's last free WAL write: the claim's write gets stuck, with the
    -- applier holding the latch. Nothing of it shows in the limbo yet.
    node1:exec(function()
        box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', 0)
    end)
    from_node2:wait_held(function(queue)
        return has_type(queue, box.iproto.type.RAFT_PROMOTE)
    end)
    local pushes = take_out(from_node2.queue, is_raft)
    t.assert_gt(#pushes, 0)
    from_node2:release_first(#from_node2.queue)
    node1:exec(function(node2_id, term1)
        t.helpers.retrying({timeout = _G.TIMEOUT}, function()
            t.assert(box.error.injection.get('ERRINJ_WAL_DELAY'))
            t.assert(box.info.synchro.queue.busy)
        end)
        t.assert_equals(box.info.election.state, 'candidate')
        t.assert_lt(box.info.synchro.queue.term, term1)
        local slot = box.info.synchro.nodes[node2_id]
        t.assert(slot == nil or slot.promote == nil)
    end, {node2_id, term1})
    -- The vote arrives, node 1 is the leader. Its Raft worker comes for the
    -- latch and waits.
    from_node3:release()
    node1:wait_for_election_state('leader')
    node1:exec(function()
        t.assert(box.info.synchro.queue.busy)
        t.assert_equals(box.info.synchro.own_promote, nil)
    end)
    -- Node 1's WAL is released: the claim applies, the worker gets the latch
    -- and finds its term below the queue's. It gives up, the promotion fails
    -- naming the owner, nothing is written.
    local ok, err = vraft.promote_join_at_wal_release(node1, 'promote1')
    t.assert_not(ok)
    t.assert_equals(err.code, box.error.INTERFERING_PROMOTE)
    t.assert_equals(err.replica_id, node2_id)
    node1:exec(function(term1)
        t.assert_equals(box.info.election.state, 'leader')
        t.assert_equals(box.info.election.term, term1)
        t.assert(box.info.ro)
        local slot = box.info.synchro.nodes[box.info.id]
        t.assert(slot == nil or slot.promote == nil)
        t.assert_equals(box.info.synchro.own_promote, nil)
    end, {term1})
    assert_queue_owner(node1, node2_id, term2)
    t.helpers.retrying({timeout = TIMEOUT}, function()
        t.assert(node1:grep_log('Instance with replica id ' .. node2_id ..
                                ' was promoted first'))
    end)
    t.assert_not(node1:grep_log('already used term'))
    -- Node 1 steps off its stale leadership, then the term arrives.
    node1:exec(function()
        box.ctl.demote()
        t.assert_equals(box.info.election.state, 'follower')
    end)
    for i, push in ipairs(pushes) do
        table.insert(from_node2.queue, i, push)
    end
    from_node2:release()
    acks_from_node2:release()
    node1:wait_for_election_term(term2)
    -- The set healed, everyone agrees. The links stay in place of the direct
    -- connections: a relay over a link outlives the applier.
    node1:update_box_cfg{replication = {link_1_from_2.uri, link_1_from_3.uri}}
    node2:update_box_cfg{replication = {link_2_from_1.uri, cg.uris[3]}}
    node3:update_box_cfg{replication = {cg.uris[1], cg.uris[2]}}
    for _, node in pairs(cg.nodes) do
        vraft.wait_for_all_upstreams_follow(node)
    end
    vraft.wait_sync(cg)
    for _, node in pairs(cg.nodes) do
        node:exec(function(node2_id, term2)
            t.assert_equals(box.info.election.term, term2)
            t.assert_equals(box.info.synchro.nodes[node2_id].promote, nil)
        end, {node2_id, term2})
        assert_queue_owner(node, node2_id, term2)
        vraft.assert_all_upstreams_follow(node)
    end
end
