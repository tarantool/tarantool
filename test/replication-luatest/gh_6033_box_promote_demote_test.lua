local luatest = require('luatest')
local server = require('luatest.server')
local cluster = require('luatest.replica_set')

local g_common = luatest.group('gh-6033-box-promote-demote')
local g_unconfigured = luatest.group('gh-6033-box-promote-demote-unconfigured')

local function box_cfg_update(servers, cfg)
    for _, server in ipairs(servers) do
        server:exec(function(cfg) box.cfg(cfg) end, {cfg})
    end
end

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

local function promote(server)
    server:exec(function()
        box.ctl.promote()
        box.ctl.wait_rw()
    end)
end

local function demote(server)
    server:exec(function() box.ctl.demote() end)
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

local function wal_delay_wait(server)
    luatest.helpers.retrying({}, server.exec, server, function()
        if not box.error.injection.get('ERRINJ_WAL_DELAY') then
            error('WAL still is not blocked')
        end
    end)
end

local function wal_delay_end(server)
    server:exec(function()
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
    end)
end

local function cluster_init(g)
    g.cluster = cluster:new({})

    g.box_cfg = {
        election_mode = 'off',
        election_timeout = box.NULL,
        replication_timeout = 0.1,
        replication_synchro_timeout = 5,
        replication_synchro_quorum = 1,
        replication = {
            server.build_listen_uri('server_1'),
            server.build_listen_uri('server_2'),
        },
    }

    g.server_1 = g.cluster:build_and_add_server(
        {alias = 'server_1', box_cfg = g.box_cfg})
    g.server_2 = g.cluster:build_and_add_server(
        {alias = 'server_2', box_cfg = g.box_cfg})
    g.cluster:start()
    g.cluster:wait_for_fullmesh()
end

local function cluster_reinit(g)
    box_cfg_update(g.cluster.servers, g.box_cfg)
    wait_sync(g.cluster.servers)
end

local function cluster_stop(g)
    g.cluster:drop()
end

g_common.before_all(cluster_init)
g_common.after_all(cluster_stop)
g_common.before_each(cluster_reinit)

-- Promoting/demoting should succeed if server is not configured.
g_unconfigured.test_unconfigured = function()
    local ok, err = pcall(box.ctl.promote)
    luatest.assert(ok, string.format(
        'Promoting unconfigured server is always successful: %s', err))

    local ok, err = pcall(box.ctl.demote)
    luatest.assert(ok, string.format(
        'Demoting unconfigured server is always successful: %s', err))
end

-- Promoting current RAFT leader and synchro queue owner should succeed
-- with elections enabled.
g_common.test_leader_promote = function(g)
    box_cfg_update({g.server_1}, {election_mode = 'manual'})
    promote(g.server_1)
    wait_sync(g.cluster.servers)

    local ok, err = g.server_1:exec(function()
        return pcall(box.ctl.promote)
    end)
    luatest.assert(ok, string.format(
        'Promoting current leader with elections on succeeds: %s', err))
    wait_sync(g.cluster.servers)

    box_cfg_update({g.server_1}, {election_mode = 'off'})
    promote(g.server_1)
    local ok, err = g.server_1:exec(function()
        return pcall(box.ctl.promote)
    end)
    luatest.assert(ok, string.format(
        'Promoting current leader with elections off succeeds: %s', err))
    wait_sync(g.cluster.servers)

    demote(g.server_1)
end

-- Demoting current follower should succeed.
g_common.test_follower_demote = function(g)
    local ok, err = g.server_1:exec(function()
        return pcall(box.ctl.demote)
    end)
    luatest.assert(ok, string.format(
        'Demoting follower with elections off is always successful: %s', err))
    wait_sync(g.cluster.servers)

    box_cfg_update({g.server_1}, {election_mode = 'manual'})
    local ok, err = g.server_1:exec(function()
        return pcall(box.ctl.demote)
    end)
    luatest.assert(ok, string.format(
        'Demoting follower with elections on is always successful: %s', err))
end

