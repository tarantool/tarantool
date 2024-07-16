local t = require('luatest')
local cluster = require('luatest.replica_set')
local server = require('luatest.server')

local g = t.group('gh-5295')

-- To distinguish replicas by name
local test_id = 0

-- gh-5295: the server should stop replication from an upstream which sends data
-- conflicting in terms of promote / confirmed_lsn.
--
-- The test requires one instance in replicaset per each split-brain case, plus
-- one "main" instance. The idea of each test case is first to join the instance
-- and then partition it from the main server. Once partitioned, create a
-- split-brain situation between the partitioned node and main. Then check that
-- the partitioned node can't reconnect.

g.before_all(function(cg)
    cg.cluster = cluster:new({})

    cg.box_cfg = {
        replication_timeout         = 0.1,
        replication_synchro_quorum  = 1,
        replication_synchro_timeout = 0.01,
        election_timeout            = 0.1,
        election_fencing_mode       = 'off',
        log_level                   = 6,
    }

    cg.main = cg.cluster:build_and_add_server{
        alias = 'main',
        box_cfg = cg.box_cfg,
    }
    cg.cluster:start()

    cg.main:exec(function()
        box.ctl.promote()
        box.ctl.wait_rw()
        local s = box.schema.space.create('sync', {is_sync = true})
        s:create_index('pk')
        s = box.schema.space.create('async')
        s:create_index('pk')
        -- Check the test correctness.
        t.assert_equals(box.info.id, 1)
    end)
end)

local function update_replication(...)
    return box.cfg{replication = {...}}
end

g.before_each(function(cg)
    -- Check that the servers start synced and with main being leader.
    -- It's a prerequisite for each test.
    cg.main:exec(function()
        t.assert_equals(box.info.synchro.queue.owner,
                        box.info.id, 'main node is leader')
    end)

    test_id = test_id + 1
    cg.box_cfg.replication = {
            cg.main.net_box_uri,
            server.build_listen_uri('split_replica'..test_id, cg.cluster.id),
    }
    cg.split_replica = cg.cluster:build_and_add_server{
        alias = 'split_replica'..test_id,
        box_cfg = cg.box_cfg,
    }
    cg.split_replica:start()
    t.helpers.retrying({}, function()
        cg.split_replica:assert_follows_upstream(1)
    end)

    cg.main:exec(update_replication, cg.box_cfg.replication)
    t.helpers.retrying({}, function()
        cg.main:assert_follows_upstream(2)
    end)
end)

-- Drop the partitioned server after each case of split-brain.
g.after_each(function(cg)
    cg.split_replica:drop()
    -- Drop the replica's cluster entry, so that next one receives same id.
    cg.main:exec(function()
        box.ctl.promote()
        box.space._cluster:delete{2}
    end)
    cg.cluster.servers[2] = nil
end)

g.after_all(function(cg)
    cg.cluster:drop()
end)

local function partition_replica(cg)
    -- Each partitioning starts on synced servers.
    cg.split_replica:wait_for_vclock_of(cg.main)
    cg.main:wait_for_vclock_of(cg.split_replica)
    cg.split_replica:exec(update_replication, {})
    cg.main:exec(update_replication, {})
end

local function reconnect_and_check_split_brain(cg)
    local srv = cg.split_replica
    srv:exec(update_replication, {cg.main.net_box_uri})
    t.helpers.retrying({}, srv.exec, srv, function()
        local upstream = box.info.replication[1].upstream
        t.assert_equals(upstream.status, 'stopped', 'replication is stopped')
        t.assert_str_contains(upstream.message, 'Split-Brain discovered: ',
                              false, 'split-brain is discovered')
    end)
end

local function reconnect_and_check_no_split_brain(cg)
    local srv = cg.split_replica
    srv:exec(update_replication, {cg.main.net_box_uri})
    t.helpers.retrying({}, srv.exec, srv, function()
        local upstream = box.info.replication[1].upstream
        t.assert_equals(upstream.status, 'follow', 'no split-brain')
    end)
end

