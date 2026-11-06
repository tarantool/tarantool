local t = require('luatest')
local vraft = require('test.replication-luatest.lib.vraft_tools')

local g = t.group()

-- Every wait in the file. Nothing here is expected to take long; a wait
-- running out is a failure, not slowness.
local TIMEOUT = 10

--
-- The owner cut off from its quorum keeps writing: the transactions wait in
-- the queue on the owner and on the node still listening to it. A new leader
-- elected elsewhere claims the queue with the boundary below them. When the
-- claim arrives, the transactions are rolled back on both - the owner's
-- writers get the error, the listener rewinds the rows it had never acked
-- into existence. The old owner can lead again afterwards.
--

-- The debug-only box.info.synchro introspection.
g.before_all(function()
    t.tarantool.skip_if_not_debug()
end)

g.before_each(function(cg)
    -- Five nodes with the quorum of three: the owner with its listener is
    -- short of it, the other three are not.
    vraft.build_replica_set(cg, {
        replication_timeout = 0.1,
        replication_synchro_quorum = 3,
        replication_synchro_timeout = TIMEOUT,
        election_mode = 'manual',
        -- The owner keeps its state as it is while cut off.
        election_fencing_mode = 'off',
    }, {node_count = 5, glob_timeout = TIMEOUT})
end)

g.after_each(function(cg)
    cg.replica_set:drop()
end)

g.test_promote_rewinds_unacked_txns = function(cg)
    local node1, node2, node3, node4, node5 = unpack(cg.nodes)
    local node1_id, node3_id = node1:get_instance_id(), node3:get_instance_id()

    node1:exec(function()
        box.ctl.promote()
        t.assert_equals(box.info.synchro.queue.owner, box.info.id)
        box.schema.space.create('s', {is_sync = true}):create_index('p')
        box.space.s:replace{1}
    end)
    vraft.wait_sync(cg)
    -- The owner stays with node 2 alone, the other three with each other.
    local others = {cg.uris[3], cg.uris[4], cg.uris[5]}
    node1:update_box_cfg{replication = {cg.uris[2]}}
    node2:update_box_cfg{replication = {cg.uris[1]}}
    node3:update_box_cfg{replication = others}
    node4:update_box_cfg{replication = others}
    node5:update_box_cfg{replication = others}
    -- The owner writes transactions nobody but node 2 gets. They wait in the
    -- queue on both, visible, not committed.
    node1:exec(function()
        local fiber = require('fiber')
        local writers = {}
        for i = 2, 4 do
            local f = fiber.new(function()
                box.space.s:replace{i}
            end)
            f:set_joinable(true)
            table.insert(writers, f)
        end
        rawset(_G, 'writers', writers)
        t.helpers.retrying({timeout = _G.TIMEOUT}, function()
            t.assert_equals(box.info.synchro.queue.len, 3)
        end)
    end)
    node2:wait_for_vclock_of(node1)
    node2:exec(function()
        t.assert_equals(box.info.synchro.queue.len, 3)
        t.assert_equals(box.space.s:select(), {{1}, {2}, {3}, {4}})
    end)
    -- Node 3 takes the next term with the votes of its group and claims the
    -- queue with their acks. Its PROMOTE confirms up to what node 3 has - the
    -- first row. Node 3 writes on.
    node3:exec(function(node1_id)
        t.assert_equals(box.info.synchro.queue.owner, node1_id)
        box.ctl.promote()
        t.assert_equals(box.info.synchro.queue.owner, box.info.id)
        box.space.s:replace{5}
    end, {node1_id})
    local term2 = node3:get_election_term()
    for _, node in pairs({node4, node5}) do
        node:wait_for_vclock_of(node3)
        node:exec(function(node3_id)
            t.assert_equals(box.info.synchro.queue.owner, node3_id)
            t.assert_equals(box.space.s:select(), {{1}, {5}})
        end, {node3_id})
    end
    -- Everyone reconnects. The claim arrives to node 1 and node 2 with the
    -- boundary below their waiting transactions: rolled back on both, the
    -- writers get the error. Node 1 steps down on the term.
    for _, node in pairs(cg.nodes) do
        node:update_box_cfg{replication = cg.uris}
    end
    for _, node in pairs(cg.nodes) do
        vraft.wait_for_all_upstreams_follow(node)
    end
    node1:exec(function(term2)
        for _, f in pairs(_G.writers) do
            local ok, err = f:join(_G.TIMEOUT)
            t.assert_not(ok)
            t.assert_equals(err.code, box.error.SYNC_ROLLBACK)
        end
        rawset(_G, 'writers', nil)
        t.assert_equals(box.info.election.term, term2)
        t.assert_equals(box.info.election.state, 'follower')
    end, {term2})
    vraft.wait_sync(cg)
    for _, node in pairs(cg.nodes) do
        node:exec(function(node3_id, term2)
            t.assert_equals(box.info.synchro.queue.owner, node3_id)
            t.assert_equals(box.info.synchro.queue.term, term2)
            t.assert_equals(box.info.synchro.queue.len, 0)
            t.assert_equals(box.space.s:select(), {{1}, {5}})
        end, {node3_id, term2})
        vraft.assert_all_upstreams_follow(node)
    end
    -- The old owner leads again, nothing of the rewound history comes back.
    node1:exec(function()
        box.ctl.promote()
        t.assert_equals(box.info.synchro.queue.owner, box.info.id)
        box.space.s:replace{6}
    end)
    vraft.wait_sync(cg)
    for _, node in pairs(cg.nodes) do
        node:exec(function(node1_id)
            t.assert_equals(box.info.synchro.queue.owner, node1_id)
            t.assert_equals(box.space.s:select(), {{1}, {5}, {6}})
        end, {node1_id})
        vraft.assert_all_upstreams_follow(node)
    end
end
