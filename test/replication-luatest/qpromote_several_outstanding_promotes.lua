local t = require('luatest')
local common = require('test.replication-luatest.qpromote_common')

local g = common.make_test_group({nodes=5, quorum=3})

-- The idea here is that in a cluster of 5 nodes we can have 2 nodes
-- being unresponsive and cluster still should continue. In this case two
-- nodes become unresponsive by emitting promotes that get stuck.
g.test_two_stuck_outstanding_promotes = function(g)
    local n1 = g.cluster.servers[1]
    local n2 = g.cluster.servers[2]
    local n3 = g.cluster.servers[3]

    -- Both n1 and n2 have hard time pushing out their promotes.
    common.spawn_stuck_promote(n1)
    common.spawn_stuck_promote(n2)

    common.promote(n3)

    n3:exec(function()
        box.space.test:replace({ 1 })
    end)

    common.remove_wal_delay_on_xrow_type(n1)
    common.remove_wal_delay_on_xrow_type(n2)

    for _, server in ipairs(g.cluster.servers) do
        server:wait_for_vclock_of(n3)

        t.assert_equals(server:exec(function()
            return box.space.test:get { 1 }
        end), { 1 })
    end

    common.ensure_healthy(g.cluster.servers)

    common.promote(n1)
    n1:exec(function()
        box.space.test:replace({ 2 })
    end)

    for _, server in ipairs(g.cluster.servers) do
        t.assert_equals(server:exec(function()
            return box.space.test:get { 2 }
        end), { 2 })
    end

    common.ensure_healthy(g.cluster.servers)
end

-- Variation of the previous test, but here nodes get stuck on confirm request
g.test_two_stuck_outstanding_confirms = function(g)
    local n1 = g.cluster.servers[1]
    local n2 = g.cluster.servers[2]
    local n3 = g.cluster.servers[3]

    -- Both n1 and n2 have hard time pushing out their confirms.
    common.spawn_promote_stuck_on_confirm(n1)
    common.spawn_promote_stuck_on_confirm(n2)

    common.promote(n3)

    n3:exec(function()
        box.space.test:replace({ 1 })
    end)

    common.remove_wal_delay_on_xrow_type(n1)
    common.remove_wal_delay_on_xrow_type(n2)

    for _, server in ipairs(g.cluster.servers) do
        server:wait_for_vclock_of(n3)

        t.assert_equals(server:exec(function()
            return box.space.test:get { 1 }
        end), { 1 })
    end

    common.ensure_healthy(g.cluster.servers)

    common.promote(n1)
    n1:exec(function()
        box.space.test:replace({ 2 })
    end)

    for _, server in ipairs(g.cluster.servers) do
        t.assert_equals(server:exec(function()
            return box.space.test:get { 2 }
        end), { 2 })
    end

    common.ensure_healthy(g.cluster.servers)
end