local function write_promote()
    t.assert_not_equals(box.info.synchro.queue.owner,  box.info.id,
                        "Promoting a follower")
    box.ctl.promote()
    box.ctl.wait_rw()
    t.helpers.retrying({}, function()
        t.assert_equals(box.info.synchro.queue.owner, box.info.id,
                        "Promote succeeded")
    end)
end

local function write_demote()
    t.assert_equals(box.info.synchro.queue.owner, box.info.id,
                    "Demoting the leader")
    box.cfg{election_mode = 'off'}
    box.ctl.demote()
    box.cfg{election_mode = 'manual'}
    t.assert_equals(box.info.synchro.queue.owner, 0, "Demote succeeded")
end

-- Any async transaction performed in an obsolete term means a split-brain.
g.test_async_old_term = function(cg)
    partition_replica(cg)
    cg.split_replica:exec(write_promote)
    cg.main:exec(function() box.space.async:replace{1} end)
    reconnect_and_check_split_brain(cg)
end

-- A conflicting sync transaction confirmation from an obsolete term means a
-- split-brain.
g.test_bad_confirm_old_term = function(cg)
    partition_replica(cg)
    cg.split_replica:exec(write_promote)
    cg.main:exec(function() box.space.sync:replace{1} end)
    reconnect_and_check_split_brain(cg)
end

-- Obsolete sync transaction confirmation might be fine when it doesn't
-- contradict local history.
g.test_good_confirm_old_term = function(cg)
    t.tarantool.skip_if_not_debug()
    -- Delay confirmation on the old leader, so that the transaction is included
    -- into new leader's PROMOTE, but the old leader writes a CONFIRM for it.
    cg.main:exec(function()
        local lsn = box.info.lsn
        box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', 1)
        require('fiber').new(function() box.space.sync:replace{1} end)
        t.helpers.retrying({}, function()
            t.assert_equals(box.info.lsn, lsn + 1)
        end)
    end)
    partition_replica(cg)
    cg.split_replica:exec(write_promote)
    cg.main:exec(function()
        local lsn = box.info.lsn
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
        t.helpers.retrying({}, function()
            t.assert_equals(box.info.lsn, lsn + 1)
        end)
    end)
    reconnect_and_check_no_split_brain(cg)
end

-- A conflicting sync transaction rollback from an obsolete term means a
-- split-brain.
g.test_bad_rollback_old_term = function(cg)
    t.tarantool.skip_if_not_debug()
    -- Delay rollback on the old leader, so that the transaction is included
    -- into new leader's PROMOTE, but the old leader writes a ROLLBACK for it.
    cg.main:exec(function()
        local lsn = box.info.lsn
        box.cfg{replication_synchro_quorum = 31}
        box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', 1)
        require('fiber').new(function() box.space.sync:replace{1} end)
        t.helpers.retrying({}, function()
            t.assert_equals(box.info.lsn, lsn + 1)
        end)
    end)
    partition_replica(cg)
    cg.split_replica:exec(write_promote)
    cg.main:exec(function()
        local lsn = box.info.lsn
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
        t.helpers.retrying({}, function()
            t.assert_equals(box.info.lsn, lsn + 1)
        end)
        box.cfg{replication_synchro_quorum = 1}
    end)
    reconnect_and_check_split_brain(cg)
end

-- Obsolete sync transaction rollback might be fine when it doesn't contradict
-- local history.
g.test_good_rollback_old_term = function(cg)
    partition_replica(cg)
    cg.split_replica:exec(write_promote)
    cg.main:exec(function()
        box.cfg{replication_synchro_quorum = 31}
        pcall(box.space.sync.replace, box.space.sync, {1})
        box.cfg{replication_synchro_quorum = 1}
    end)
    reconnect_and_check_no_split_brain(cg)
end

-- Conflicting demote for the same term is a split-brain.
g.test_demote_same_term = function(cg)
    partition_replica(cg)
    cg.split_replica:exec(write_promote)
    cg.main:exec(write_demote)
    reconnect_and_check_split_brain(cg)
    cg.main:exec(write_promote)
end

-- Conflicting promote for the same term is a split-brain.
g.test_promote_same_term = function(cg)
    cg.main:exec(write_demote)
    partition_replica(cg)
    cg.split_replica:exec(write_promote)
    cg.main:exec(write_promote)
    reconnect_and_check_split_brain(cg)
