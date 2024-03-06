local t = require('luatest')
local common = require('test.replication-luatest.qpromote_common')

local g = common.make_test_group({nodes=5, quorum=3})

-- This test breaks linear flow of limbo transition process.
-- Basically initial leader gets isolated, then second node
-- is elected but promote gets stuck. After that third node
-- is elected. This way third election for initial leader will
-- have different previous limbo owner breaking linear history.
-- This test is a regression one, it fails on unpatched version.
--
-- Note: strictly speaking content of the limbo doesn't matter
-- for this test. In other words the test always operates on
-- committed limbo. All dml operations are used to ensure that
-- cluster operates normally. I e data that was there before promote
-- is still there and cluster is writable.
g.test_diverging_limbo_owner = function(g)
    -- We're creating a setup where n<i> is a node and t<i> is a transaction:
    -- n1: t1, t2
    -- n2: t1, t2
    -- n3: t1, t2
    -- n4: t1
    -- n5: t1

    -- ensure n1 is a leader
    local n1 = g.cluster.servers[1]

    -- TODO: assert promote index/term made + 1
    common.promote(n1)

    t.helpers.retrying({}, function()
        common.ensure_healthy(g.cluster.servers)
    end)

    t.assert_equals(n1, g.cluster:get_leader(), "n1 must be the leader")

    -- Make t1 on all nodes:
    n1:exec(function()
        box.space.test:replace { 1 }
    end)

    for _, server in ipairs(g.cluster.servers) do
        server:wait_for_vclock_of(n1)
    end

    -- Make t2 on n{1,2,3}
    -- for servers 1-3 set replication to 1-3
    -- for servers 4-5 set empty replication
    -- make new tx on a leader, ensure it got propagated
    for i = 1, 3 do
        common.server_set_replication(
            g.cluster.servers[i],
            {
                g.cluster.servers[1].net_box_uri,
                g.cluster.servers[2].net_box_uri,
                g.cluster.servers[3].net_box_uri,
            })
    end

    for i = 4, 5 do
        common.server_set_replication(g.cluster.servers[i], {})
    end

    t.assert_equals(n1, g.cluster:get_leader(), "n1 must still be the leader")
    n1:exec(function()
        box.space.test:replace { 2 }
    end)
    for i = 1, 3 do
        g.cluster.servers[i]:wait_for_vclock_of(n1)
    end

    -- Elect n3, isolate n{1,2} and n{3,4,5}
    common.server_set_replication(g.cluster.servers[1], {})
    common.server_set_replication(g.cluster.servers[2], {})

    for i = 3, 5 do
        common.server_set_replication(
            g.cluster.servers[i],
            {
                g.cluster.servers[3].net_box_uri,
                g.cluster.servers[4].net_box_uri,
                g.cluster.servers[5].net_box_uri,
            })
    end

    local n3 = g.cluster.servers[3]
    common.spawn_stuck_promote(n3)

    -- Isolate n3
    common.server_set_replication(g.cluster.servers[3], {})
    -- Connect only n{2,4,5}
    for _, i in pairs({ 2, 4, 5 }) do
        common.server_set_replication(
            g.cluster.servers[i],
            {
                g.cluster.servers[2].net_box_uri,
                g.cluster.servers[4].net_box_uri,
                g.cluster.servers[5].net_box_uri,
            })
    end
    local n2 = g.cluster.servers[2]
    common.promote(n2)

    -- Let n2's promote reach n1
    -- Connect only n{1,2,4,5}
    for _, i in pairs({ 1, 2, 4, 5 }) do
        common.server_set_replication(
            g.cluster.servers[i],
            {
                g.cluster.servers[1].net_box_uri,
                g.cluster.servers[2].net_box_uri,
                g.cluster.servers[4].net_box_uri,
                g.cluster.servers[5].net_box_uri,
            })
    end

    common.remove_wal_delay_on_xrow_type(n3)

    -- Let n3's promote reach n1 (restore full connectivity)
    for i = 1, 5 do
        common.server_set_replication(
            g.cluster.servers[i],
            {
                g.cluster.servers[1].net_box_uri,
                g.cluster.servers[2].net_box_uri,
                g.cluster.servers[3].net_box_uri,
                g.cluster.servers[4].net_box_uri,
                g.cluster.servers[5].net_box_uri,
            })
    end

    -- Ensure n2 is writable
    n2:exec(function()
        box.space.test:replace { 3 }
    end)
    for _, server in ipairs(g.cluster.servers) do
        server:wait_for_vclock_of(n2)
    end

    -- This additionally ensures that data matches on all nodes
    t.helpers.retrying({}, function()
        common.ensure_healthy(g.cluster.servers)
    end)
end
