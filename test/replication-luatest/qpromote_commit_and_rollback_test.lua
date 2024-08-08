local t = require('luatest')

local common = require('test.replication-luatest.qpromote_common')

local g = common.make_test_group({nodes=5, quorum=3})

local function range_insert(server, start, stop)
    server:exec(function(start, stop)
        for i = start, stop do
            box.space.test:replace { i }
        end
    end, { start, stop })
end

local function range_insert_spawn(server, start, stop)
    for i = start, stop do
        server:exec(function(val)
            require('fiber').new(function()
                box.space.test:replace { val }
            end)
        end, { i })

        -- validation pass, ensure we indeed made these changes
        t.assert_equals(server:exec(function(val)
            return box.space.test:get { val }
        end, { i }), { i })
    end
end

local function ensure_range_present(servers, start, stop)
    for _, server in ipairs(servers) do
        for i = start, stop do
            t.assert_equals(server:exec(function(val)
                return box.space.test:get { val }
            end, { i }), { i })
        end
    end
end

local function ensure_range_missing(servers, start, stop)
    for _, server in ipairs(servers) do
        for i = start, stop do
            t.assert_equals(server:exec(function(val)
                return box.space.test:get { val }
            end, { i }), nil)
        end
    end
end


local function do_test_commit_and_rollback(g, partial)
    local n1 = g.cluster.servers[1]
    local n2 = g.cluster.servers[2]
    local n3 = g.cluster.servers[3]

    common.promote(n1)

    range_insert(n1, 1, 3)

    local others
    if partial then
        common.make_connected_mesh({n1, n2})

        others = {
            g.cluster.servers[3],
            g.cluster.servers[4],
            g.cluster.servers[5],
        }
        common.make_connected_mesh(others)
    else
        others = {
            g.cluster.servers[2],
            g.cluster.servers[3],
            g.cluster.servers[4],
            g.cluster.servers[5],
        }
        common.server_set_replication(n1, {})
        common.make_connected_mesh(others)
    end

    range_insert_spawn(n1, 4, 6)

    if partial then
        -- ensure n2 got uncommitted yet tx from n1
        n2:wait_for_vclock_of(n1)
        ensure_range_present({n2}, 4, 6)
    end

    common.promote(n3)

    ensure_range_present(g.cluster.servers, 1, 3)

    range_insert(n3, 7, 9)

    t.helpers.retrying({}, function()
        ensure_range_present(others, 7, 9)
    end)

    common.make_connected_mesh(g.cluster.servers)

    t.helpers.retrying({}, function()
        -- transactions made prior leadership transitions
        ensure_range_present(g.cluster.servers, 1, 3)

        -- transactions made on n1 while it was isolated
        -- they shouldn't have committed
        ensure_range_missing(g.cluster.servers, 4, 6)

        -- these were made on n3, all servers should accept this history
        ensure_range_present(g.cluster.servers, 7, 9)

        common.ensure_healthy(g.cluster.servers)
    end)

    -- after all things have settled n1 should be back to normal
    -- and should be able to be successfully promoted again
    common.promote(n1)

    t.helpers.retrying({}, function()
        common.ensure_healthy(g.cluster.servers)
    end)

    range_insert(n1, 10, 12)

    t.helpers.retrying({}, function()
        ensure_range_present(g.cluster.servers, 10, 12)

        common.ensure_healthy(g.cluster.servers)
    end)
end

-- The test excersises the case when after promotion a node loses connection
-- to quorum of replicas. Any transaction performed on it won't be able to
-- commit. After successful leader election newly elected leader will be able
-- to accept new transactions and successfully commit them.
-- When connectivity is restored old leader must accept history of new leader
-- effectively rolling back transactions which didn't manage to get accepted
-- by quorum.
-- Additionally after connectivity is restored and replicas are synchronized
-- with each other old leader should be able to become leader again.
-- Note that in this test uncommitted transactions from old leader do not
-- reach any other node. The case when this happens is tested in
-- test_commit_and_rollback_partial
g.test_commit_and_rollback = function(g)
    do_test_commit_and_rollback(g, false)
end

-- This is a variation of the above test with the difference being
-- that uncommitted changes from old leader are replicated to some nodes
-- without having a quorum. This way we test the path when replica has to
-- rewind its history to accept a diverging one from the next leader.
g.test_commit_and_rollback_partial = function(g)
    do_test_commit_and_rollback(g, true)
end