end

-- Promote from a bigger term with lsn < confirmed_lsn is a split brain.
g.test_promote_new_term_small_lsn = function(cg)
    cg.split_replica:exec(write_promote)
    partition_replica(cg)
    cg.split_replica:exec(function() box.space.sync:replace{1} end)
    cg.main:exec(write_promote)
    reconnect_and_check_split_brain(cg)
end

local function fill_queue_and_write(server)
    local wal_write_count = server:exec(function()
        local fiber = require('fiber')
        box.cfg{
            replication_synchro_quorum = 31,
            replication_synchro_timeout = 1000,
        }
        local write_cnt = box.error.injection.get('ERRINJ_WAL_WRITE_COUNT')
        fiber.new(box.space.sync.replace, box.space.sync, {1})
        return write_cnt
    end)
    t.helpers.retrying({}, server.exec, server, function(cnt)
        local new_cnt = box.error.injection.get('ERRINJ_WAL_WRITE_COUNT')
        t.assert(new_cnt > cnt, 'WAL write succeeded')
    end, {wal_write_count})
end

local function perform_rollback(server)
    t.assert_gt(server:exec(function() return box.info.synchro.queue.len end), 0)
    server:exec(function() box.cfg{replication_synchro_timeout = 0.01} end)
    t.helpers.retrying({delay = 0.1}, server.exec, server, function()
        t.assert_equals(box.info.synchro.queue.len, 0, 'Rollback happened')
    end)
end

-- Promote from a bigger term with lsn > confirmed_lsn is a split brain.
g.test_promote_new_term_big_lsn = function(cg)
    cg.split_replica:exec(write_promote)
    fill_queue_and_write(cg.split_replica)
    partition_replica(cg)
    perform_rollback(cg.split_replica)
    cg.main:exec(write_promote)
    reconnect_and_check_split_brain(cg)
end

-- Promote from a bigger term with conflicting queue contents is a split brain.
g.test_promote_new_term_conflicting_queue = function(cg)
    cg.split_replica:exec(write_promote)
    fill_queue_and_write(cg.split_replica)
    partition_replica(cg)
    perform_rollback(cg.split_replica)
    cg.main:exec(write_promote)
    fill_queue_and_write(cg.split_replica)
    reconnect_and_check_split_brain(cg)
end

local g_very_old_term = t.group('test-confirm-very-old-term')

g_very_old_term.before_each(function(cg)
    t.tarantool.skip_if_not_debug()
    cg.cluster = cluster:new{}
    cg.box_cfg = {
        replication_timeout = 0.1,
        replication_synchro_quorum = 1,
        replication = {
            server.build_listen_uri('server1', cg.cluster.id),
            server.build_listen_uri('server2', cg.cluster.id),
            server.build_listen_uri('server3', cg.cluster.id),
        },
        election_mode = 'voter',
    }
    cg.servers = {}
    for i = 1, 3 do
        cg.servers[i] = cg.cluster:build_and_add_server{
            alias = 'server' .. i,
            box_cfg = cg.box_cfg,
        }
    end
    cg.servers[1].box_cfg.election_mode = 'manual'
    cg.cluster:start()
    cg.cluster:wait_for_fullmesh()
    cg.servers[1]:exec(function()
        local s = box.schema.space.create('sync', {is_sync = true})
        s:create_index('pk')
    end)
end)

g_very_old_term.after_each(function(cg)
    cg.cluster:drop()
end)

