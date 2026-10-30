local t = require('luatest')
local cluster = require('luatest.replica_set')
local proxy = require('luatest.replica_proxy')
local server = require('luatest.server')

local g = t.group('assertion-in-box-promote-qsync')

local wait_timeout = 10

local function wait_pair_sync(server1, server2)
    -- Without retrying it fails sometimes when vclocks are empty and both
    -- instances are in 'connect' state instead of 'follow'.
    t.helpers.retrying({timeout = 10}, function()
        server1:wait_for_vclock_of(server2)
        server2:wait_for_vclock_of(server1)
        server1:assert_follows_upstream(server2:get_instance_id())
        server2:assert_follows_upstream(server1:get_instance_id())
    end)
end

local function server_wait_wal_is_blocked(server)
    server:exec(function()
        t.helpers.retrying({timeout = 10}, function()
            t.assert(box.error.injection.get('ERRINJ_WAL_DELAY'))
        end)
    end)
end

local function server_wait_synchro_queue_len_is_equal(server, expected)
    server:exec(function(expected)
        t.helpers.retrying({timeout = 10}, function(expected)
            t.assert_equals(box.info.synchro.queue.len, expected)
        end, expected)
    end, {expected})
end

local function get_wait_quorum_count(server)
    return server:exec(function()
        return box.error.injection.get('ERRINJ_WAIT_QUORUM_COUNT')
    end)
end

local function server_wait_wait_quorum_count_ge_than(server, threshold)
    server:exec(function(threshold, wait_timeout)
        t.helpers.retrying({timeout = wait_timeout}, function(threshold)
            t.assert_ge(box.error.injection.get('ERRINJ_WAIT_QUORUM_COUNT'),
                threshold)
        end, threshold)
    end, {threshold, wait_timeout})
end

g.before_each(function(cg)
    t.tarantool.skip_if_not_debug()

    cg.cluster = cluster:new({})

    local box_cfg = {
        replication = {
            server.build_listen_uri('master', cg.cluster.id),
            server.build_listen_uri('replica_proxy'),
        },
        election_mode = 'candidate',
        replication_synchro_quorum = 2,
        replication_synchro_timeout = 100000,
        replication_timeout = 0.1,
        election_fencing_mode = 'off',
    }
    cg.master = cg.cluster:build_and_add_server({
        alias = 'master',
        box_cfg = box_cfg
    })
    box_cfg.replication = {
        server.build_listen_uri('replica', cg.cluster.id),
        server.build_listen_uri('master_proxy'),
    }
    box_cfg.election_mode = 'off'
    cg.replica = cg.cluster:build_and_add_server({
        alias = 'replica',
        box_cfg = box_cfg
    })
    cg.master_proxy = proxy:new({
        client_socket_path = server.build_listen_uri('master_proxy'),
        server_socket_path = server.build_listen_uri('master', cg.cluster.id),
    })
    t.assert(cg.master_proxy:start({force = true}))
    cg.replica_proxy = proxy:new({
        client_socket_path = server.build_listen_uri('replica_proxy'),
        server_socket_path = server.build_listen_uri('replica', cg.cluster.id),
    })
    t.assert(cg.replica_proxy:start({force = true}))
    cg.cluster:start()
    cg.master:wait_until_election_leader_found()
    cg.replica:wait_until_election_leader_found()

    cg.master:exec(function()
        box.schema.space.create('test', {is_sync = true})
        box.space.test:create_index('pk')
    end)
    wait_pair_sync(cg.replica, cg.master)
end)

g.after_each(function(cg)
    cg.cluster:drop()
end)

g.test_is_in_box_promote = function(cg)
    local f = cg.replica:exec(function()
        box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', 0)
        local f = require('fiber').create(function() box.ctl.promote() end)
        f:set_joinable(true)
        return f:id()
    end)
    server_wait_wal_is_blocked(cg.replica)

    cg.replica_proxy:pause()

    t.helpers.retrying({timeout = 10}, function()
        cg.master:exec(function()
            local status = box.info.replication[2].upstream.status
            t.assert(status ~= 'follow')
        end)
    end)

    cg.master:exec(function()
        require('fiber').create(function() box.space.test:insert{1} end)
    end)
    server_wait_synchro_queue_len_is_equal(cg.replica, 1)

    local wait_quorum_count = get_wait_quorum_count(cg.replica)
    local ff = require('fiber').create(function()
        cg.replica:exec(function(f)
            box.error.injection.set('ERRINJ_WAL_DELAY', false)
            local ok, _ = require('fiber').find(f):join()
            t.assert(ok)
        end, {f})
    end)
    ff:set_joinable(true)
    -- We need to be sure we entered the 'box_wait_quorum' call.
    server_wait_wait_quorum_count_ge_than(cg.replica, wait_quorum_count + 1)
    cg.replica:exec(function()
        box.cfg{
            election_mode = 'candidate',
            replication_synchro_quorum = 1
        }
    end)
    cg.replica:wait_for_election_state('leader')
    cg.replica_proxy:resume()
    local ok, err = ff:join()
    t.assert_equals(err, nil)
    t.assert(ok)
end
