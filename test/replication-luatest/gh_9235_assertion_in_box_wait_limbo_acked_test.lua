local t = require('luatest')
local cluster = require('luatest.replica_set')
local server = require('luatest.server')

local g = t.group('assertion-in-box-wait-limbo-acked')
--
-- gh-9235:
-- Assertion in box_wait_limbo_acked.
--
local wait_timeout = 10

local function wait_pair_sync(server1, server2)
    -- Without retrying it fails sometimes when vclocks are empty and both
    -- instances are in 'connect' state instead of 'follow'.
    t.helpers.retrying({timeout = wait_timeout}, function()
        server1:wait_for_vclock_of(server2)
        server2:wait_for_vclock_of(server1)
        server1:assert_follows_upstream(server2:get_instance_id())
        server2:assert_follows_upstream(server1:get_instance_id())
    end)
end

local function server_wait_wal_is_blocked(server)
    server:exec(function(wait_timeout)
        t.helpers.retrying({timeout = wait_timeout}, function()
            t.assert(box.error.injection.get('ERRINJ_WAL_DELAY'))
        end)
    end, {wait_timeout})
end

local function server_wait_synchro_queue_len_is_equal(server, expected)
    server:exec(function(expected, wait_timeout)
        t.helpers.retrying({timeout = wait_timeout}, function(expected)
            t.assert_equals(box.info.synchro.queue.len, expected)
        end, expected)
    end, {expected, wait_timeout})
end

g.before_each(function(cg)
    t.tarantool.skip_if_not_debug()

    cg.cluster = cluster:new({})
    local box_cfg = {
        replication = {
            server.build_listen_uri('master', cg.cluster.id),
            server.build_listen_uri('replica', cg.cluster.id),
        },
        election_mode = 'candidate',
        replication_timeout = 0.1,
        election_fencing_mode='off',
        replication_synchro_quorum = 2,
        replication_synchro_timeout = 100000,
    }
    cg.master = cg.cluster:build_and_add_server({
        alias = 'master',
        box_cfg = box_cfg
    })
    box_cfg.election_mode = 'off'
    cg.replica = cg.cluster:build_and_add_server({
        alias = 'replica',
        box_cfg = box_cfg
    })
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

g.test_assert_last_entry_lsn_is_positive = function(cg)
    local f = cg.replica:exec(function()
       box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', 0)
       local f = require('fiber').create(function() box.ctl.promote() end)
       box.cfg{wal_queue_max_size=1}
       f:set_joinable(true)
       return f:id()
    end)
    server_wait_wal_is_blocked(cg.replica)
    cg.master:exec(function()
        require('fiber').create(function() box.space.test:insert{1} end)
        require('fiber').create(function() box.space.test:insert{2} end)
    end)
    server_wait_synchro_queue_len_is_equal(cg.replica, 1)
    cg.replica:exec(function(f)
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
        require('fiber').find(f):join()
    end, {f})
    cg.master:exec(function()
        box.cfg{replication_synchro_quorum=1}
    end)
    server_wait_synchro_queue_len_is_equal(cg.replica, 0)
end