g_very_old_term.test_confirm = function( cg)
    -- Create a sync transaction and block its confirmation.
    cg.servers[1]:exec(function()
        local lsn = box.info.lsn
        box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', 1)
        require('fiber').new(function()
            box.space.sync:insert{1}
        end)
        t.helpers.retrying({}, function()
            t.assert_equals(box.info.lsn, lsn + 1)
        end)
    end)
    -- Make sure the transaction reaches other servers and disconnect them.
    cg.servers[1]:update_box_cfg({replication = ""})
    local new_cfg = table.deepcopy(cg.box_cfg)
    table.remove(new_cfg.replication, 1)
    new_cfg.election_mode = 'manual'
    for i = 2, 3 do
        cg.servers[i]:wait_for_vclock_of(cg.servers[1])
        cg.servers[i]:update_box_cfg(new_cfg)
    end
    cg.servers[1]:exec(function()
        local lsn = box.info.lsn
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
        t.helpers.retrying({}, function()
            t.assert_equals(box.info.lsn, lsn + 1)
        end)
    end)
    -- Perform a few cycles: promote either server2 or server3, let it write
    -- some synchronous transaction.
    for j = 1, 4 do
        local leader = cg.servers[2 + j % 2]
        local replica = cg.servers[2 + (j - 1) % 2]
        leader:exec(function(n)
            box.ctl.promote()
            box.ctl.wait_rw()
            box.space.sync:insert{n}
        end, {j + 1})
        replica:wait_for_vclock_of(leader)
    end
    for i = 2, 3 do
        cg.servers[i]:exec(function() box.snapshot() end)
        cg.servers[i]:restart()
    end
    cg.servers[1]:update_box_cfg({replication = cg.box_cfg.replication})
    cg.cluster:wait_for_fullmesh()
end

local function test_promote_split_brain(cg, with_data)
    if with_data then
        cg.servers[1]:exec(function()
            box.schema.create_space('test', {is_sync = true}):create_index('pk')
        end)
        cg.servers[2]:wait_for_vclock_of(cg.servers[1])
        cg.servers[3]:wait_for_vclock_of(cg.servers[1])
    end
    for i = 2, 3 do
        cg.servers[i]:exec(function()
            box.cfg{election_mode = 'manual'}
        end)
    end

    -- Third server is network partitioned.
    local new_replication = {
        cg.servers[1].net_box_uri,
        cg.servers[2].net_box_uri,
    }

    cg.servers[3]:exec(update_replication, {})
    cg.servers[1]:exec(update_replication, new_replication)
    cg.servers[2]:exec(update_replication, new_replication)

    -- Bump term on the first server, so that it is more, than term
    -- of the third server after its promotion.
    cg.servers[2]:exec(write_promote)
    cg.servers[2]:wait_for_election_leader()
    local term = cg.servers[2]:get_synchro_queue_term()
    cg.servers[1]:wait_for_synchro_queue_term(term)
    cg.servers[1]:exec(write_promote)
    if with_data then
        cg.servers[1]:exec(function()
            box.space.test:replace({1, 's1'})
        end)
    end
    cg.servers[3]:exec(function()
        -- Otherwise it won't be able to start the promotion.
        box.cfg{replication_synchro_quorum = 1}
    end)
    cg.servers[3]:exec(write_promote)
    t.assert(cg.servers[3]:get_synchro_queue_term() <
             cg.servers[1]:get_synchro_queue_term())
    if with_data then
        cg.servers[3]:exec(function()
            box.space.test:replace({1, 's3'})
        end)
    end

    -- Split-Brain should be noticed, when the first server manages to
    -- connect to the third one.
    cg.servers[1]:exec(update_replication, {
        cg.servers[1].net_box_uri,
        cg.servers[2].net_box_uri,
        cg.servers[3].net_box_uri,
    })
    cg.servers[1]:exec(function(id)
        local message = 'Split-Brain discovered'
        t.helpers.retrying({timeout = 5}, function()
            local info = box.info.replication[id]
            t.assert_equals(info.upstream.status, 'stopped')
            t.assert_str_contains(info.upstream.message, message)
        end)
    end, {cg.servers[3]:get_instance_id()})
    if with_data then
        cg.servers[1]:exec(function()
            t.assert_equals(box.space.test:select(1), {{1, 's1'}})
        end)
        cg.servers[3]:exec(function()
            t.assert_equals(box.space.test:select(1), {{1, 's3'}})
        end)
    end
end

g_very_old_term.test_promote_split_brain_without_data = function(cg)
    -- Test, that we can detect split-brain even if none of the data CONFIRMs
    -- from the previous terms are received.
    test_promote_split_brain(cg, false)
end

g_very_old_term.test_promote_split_brain_without_data = function(cg)
    -- Test, that replication won't be recovered after split-brain is
    -- encountered. Data should remain the same, as it was before merging
    -- separated parts.
    test_promote_split_brain(cg, true)
end