-- Promoting current RAFT leader should succeed,
-- even if he doesn't own synchro queue with elections enabled.
g_common.test_raft_leader_promote = function(g)
    box_cfg_update({g.server_1}, {election_mode = 'manual'})

    -- Promote server, but get stuck while writing PROMOTE
    -- (become RAFT leader without obtaining synchro queue)
    wal_delay_start(g.server_1, 0)
    local term = g.server_1:get_election_term()
    local f = promote_start(g.server_1)
    g.server_1:play_wal_until_synchro_queue_is_busy()
    g.server_1:wait_for_election_leader()

    local ok, err = g.server_1:exec(function()
        return pcall(box.ctl.promote)
    end)
    luatest.assert(ok, string.format(
        'Promoting current RAFT leader succeeds: %s', err))

    wal_delay_end(g.server_1)
    fiber_join(g.server_1, f)
    g.server_1:wait_for_synchro_queue_term(term + 1)
    wait_sync(g.cluster.servers)

    box_cfg_update({g.server_1}, {election_mode = 'off'})
    demote(g.server_1)
end

--
-- If a node stopped being a candidate, its box.ctl.promote() should abort right
-- away.
--
g_common.test_voter_during_promote = function(g)
    box_cfg_update({g.server_1, g.server_2}, {
        election_mode = 'manual',
        election_timeout = 1000,
        replication_synchro_quorum = 2,
    })
    wal_delay_start(g.server_1, 0)
    local promote_fid = promote_start(g.server_2)
    -- Server1 hangs on new term WAL write.
    wal_delay_wait(g.server_1)
    luatest.assert_equals(g.server_1:get_election_term(),
                          g.server_2:get_election_term())

    -- Server2 should stop the promotion without waiting for the term outcome
    -- because it no longer can win anyway.
    box_cfg_update({g.server_2}, {election_mode = 'voter'})
    wal_delay_end(g.server_1)
    fiber_join(g.server_2, promote_fid)

    -- Nobody won.
    local function get_election_state_f()
        return box.info.election.state
    end
    luatest.assert_equals(g.server_1:exec(get_election_state_f), 'follower')
    luatest.assert_equals(g.server_2:exec(get_election_state_f), 'follower')
end

-- Promoting and demoting should work when everything is ok.
g_common.test_ok = function(g)
    local ok, err = g.server_1:exec(function()
        return pcall(box.ctl.promote)
    end)
    luatest.assert(ok, string.format(
        'Promoting succeeds with elections off: %s', err))

    wait_sync(g.cluster.servers)
    local ok, err = g.server_1:exec(function()
        return pcall(box.ctl.demote)
    end)
    luatest.assert(ok, string.format(
        'Demoting succeeds with elections off: %s', err))

    box_cfg_update({g.server_1}, {election_mode = 'manual'})

    wait_sync(g.cluster.servers)
    local ok, err = g.server_1:exec(function()
        return pcall(box.ctl.promote)
    end)
    luatest.assert(ok, string.format(
        'Promoting succeeds with elections on: %s', err))

    wait_sync(g.cluster.servers)
    local ok, err = g.server_1:exec(function()
        return pcall(box.ctl.demote)
    end)
    luatest.assert(ok, string.format(
        'Demoting succeeds with elections on: %s', err))
end

-- Simultaneous promoting/demoting should fail.
g_common.test_simultaneous = function(g)
    wal_delay_start(g.server_1)

    local term = g.server_1:get_election_term()
    local f = promote_start(g.server_1)
    g.server_1:wait_for_election_term(term + 1)

    local ok, err = g.server_1:exec(function()
        return pcall(box.ctl.promote)
    end)
    luatest.assert(not ok and err.code == box.error.UNSUPPORTED,
        'Simultaneous promote fails')

    local ok, err = g.server_1:exec(function()
        return pcall(box.ctl.demote)
    end)
    luatest.assert(not ok and err.code == box.error.UNSUPPORTED,
        'Simultaneous demote fails')

    wal_delay_end(g.server_1)
    fiber_join(g.server_1, f)
    g.server_1:wait_for_synchro_queue_term(term + 1)
    wait_sync(g.cluster.servers)
    demote(g.server_1)
end

-- Promoting voter should fail.
g_common.test_voter_promote = function(g)
    box_cfg_update({g.server_1}, {election_mode = 'voter'})

    local ok, err = g.server_1:exec(function()
        return pcall(box.ctl.promote)
    end)
    luatest.assert(not ok and err.code == box.error.UNSUPPORTED,
        'Promoting voter fails')
