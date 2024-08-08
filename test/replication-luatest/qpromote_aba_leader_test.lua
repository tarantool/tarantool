local t = require('luatest')
local common = require('test.replication-luatest.qpromote_common')

local g = common.make_test_group({nodes=5, quorum=3})

-- Aba in the test name means that node that was a leadeer lost its crown and
-- then after another node held the leader role become leader again.
-- It is important that transaction that was waiting for quorum before second
-- node was elected manages to commit when initial leader gets reelected.
-- We're creating a setup where n<i> is a node and t<i> is a transaction.
-- Promotes are denoted with p
-- n1: t1            p2 (n1's history should be accepted by all nodes)
-- n2:    p1 (stuck)     t1,p2
-- n3:                   t1,p2
-- n4:                   t1,p2
-- n5:                   t1,p2
g.test_aba_leader_stuck_promote = function(g)
    -- ensure n1 is a leader
    local n1 = g.cluster.servers[1]
    common.promote(n1)

    t.assert_equals(n1, g.cluster:get_leader(), "n1 must be the leader")
    -- isolate n1
    common.server_set_replication(n1, {})

    common.make_connected_mesh({
        g.cluster.servers[2],
        g.cluster.servers[3],
        g.cluster.servers[4],
        g.cluster.servers[5],
    })

    -- kick off t1. It won't be able to gather quorum
    n1:exec(function()
        require('fiber').new(function()
            box.space.test:replace { 1 }
        end)
    end)

    t.assert_equals(n1:exec(function()
        return box.space.test:get { 1 }
    end), { 1 })

    local n2 = g.cluster.servers[2]
    common.spawn_stuck_promote(n2)

    -- isolate n2, restore connectivity between other nodes
    common.server_set_replication(n2, {})

    local nodes_without_n2 = {
        g.cluster.servers[1],
        g.cluster.servers[3],
        g.cluster.servers[4],
        g.cluster.servers[5],
    }
    common.make_connected_mesh(nodes_without_n2)

    -- promote n1 again
    common.promote(n1)

    -- our t1 should've reached n3 for example
    g.cluster.servers[3]:wait_for_vclock_of(n1)
    t.assert_equals(g.cluster.servers[3]:exec(function()
        return box.space.test:get { 1 }
    end), { 1 })

    t.helpers.retrying({}, function()
        common.ensure_healthy(nodes_without_n2)
    end)

    common.remove_wal_delay_on_xrow_type(n2)

    common.make_connected_mesh(g.cluster.servers)

    -- we're interested in n2
    n2:wait_for_vclock_of(n1)
    t.assert_equals(n2:exec(function()
        return box.space.test:get { 1 }
    end), { 1 })

    -- ensure promote that was stuck reached all other nodes
    for _,server in ipairs(nodes_without_n2) do
        server:wait_for_vclock_of(n2)
    end

    t.helpers.retrying({}, function()
        common.ensure_healthy(g.cluster.servers)
    end)
end


-- This is a variation of previous test with an addition that stuck promote
-- reaches one other node before connections are healed.
-- This test also creates 3 transactions, second transaction is submitted on
-- a node that believes it is rw after issuing stuck promote. By the end of
-- the test transactions one and two should be committed and second one
-- shouldn't be visible.
g.test_aba_leader_with_stuck_promote_reaching_neighbour = function(g)
    -- ensure n1 is a leader
    local n1 = g.cluster.servers[1]
    common.promote(n1)

    t.assert_equals(n1, g.cluster:get_leader(), "n1 must be the leader")
    -- isolate n1
    common.server_set_replication(n1, {})

    common.make_connected_mesh({
        g.cluster.servers[2],
        g.cluster.servers[3],
        g.cluster.servers[4],
        g.cluster.servers[5],
    })

    -- kick off t1. It won't be able to gather quorum
    n1:exec(function()
        require('fiber').new(function()
            box.space.test:replace { 1 }
        end)
    end)

    t.assert_equals(n1:exec(function()
        return box.space.test:get { 1 }
    end), { 1 })

    local n2 = g.cluster.servers[2]
    common.spawn_stuck_promote(n2)

    -- isolate n2 and n3 restore connectivity between other nodes
    common.make_connected_mesh({
        g.cluster.servers[2],
        g.cluster.servers[3],
    })

    local second = {
        g.cluster.servers[1],
        g.cluster.servers[4],
        g.cluster.servers[5],
    }
    common.make_connected_mesh(second)

    t.helpers.retrying({}, function()
        common.ensure_healthy(second)
    end)

    common.remove_wal_delay_on_xrow_type(n2)
    local n3 = g.cluster.servers[3]
    n3:wait_for_vclock_of(n2)
    -- TODO assert n3 got promote from n2, assert synchro queue owner

    local n1_id = n1:get_instance_id()
    local n2_synchro_owner = n2:exec(function ()
       return box.info.synchro.queue.owner
    end)

    -- make sure still n2 sees n1 as synchro queue owner
    t.assert_equals(n2_synchro_owner, n1_id)

    -- promote n1 again
    common.promote(n1)

    -- this should succeed, there is quorum n{1,4,5}
    n1:exec(function()
        box.space.test:replace { 3 }
    end)

    common.make_connected_mesh(g.cluster.servers)

    -- we're especially interested in n2
    n2:wait_for_vclock_of(n1)

    for _, s in pairs(g.cluster.servers) do
        -- t1 is committed
        t.assert_equals(s:exec(function()
            return box.space.test:get { 1 }
        end), { 1 })
        -- t3 is committed
        t.assert_equals(s:exec(function()
            return box.space.test:get { 3 }
        end), { 3 })
    end
end

-- TODO describe
g.test_aba_leader_rollback_transaction = function(g)
    -- ensure n1 is a leader
    local n1 = g.cluster.servers[1]
    common.promote(n1)

    t.assert_equals(n1, g.cluster:get_leader(), "n1 must be the leader")
    -- isolate n1
    common.server_set_replication(n1, {})

    common.make_connected_mesh({
        g.cluster.servers[2],
        g.cluster.servers[3],
        g.cluster.servers[4],
        g.cluster.servers[5],
    })

    -- kick off t1. It won't be able to gather quorum
    n1:exec(function()
        require('fiber').new(function()
            box.space.test:replace { 1 }
        end)
    end)

    t.helpers.retrying({}, function ()
        local synchro = n1:exec(function()
            return box.info.synchro
        end)
        t.assert_equals(synchro.queue.len, 1,
            "limbo must contain one pending transaction")
    end)

    -- now n2 becomes leader with successful promote
    local n2 = g.cluster.servers[2]
    common.promote(n2)

    -- commit n2, at this point only n1 is expected to stay without it
    n2:exec(function()
        box.space.test:replace { 2 }
    end)

    -- n1 should now receive new history from n2 rollback t1 and apply t2
    common.make_connected_mesh(g.cluster.servers)

    -- promote n1 again
    common.promote(n1)

    t.helpers.retrying({}, function()
        common.ensure_healthy(g.cluster.servers)
    end)

    for _, s in pairs(g.cluster.servers) do
        -- t1 never happened
        t.assert_equals(s:exec(function()
            return box.space.test:get { 1 }
        end), nil)
        -- t2 is committed
        t.assert_equals(s:exec(function()
            return box.space.test:get { 2 }
        end), { 2 } )
    end
end
