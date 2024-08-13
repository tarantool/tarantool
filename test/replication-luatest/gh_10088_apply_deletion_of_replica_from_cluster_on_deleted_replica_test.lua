local fio = require('fio')
local t = require('luatest')
local replica_set = require('luatest.replica_set')
local server = require('luatest.server')

local g_two_member_cluster = t.group('two_member_cluster')
local g_three_member_cluster = t.group('three_member_cluster')

g_two_member_cluster.before_each(function(cg)
    cg.master = server:new{alias = 'master', box_cfg = {
        replication_synchro_timeout = 120,
    }}
    cg.master:start()
    cg.replica = server:new{alias = 'replica', box_cfg = {
        replication = cg.master.net_box_uri,
        replication_synchro_timeout = 120,
    }}
    cg.replica:start()
    -- Add the replica to the master's replicaset table: needed for testing
    -- the behaviour of the deleted replica after recovery.
    cg.master:update_box_cfg{replication = cg.replica.net_box_uri}
    cg.master:exec(function()
        t.helpers.retrying({timeout = 120}, function()
            t.assert_not_equals(box.info.status, "orphan")
        end)
    end)
    -- Make `_cluster` space synchronous.
    cg.master:exec(function()
        box.ctl.promote(); box.ctl.wait_rw()
        box.space._cluster:alter{is_sync = true}
        box.schema.space.create('sync', {is_sync = true}):create_index('pk')
    end)
    cg.master:wait_for_downstream_to(cg.replica)
end)

-- Test that synchronous deletion from 2-member cluster works properly.
-- Also test the behaviour of the deleted replica after recovery.
g_two_member_cluster.test_deletion = function(cg)
    local replica_id = cg.replica:get_instance_id()
    local log_file = cg.master:exec(function()
        return rawget(_G, 'box_cfg_log_file') or box.cfg.log
    end)
    fio.truncate(log_file)
    cg.master:exec(function(replica_id)
        t.assert(box.space._cluster:delete{replica_id})
    end, {replica_id})
    cg.replica:wait_for_vclock_of(cg.master)
    cg.replica:exec(function()
        t.assert_equals(box.info.id, nil)
    end)
    t.assert_equals(cg.master:grep_log('joining replica'), nil)
    -- Test the behaviour of the deleted replica after recovery.
    cg.replica:restart(nil, {wait_until_ready = false})
    local panic_msg = "'replication_anon' cfg didn't work"
    local log = fio.pathjoin(cg.replica.workdir, cg.replica.alias..'.log')
    -- By default, the deleted replica must panic after recovery, because it is
    -- bootstrapped, it does not have an instance ID, and it is not configured
    -- as anonymous.
    t.helpers.retrying({timeout = 120}, function()
        t.assert(cg.replica:grep_log(panic_msg, nil, {filename = log}))
    end)
    cg.replica:restart({box_cfg = {
        replication = cg.master.net_box_uri,
        replication_anon = true,
        read_only = true,
    }}, {wait_until_ready = false})
    local err_msg = "Can't subscribe a previously deleted non%-anonymous " ..
                    "replica as an anonymous replica"
    -- If the deleted replica is configured as anonymous during recovery, the
    -- master sees it as a non-anonymous replica in his replicaset table, and
    -- rejects the subscription request.
    t.helpers.retrying({timeout = 120}, function()
        t.assert(cg.replica:grep_log(err_msg, nil, {filename = log}))
    end)
end

g_two_member_cluster.after_each(function(cg)
    cg.master:drop()
    cg.replica:drop()
end)

g_three_member_cluster.before_each(function(cg)
    cg.replica_set = replica_set:new{}
    cg.master = cg.replica_set:build_and_add_server{alias = 'master',
                                                    box_cfg = {
        replication_synchro_timeout = 120,
    }}
    cg.master:start()
    cg.replica =
        cg.replica_set:build_and_add_server{alias = 'replica',
                                            box_cfg = {
        replication = {
            cg.master.net_box_uri,
            server.build_listen_uri('to_be_deleted', cg.replica_set.id),
        },
    }}
    cg.replica_to_be_deleted =
        cg.replica_set:build_and_add_server{alias = 'to_be_deleted',
                                            box_cfg = {
        replication = {
            cg.master.net_box_uri,
            server.build_listen_uri('replica', cg.replica_set.id),
        },
    }}
    cg.replica_set:start()
    cg.master:exec(function()
        box.ctl.promote(); box.ctl.wait_rw()
        box.space._cluster:alter{is_sync = true}
        box.schema.space.create('sync', {is_sync = true}):create_index('pk')
    end)
    cg.master:wait_for_downstream_to(cg.replica)
    cg.master:wait_for_downstream_to(cg.replica_to_be_deleted)
end)

-- Test that synchronous deletion from 3-member cluster with 1 disabled node and
-- the deleted node alive works properly.
g_three_member_cluster.test_deletion = function(cg)
    cg.replica:exec(function()
        box.cfg{replication = ''}
    end)
    local to_be_deleted_id = cg.replica_to_be_deleted:get_instance_id()
    cg.master:exec(function(to_be_deleted_id)
        t.assert(box.space._cluster:delete{to_be_deleted_id})
    end, {to_be_deleted_id})
    cg.replica_to_be_deleted:wait_for_vclock_of(cg.master)
    cg.replica_to_be_deleted:exec(function()
        t.assert_equals(box.info.id, nil)
    end)
end

-- Test that the deleted replica stops all appliers, effectively stopping
-- replication, and cannot ack synchronous transactions.
g_three_member_cluster.test_behaviour_of_deleted_replica = function(cg)
    local to_be_deleted_id = cg.replica_to_be_deleted:get_instance_id()
    cg.master:exec(function(to_be_deleted_id)
        t.assert(box.space._cluster:delete{to_be_deleted_id})
    end, {to_be_deleted_id})
    cg.replica_to_be_deleted:wait_for_vclock_of(cg.master)
    local master_id = cg.master:get_instance_id()
    local replica_id = cg.replica:get_instance_id()
    cg.replica_to_be_deleted:exec(function(deleted_id, master_id, replica_id)
        t.assert_equals(box.info.id, nil)
        local msg = "The local instance id " .. deleted_id .. " is read-only"
        -- Test that the deleted node stopped replication with the master
        -- from which it received the delete.
        t.helpers.retrying({timeout = 120}, function()
            t.assert_equals(box.info.replication[master_id].upstream.status,
                            'stopped')
            t.assert_equals(box.info.replication[master_id].upstream.message,
                            msg)
        end)
        -- Test that the deleted node stopped replication with the other replica
        -- from which it did not receive the delete.
        t.helpers.retrying({timeout = 120}, function()
            t.assert_equals(box.info.replication[replica_id].upstream.status,
                            'stopped')
            t.assert_equals(box.info.replication[replica_id].upstream.message,
                            msg)
        end)
    end, {to_be_deleted_id, master_id, replica_id})

    -- Test that the deleted node cannot ack a synchronous transaction.
    cg.master:exec(function()
        box.cfg{replication_synchro_quorum = 3, replication_synchro_timeout = 1}

        local msg = 'Quorum collection for a synchronous transaction is ' ..
                    'timed out'
        t.assert_error_msg_content_equals(msg, function()
            box.space.sync:replace{0}
        end)
    end)
end

g_three_member_cluster.after_each(function(cg)
    cg.replica_set:drop()
end)
