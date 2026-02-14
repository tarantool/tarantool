local t = require('luatest')
local cluster = require('luatest.replica_set')
local proxy = require('luatest.replica_proxy')
local server = require('luatest.server')
local assertions = require('luatest.assertions')

local g = t.group('cover-box-wait-limbo-acked')
--
-- gh-7318:
-- Cover box_wait_limbo_acked with tests.
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

local function server_becomes_the_leader_again(server)
    local prev_cfg = server:exec(function()
        local prev_cfg = box.cfg
        box.cfg{
            election_mode='candidate',
            replication_synchro_quorum=1
        }
        return prev_cfg
    end)
    server:wait_until_election_leader_found()
    server:exec(function(prev_cfg)
        t.assert_equals(box.info.election.leader, box.info.id)
        box.cfg{
            election_mode=prev_cfg.election_mode,
            replication_synchro_quorum=prev_cfg.replication_synchro_quorum,
        }
    end, {prev_cfg})
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
    cg.master = cg.cluster:build_and_add_server({
        alias = 'master',
        box_cfg = {
            replication = {
                server.build_listen_uri('master', cg.cluster.id),
                server.build_listen_uri('replica_proxy'),
            },
            election_mode = 'candidate',
            replication_synchro_quorum = 2,
            replication_synchro_timeout = 100000,
            replication_timeout = 0.1,
            election_fencing_mode='off',
        }
    })
    cg.replica = cg.cluster:build_and_add_server({
        alias = 'replica',
        box_cfg = {
            replication = {
                server.build_listen_uri('replica', cg.cluster.id),
                server.build_listen_uri('master_proxy'),
            },
            election_mode = 'off',
            replication_synchro_quorum = 2,
            replication_synchro_timeout = 100000,
            replication_timeout = 0.1,
            election_fencing_mode='off',
        }
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
    cg.master_proxy:stop()
    cg.replica_proxy:stop()
end)

g.test_update_greatest_term_while_wait_quorum = function(cg)
    -- The purpose of the test is to cover
    -- 'if (box_check_promote_term_intact(promote_term) != 0)' in
    -- 'box_wait_limbo_acked()' located immediately after 'box_wait_quorum()'.
    cg.replica:exec(function()
        box.cfg{election_mode = 'manual'}
    end)
    local term = cg.master:exec(function()
        local fiber = require('fiber')
        box.cfg{
            election_mode = 'manual',
            election_timeout = 1000,
            replication_synchro_quorum = 3,
        }
        t.assert_equals(box.info.election.state, 'leader')
        rawset(_G, 'f_txn', fiber.create(function()
            fiber.self():set_joinable(true)
            box.space.test:replace{1}
        end))
        t.assert_equals(box.info.synchro.queue.len, 1)
        t.assert_equals(box.info.synchro.queue.owner, box.info.id)
        t.assert_equals(box.info.election.state, 'leader')
        box.ctl.demote()
        t.assert_equals(box.info.election.state, 'follower')
        rawset(_G, 'f_promote', fiber.create(function()
            fiber.self():set_joinable(true)
            box.ctl.promote()
        end))
        t.helpers.retrying({timeout = 120}, function()
            t.assert_equals(box.info.election.state, 'leader')
        end)
        t.assert_equals(box.info.synchro.queue.owner, box.info.id)
        t.assert_lt(box.info.synchro.queue.term, box.info.election.term)
        return box.info.election.term
    end)
    cg.replica:exec(function(term)
        t.helpers.retrying({timeout = 120}, function()
            t.assert_equals(box.info.election.term, term)
        end)
        box.cfg{
            replication_synchro_quorum = 1,
            election_timeout = 1000,
        }
        box.ctl.promote()
    end, {term})
    cg.master:wait_for_vclock_of(cg.replica)
    cg.master:exec(function()
        -- XXX: this fiber is stuck in 'wait RW' and won't notice that this
        -- instance isn't the leader anymore.
        _G.f_promote:cancel()
        t.assert((_G.f_txn:join(10)))
    end)
end

g.test_txn_limbo_is_empty_while_wait_quorum = function(cg)
    -- The purpose of the test is to cover
    -- 'if (txn_limbo_is_empty(&txn_limbo))' in
    -- 'box_wait_limbo_acked()' located immediately after 'box_wait_quorum()'.
    local f = cg.replica:exec(function()
        box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', 0)
        local f = require('fiber').create(function()
            return pcall(box.ctl.promote)
        end)
        f:set_joinable(true)
        return f:id()
    end)
    server_wait_wal_is_blocked(cg.replica)
    cg.replica_proxy:pause()
    t.helpers.retrying({timeout = wait_timeout}, function()
        cg.master:exec(function()
            local status = box.info.replication[2].upstream.status
            t.assert(status ~= 'follow')
        end)
    end)
    cg.master:exec(function()
        require('fiber').create(function() box.space.test:insert{1} end)
    end)
    server_wait_synchro_queue_len_is_equal(cg.replica, 1)
    -- The master must not send 'CONFIRM' before fiber 'f'
    -- reaches 'box_wait_quorum'. However, we must collect
    -- quorum now, because then limbo on the master will be
    -- frozen after we write a new term on the replica.
    cg.master_proxy:pause()
    t.helpers.retrying({timeout = wait_timeout}, function()
        cg.replica:exec(function()
            local status = box.info.replication[1].upstream.status
            t.assert(status ~= 'follow')
        end)
    end)
    cg.master:exec(function()
        box.cfg{replication_synchro_quorum=1}
    end)
    server_wait_synchro_queue_len_is_equal(cg.master, 0)
    cg.master:exec(function()
        box.cfg{replication_synchro_quorum=2}
    end)
    local wait_quorum_count = get_wait_quorum_count(cg.replica)
    local ff = require('fiber').create(function()
        cg.replica:exec(function(f)
            box.error.injection.set('ERRINJ_WAL_DELAY', false)
            local _, ok, err = require('fiber').find(f):join()
            t.assert(ok)
            t.assert(not err)
        end, {f})
    end)
    ff:set_joinable(true)
    server_wait_wait_quorum_count_ge_than(cg.replica, wait_quorum_count + 1)
    -- Now the master will send 'CONFIRM'.
    cg.master_proxy:resume()
    server_wait_synchro_queue_len_is_equal(cg.replica, 0)
    cg.replica_proxy:resume()
    local ok, err = ff:join()
    t.assert(ok)
    t.assert(not err)
end

g.test_quorum_less_replication_synchro_quorum = function(cg)
    -- The purpose of the test is to cover
    -- 'if (quorum < replication_synchro_quorum)' in
    -- 'box_wait_limbo_acked()' located immediately after 'box_wait_quorum()'.
    local f = cg.replica:exec(function()
        box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', 0)
        local f = require('fiber').create(function()
            return pcall(box.ctl.promote)
        end)
        f:set_joinable(true)
        return f:id()
    end)
    server_wait_wal_is_blocked(cg.replica)
    cg.master:exec(function()
        require('fiber').create(function() box.space.test:insert{1} end)
    end)
    -- Replica will store the local variable 'quorum' with the value 1.
    cg.replica:exec(function()
        box.cfg{replication_synchro_quorum=1}
    end)
    server_wait_synchro_queue_len_is_equal(cg.replica, 1)
    cg.replica:exec(function()
        box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', 0)
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
    end)
    server_wait_wal_is_blocked(cg.replica)
    -- Currently 'quorum' = 1. Now let's increase the replica quorum value.
    cg.replica:exec(function()
        box.cfg{replication_synchro_quorum=2}
    end)
    cg.replica:exec(function(f)
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
        local _, ok, err = require('fiber').find(f):join()
        t.assert(ok)
        t.assert_not(err)
    end, {f})
    server_becomes_the_leader_again(cg.master)
    server_wait_synchro_queue_len_is_equal(cg.replica, 0)
end

g.test_new_synchronous_transactions_appeared_while_wait_quorum = function(cg)
    assertions.skip("not testable without crutches anymore")
    -- The purpose of the test is to cover
    -- 'if (wait_lsn < txn_limbo_last_synchro_entry(&txn_limbo)->lsn)' in
    -- 'box_wait_limbo_acked()' located immediately after 'box_wait_quorum()'.
    cg.master:exec(function()
        box.cfg{
            election_fencing_mode='off',
            replication_synchro_quorum=3
        }
    end)
    cg.replica_proxy:pause()
    t.helpers.retrying({timeout = wait_timeout}, function()
        cg.master:exec(function()
            local status = box.info.replication[2].upstream.status
            t.assert(status ~= 'follow')
        end)
    end)
    local f = cg.replica:exec(function()
        box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', 0)
        local f = require('fiber').create(function()
            return pcall(box.ctl.promote)
        end)
        f:set_joinable(true)
        return f:id()
    end)
    server_wait_wal_is_blocked(cg.replica)
    cg.master:exec(function()
        require('fiber').create(function() box.space.test:insert{1} end)
    end)
    server_wait_synchro_queue_len_is_equal(cg.master, 1)
    server_wait_synchro_queue_len_is_equal(cg.replica, 1)
    -- The second transaction should not arrive before at the replica
    -- before fiber 'f' reaches 'box_wait_quorum'.
    cg.master_proxy:pause()
    t.helpers.retrying({timeout = wait_timeout}, function()
        cg.replica:exec(function()
            local status = box.info.replication[1].upstream.status
            t.assert(status ~= 'follow')
        end)
    end)
    cg.master:exec(function()
        require('fiber').create(function() box.space.test:insert{2} end)
    end)
    server_wait_synchro_queue_len_is_equal(cg.master, 2)
    cg.replica:exec(function()
        box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', 0)
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
    end)
    server_wait_wal_is_blocked(cg.replica)
    local wait_quorum_count = get_wait_quorum_count(cg.replica)
    local ff = require('fiber').create(function()
        cg.replica:exec(function(f)
            box.error.injection.set('ERRINJ_WAL_DELAY', false)
            local _, ok, err = require('fiber').find(f):join()
            t.assert(not ok)
            t.assert_equals(err.type, 'ClientError')
            t.assert_equals(err.message, 'Couldn\'t wait for quorum 2: '
                .. 'new synchronous transactions appeared')
        end, {f})
    end)
    ff:set_joinable(true)
    server_wait_wait_quorum_count_ge_than(cg.replica, wait_quorum_count + 1)
    -- Now the master will send the second insert transaction.
    cg.master_proxy:resume()
    server_wait_synchro_queue_len_is_equal(cg.replica, 2)
    cg.replica_proxy:resume()
    local ok, err = ff:join()
    t.assert(ok)
    t.assert(not err)
    server_becomes_the_leader_again(cg.master)
end
