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
        election_fencing_enabled    = false,
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
            server.build_listen_uri('main'),
            server.build_listen_uri('split_replica'..test_id),
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
    cg.split_replica:stop()
    cg.split_replica:clean()
    -- Drop the replica's cluster entry, so that next one receives same id.
    cg.main:exec(function() box.space._cluster:delete{2} end)
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

local function reconnect_and_check_split_brain(srv)
    srv:exec(update_replication, {server.build_listen_uri('main')})
    t.helpers.retrying({}, srv.exec, srv, function()
        local upstream = box.info.replication[1].upstream
        t.assert_equals(upstream.status, 'stopped', 'replication is stopped')
        t.assert_str_contains(upstream.message, 'Split-Brain discovered: ',
                              false, 'split-brain is discovered')
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
    reconnect_and_check_split_brain(cg.split_replica)
end

-- Any unseen sync transaction confirmation from an obsolete term means a
-- split-brain.
g.test_confirm_old_term = function(cg)
    partition_replica(cg)
    cg.split_replica:exec(write_promote)
    cg.main:exec(function() box.space.sync:replace{1} end)
    reconnect_and_check_split_brain(cg.split_replica)
end

-- Any unseen sync transaction rollback from an obsolete term means a
-- split-brain.
g.test_rollback_old_term = function(cg)
    partition_replica(cg)
    cg.split_replica:exec(write_promote)
    cg.main:exec(function()
        box.cfg{replication_synchro_quorum = 31}
        pcall(box.space.sync.replace, box.space.sync, {1})
        box.cfg{replication_synchro_quorum = 1}
    end)
    reconnect_and_check_split_brain(cg.split_replica)
end

-- Conflicting demote for the same term is a split-brain.
g.test_demote_same_term = function(cg)
    partition_replica(cg)
    cg.split_replica:exec(write_promote)
    cg.main:exec(write_demote)
    reconnect_and_check_split_brain(cg.split_replica)
    cg.main:exec(write_promote)
end

-- Conflicting promote for the same term is a split-brain.
g.test_promote_same_term = function(cg)
    cg.main:exec(write_demote)
    partition_replica(cg)
    cg.split_replica:exec(write_promote)
    cg.main:exec(write_promote)
    reconnect_and_check_split_brain(cg.split_replica)
end

-- Promote from a bigger term with lsn < confirmed_lsn is a split brain.
g.test_promote_new_term_small_lsn = function(cg)
    cg.split_replica:exec(write_promote)
    partition_replica(cg)
    cg.split_replica:exec(function() box.space.sync:replace{1} end)
    cg.main:exec(write_promote)
    reconnect_and_check_split_brain(cg.split_replica)
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
    reconnect_and_check_split_brain(cg.split_replica)
end

-- Promote from a bigger term with conflicting queue contents is a split brain.
g.test_promote_new_term_conflicting_queue = function(cg)
    cg.split_replica:exec(write_promote)
    fill_queue_and_write(cg.split_replica)
    partition_replica(cg)
    perform_rollback(cg.split_replica)
    cg.main:exec(write_promote)
    fill_queue_and_write(cg.split_replica)
    reconnect_and_check_split_brain(cg.split_replica)
end
