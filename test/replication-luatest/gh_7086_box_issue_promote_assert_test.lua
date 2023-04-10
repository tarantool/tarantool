local luatest = require('luatest')
local server = require('luatest.server')
local cluster = require('luatest.replica_set')

local g = luatest.group('gh-7086')

-- On every server in cluster wait until its vclock is up to date with others
local function wait_sync(servers)
    for _, server_1 in ipairs(servers) do
        for _, server_2 in ipairs(servers) do
            if server_1 ~= server_2 then
                server_1:wait_for_election_term(server_2:get_election_term())
                server_1:wait_for_vclock_of(server_2)
            end
        end
    end
end

local function cluster_init(g)
    g.cluster = cluster:new({})

    g.box_cfg = {
        election_mode = 'off',
        replication_timeout = 0.1,
        replication_synchro_timeout = 5,
        replication_synchro_quorum = 1,
        replication = {
            server.build_listen_uri('server_1', g.cluster.id),
            server.build_listen_uri('server_2', g.cluster.id),
            server.build_listen_uri('server_3', g.cluster.id),
        },
    }

    g.server_1 = g.cluster:build_and_add_server(
        {alias = 'server_1', box_cfg = g.box_cfg})
    g.server_2 = g.cluster:build_and_add_server(
        {alias = 'server_2', box_cfg = g.box_cfg})
    g.server_3 = g.cluster:build_and_add_server(
        {alias = 'server_3', box_cfg = g.box_cfg})
    g.cluster:start()
    g.cluster:wait_for_fullmesh()
end

g.before_all(cluster_init)

local function wal_delay_start(server, countdown)
    if countdown == nil then
        server:exec(function()
            box.error.injection.set('ERRINJ_WAL_DELAY', true)
        end)
    else
        server:exec(function(countdown)
            box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', countdown)
        end, {countdown})
    end
end

local function wal_delay_end(server)
    server:exec(function()
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
    end)
end

local function promote_start(server)
    return server:exec(function()
        local f = require('fiber').new(box.ctl.promote)
        f:set_joinable(true)
        return f:id()
    end)
end

local function demote_start(server)
    return server:exec(function()
        local f = require('fiber').new(box.ctl.demote)
        f:set_joinable(true)
        return f:id()
    end)
end

local function fiber_join(server, fiber)
    return server:exec(function(fiber)
        return require('fiber').find(fiber):join()
    end, {fiber})
end

local function promote(server)
    server:exec(function()
        box.ctl.promote()
        box.ctl.wait_rw()
    end)
end

g.test_new_volatile_term_in_box_issue_promote = function(g)
    wal_delay_start(g.server_1)
    local f1 = promote_start(g.server_1)

    wal_delay_start(g.server_2, 0)
    local f2 = promote_start(g.server_2)
    g.server_2:play_wal_until_synchro_queue_is_busy()
    g.server_3:wait_for_election_term(g.server_2:get_election_term())

    wal_delay_start(g.server_3, 0)
    local f3 = promote_start(g.server_3)
    g.server_3:play_wal_until_synchro_queue_is_busy()
    g.server_2:wait_for_election_term(g.server_3:get_election_term())

    wal_delay_end(g.server_1)
    local ok, err = fiber_join(g.server_1, f1)

    wal_delay_end(g.server_2)
    wal_delay_end(g.server_3)
    fiber_join(g.server_2, f2)
    fiber_join(g.server_3, f3)

    luatest.assert(not ok and err.code == box.error.INTERFERING_ELECTIONS,
        'interfering promote not handled')

    -- This leaves cluster in split brain state, lets reinit it.
    g.cluster:drop()
    cluster_init(g)
end

g.test_new_volatile_term_in_box_issue_demote = function(g)
    promote(g.server_1)
    wait_sync(g.cluster.servers)

    wal_delay_start(g.server_1)
    local f1 = demote_start(g.server_1)

    wal_delay_start(g.server_2, 0)
    local f2 = promote_start(g.server_2)
    g.server_2:play_wal_until_synchro_queue_is_busy()
    g.server_3:wait_for_election_term(g.server_2:get_election_term())

    wal_delay_start(g.server_3, 0)
    local f3 = promote_start(g.server_3)
    g.server_3:play_wal_until_synchro_queue_is_busy()
    g.server_2:wait_for_election_term(g.server_3:get_election_term())

    wal_delay_end(g.server_1)
    local ok, err = fiber_join(g.server_1, f1)

    wal_delay_end(g.server_2)
    wal_delay_end(g.server_3)
    fiber_join(g.server_2, f2)
    fiber_join(g.server_3, f3)

    luatest.assert(not ok and err.code == box.error.INTERFERING_ELECTIONS,
        'interfering promote not handled')

    -- This leaves cluster in split brain state, lets reinit it.
    g.cluster:drop()
    cluster_init(g)
end

-- Promoting should fail if it is interrupted from another server
-- while acquiring limbo.
g.test_txn_limbo_begin_interfering_promote = function(g)
    g.server_2:exec(function()
        box.error.injection.set('ERRINJ_TXN_LIMBO_BEGIN_DELAY', true)
    end)

    local term = g.server_1:get_election_term()
    local f = promote_start(g.server_2)
    g.server_1:wait_for_election_term(term + 1)

    term = g.server_2:get_election_term()
    g.server_1:exec(function() box.ctl.promote() end)
    g.server_2:wait_for_election_term(term + 1)

    g.server_2:exec(function()
        box.error.injection.set('ERRINJ_TXN_LIMBO_BEGIN_DELAY', false)
    end)

    local ok, err = fiber_join(g.server_2, f)
    luatest.assert(not ok and
        (err.code == box.error.INTERFERING_PROMOTE or
            err.code == box.error.INTERFERING_ELECTIONS),
        'interfering promote not handled')

    g.server_1:exec(function() box.ctl.demote() end)
    wait_sync(g.cluster.servers)
end

-- Demoting should fail if it is interrupted from another server
-- while acquiring limbo.
g.test_txn_limbo_begin_interfering_demote = function(g)
    promote(g.server_2)
    wait_sync(g.cluster.servers)

    g.server_2:exec(function()
        box.error.injection.set('ERRINJ_TXN_LIMBO_BEGIN_DELAY', true)
    end)

    local term = g.server_1:get_election_term()
    local f = demote_start(g.server_2)
    g.server_1:wait_for_election_term(term + 1)

    term = g.server_2:get_election_term()
    g.server_1:exec(function() box.ctl.promote() end)
    g.server_2:wait_for_election_term(term + 1)

    g.server_2:exec(function()
        box.error.injection.set('ERRINJ_TXN_LIMBO_BEGIN_DELAY', false)
    end)
    local ok, err = fiber_join(g.server_2, f)

    luatest.assert(not ok and
        (err.code == box.error.INTERFERING_PROMOTE or
            err.code == box.error.INTERFERING_ELECTIONS),
        'interfering demote not handled')

    g.server_1:exec(function() box.ctl.demote() end)
    wait_sync(g.cluster.servers)
end