end

-- Promoting should fail if it is interrupted from another server
-- while writing new term to wal.
g_common.test_wal_interfering_promote = function(g)
    -- New term is the first being written to WAL during promote. Block WAL to
    -- prevent persisting new term immediately (without yielding in
    -- box_raft_wait_term_persisted).
    wal_delay_start(g.server_1)
    local term = g.server_1:get_election_term()
    local f = promote_start(g.server_1)

    -- Volotile term gets incremented while promote fiber is yeilding in
    -- box_raft_wait_term_persisted. We continue yeilding in
    -- box_raft_wait_term_persisted but unblock WAL to write PROMOTE from
    -- server_2.
    g.server_1:wait_for_election_term(term + 1)
    g.server_1:exec(function()
        box.error.injection.set('ERRINJ_RAFT_WAIT_TERM_PERSISTED_DELAY', true)
    end)
    wal_delay_end(g.server_1)

    -- Promote server_2 and wait until new (interfering synchro queue term
    -- arrives to server_1, causing ongoing promote to fail.
    g.server_2:wait_for_election_term(term + 1)
    promote(g.server_2)
    g.server_1:wait_for_synchro_queue_term(term + 2)
    g.server_1:exec(function()
        box.error.injection.set('ERRINJ_RAFT_WAIT_TERM_PERSISTED_DELAY', false)
    end)

    local ok, err = fiber_join(g.server_1, f)
    luatest.assert(not ok and err.code == box.error.INTERFERING_PROMOTE,
        'Interfering promote fails')
    wait_sync(g.cluster.servers)

    -- server_1 incremented term and then failed it's promote.
    -- server_2 isn't leader in new term, but still limbo owner.
    term = g.server_1:get_synchro_queue_term()
    promote(g.server_1)
    demote(g.server_1)
    g.server_2:wait_for_synchro_queue_term(term + 2)
end

-- Demoting should fail if it is interrupted from another server
-- while writing new term to wal. Similar to test_wal_interfering_promote.
g_common.test_wal_interfering_demote = function(g)
    promote(g.server_1)
    wait_sync(g.cluster.servers)

    wal_delay_start(g.server_1)
    local term = g.server_1:get_election_term()
    local f = demote_start(g.server_1)

    g.server_1:wait_for_election_term(term + 1)
    g.server_1:exec(function()
        box.error.injection.set('ERRINJ_RAFT_WAIT_TERM_PERSISTED_DELAY', true)
    end)
    wal_delay_end(g.server_1)

    promote(g.server_2)
    g.server_1:wait_for_synchro_queue_term(g.server_2:get_synchro_queue_term())
    g.server_1:exec(function()
        box.error.injection.set('ERRINJ_RAFT_WAIT_TERM_PERSISTED_DELAY', false)
    end)

    local ok, err = fiber_join(g.server_1, f)
    luatest.assert(not ok and err.code == box.error.INTERFERING_PROMOTE,
        'Interfering demote fails')
    wait_sync(g.cluster.servers)

    -- server_1 incremented term and then failed it's demote.
    -- server_2 isn't leader in new term, but still limbo owner.
    promote(g.server_1)
    demote(g.server_1)
end

-- Promoting should fail if it is interrupted from another server
-- while waiting for synchro queue being emptied.
g_common.test_limbo_full_interfering_promote = function(g)
    promote(g.server_1)
    wait_sync(g.cluster.servers)

    -- Need 3 servers for this test:
    -- server_1 will try to promote with filled synchro queue,
    -- server_2 will interrupt server_1, while server_3 is leader
    local box_cfg = table.copy(g.box_cfg)
    box_cfg.replication = {
        server.build_listen_uri('server_1'),
        server.build_listen_uri('server_2'),
        server.build_listen_uri('server_3'),
    }

    local server_3 = g.cluster:build_server(
        {alias = 'server_3', box_cfg = box_cfg})
    server_3:start()
    wait_sync({g.server_1, g.server_2, server_3})
    box_cfg_update(g.cluster.servers, box_cfg)

    promote(server_3)
    server_3:exec(function()
        box.schema.create_space('test', {is_sync = true}):create_index('pk')
    end)
    wait_sync({g.server_1, g.server_2, server_3})

    box_cfg_update({g.server_1, server_3}, {
        replication_synchro_quorum = 4,
        replication_synchro_timeout = 1000,
    })
    box_cfg_update({g.server_2}, {replication_synchro_timeout = 0.1})

    server_3:exec(function()
        local s = box.space.test
        require('fiber').create(s.replace, s, {1})
    end)
    wait_sync({g.server_1, g.server_2, server_3})

    local f = promote_start(g.server_1)
    local term = g.server_2:get_election_term()
    promote(g.server_2)
    g.server_1:wait_for_synchro_queue_term(term + 1)

    local ok, err = fiber_join(g.server_1, f)
    luatest.assert(not ok and err.code == box.error.INTERFERING_PROMOTE,
        'Interfering promote fails')

    wait_sync({g.server_1, g.server_2, server_3})
    box_cfg_update(g.cluster.servers, g.box_cfg)
    server_3:drop()
    promote(g.server_1)
    g.server_1:exec(function() box.space.test:drop() end)
    demote(g.server_1)
    wait_sync(g.cluster.servers)
end

-- Demoting should fail if it is interrupted from another server
-- while waiting for synchro queue being emptied.
g_common.test_limbo_full_interfering_demote = function(g)
    promote(g.server_1)
    wait_sync(g.cluster.servers)

    g.server_1:exec(function()
        box.schema.create_space('test', {is_sync = true}):create_index('pk')
    end)

    box_cfg_update({g.server_1}, {
        replication_synchro_quorum = 3,
        replication_synchro_timeout = 1000,
    })

    box_cfg_update({g.server_2}, {
        replication_synchro_timeout = 0.1,
    })

    g.server_1:exec(function()
        local s = box.space.test
        require('fiber').create(s.replace, s, {1})
    end)
    wait_sync(g.cluster.servers)

    -- Start demoting server_1 and interrupt it from server_2
    local f = demote_start(g.server_1)
    local term = g.server_1:get_synchro_queue_term()
    g.server_2:exec(function() pcall(box.ctl.promote) end)
    g.server_1:wait_for_synchro_queue_term(term + 1)

    local ok, err = fiber_join(g.server_1, f)
    luatest.assert(not ok and err.code == box.error.INTERFERING_PROMOTE,
        'Interfering demote fails')

    wait_sync(g.cluster.servers)
    promote(g.server_1)
    g.server_1:exec(function() box.space.test:drop() end)
    demote(g.server_1)
    wait_sync(g.cluster.servers)
end

-- Promoting should fail if synchro queue replication timeouts during it
g_common.test_fail_limbo_ack_promote = function(g)
    box_cfg_update({g.server_1}, {
        replication_synchro_quorum = 3,
        replication_synchro_timeout = 0.1,
    })

    box_cfg_update({g.server_2}, {
        replication_synchro_quorum = 3,
        replication_synchro_timeout = 1000,
    })

    -- fill synchro queue on server_1
    promote(g.server_2)
    g.server_2:exec(function()
        local s = box.schema.create_space('test', {is_sync = true})
        s:create_index('pk')
        require('fiber').create(s.replace, s, {1})
    end)
    wait_sync(g.cluster.servers)

    -- start promoting with default replication_synchro_timeout,
    -- wait until promote reaches waiting for limbo_acked,
    -- make it timeout by lowering replication_synchro_timeout
    local f = promote_start(g.server_1)
    luatest.helpers.retrying({}, function()
        luatest.assert_not_equals(nil, g.server_1:grep_log(string.format(
            'RAFT: persisted state {term: %d}', g.server_1:get_election_term()))
        )
    end)
    box_cfg_update({g.server_1}, {replication_synchro_timeout = 0.01})
    local ok, err = fiber_join(g.server_1, f)
    luatest.assert(not ok and err.code == box.error.QUORUM_WAIT,
        'Promote failed because quorum wait timed out')

    box_cfg_update(g.cluster.servers, {replication_synchro_quorum = 2})
    promote(g.server_1)
    g.server_1:exec(function() box.space.test:drop() end)
    wait_sync(g.cluster.servers)
    demote(g.server_1)
end
