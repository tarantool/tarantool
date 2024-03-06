local t = require('luatest')

local common = require('test.replication-luatest.qpromote_common')

local g = common.make_test_group({nodes=5, quorum=3})

-- This test attempts to replicate environment where we have 2 nodes in each
-- of two datacenters The test models situation when connection between two
-- datacenters is lost and one datacenter is attempted to be brought back to
-- life by decreasing quorum from 3 to 2. Main problem here is what happens
-- when connection is restored. Before the patch messages from isolated subsets
-- lead to disruptions whence reaching other members of the cluster
g.test_two_plus_two_quorum_lowering = function(g)
    -- We have one extra node, isolate it first
    common.server_set_replication(g.cluster.servers[5], {})
    common.make_connected_mesh({
        g.cluster.servers[1],
        g.cluster.servers[2],
        g.cluster.servers[3],
        g.cluster.servers[4],
    })

    -- Establish 1 as a leader
    common.promote(g.cluster.servers[1])

    -- Disconnect 2 DC so we have 2 groups of 2 nodes isolated from each other
    common.make_connected_mesh({
        g.cluster.servers[1],
        g.cluster.servers[2],
    })
    common.make_connected_mesh({
        g.cluster.servers[3],
        g.cluster.servers[4],
    })

    common.server_set_synchro_quorum(g.cluster.servers[1], 2)

    -- n1 is writable
    g.cluster.servers[1]:exec(function()
        box.space.test:replace({ 2 })
    end)

    common.server_set_synchro_quorum(g.cluster.servers[2], 2)
    common.promote(g.cluster.servers[2])
    -- n2 is writable
    g.cluster.servers[2]:exec(function()
        box.space.test:replace({ 3 })
    end)

    -- reconnect all nodes
    common.make_connected_mesh(g.cluster.servers)

    for _, server in ipairs(g.cluster.servers) do
        server:wait_for_vclock_of(g.cluster.servers[2])
        common.server_set_synchro_quorum(server, 3)
    end

    t.helpers.retrying({}, function()
        common.ensure_healthy(g.cluster.servers)
    end)

    for _, server in pairs(g.cluster.servers) do
        t.assert_equals(server:exec(function()
            return box.space.test:get { 2 }
        end), { 2 })

        t.assert_equals(server:exec(function()
            return box.space.test:get { 3 }
        end), { 3 })
    end
end
