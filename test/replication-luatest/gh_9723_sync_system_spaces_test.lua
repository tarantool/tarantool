local cbuilder = require('test.config-luatest.cbuilder')
local cluster = require('test.config-luatest.cluster')
local compat = require('compat')
local fio = require('fio')
local fun = require('fun')
local t = require('luatest')
local treegen = require('test.treegen')
local replica_set = require('luatest.replica_set')
local server = require('test.luatest_helpers.server')
local yaml = require('yaml')

local g_general = t.group('general')
local g_recovery = t.group('recovery')
local g_schema_upgrade = t.group('schema_upgrade')

local check_sync_system_spaces = [[
    function(is_sync_state)
        local t = require('luatest')

        local sync_system_spaces = {
            '_schema',
            '_collation',
            '_vcollation',
            '_space',
            '_vspace',
            '_sequence',
            '_vsequence',
            '_index',
            '_vindex',
            '_func',
            '_vfunc',
            '_user',
            '_vuser',
            '_priv',
            '_vpriv',
            '_cluster',
            '_trigger',
            '_truncate',
            '_space_sequence',
            '_vspace_sequence',
            '_fk_constraint',
            '_ck_constraint',
            '_func_index',
            '_session_settings',
        }

        local async_system_spaces = {
            '_vinyl_deferred_delete', -- local space
            '_sequence_data', -- synchronized by synchronous space operations
        }

        for _, space in ipairs(sync_system_spaces) do
            t.assert_equals(box.space[space].is_sync, false)
            t.assert_equals(box.space[space].state.is_sync, is_sync_state)
        end
        for _, space in ipairs(async_system_spaces) do
            t.assert_equals(box.space[space].is_sync, false)
            t.assert_equals(box.space[space].state.is_sync, false)
        end
    end
]]

g_general.before_each(function(cg)
    cg.replica_set = replica_set:new{}
    local box_cfg = {
        replication = {
            server.build_listen_uri('server1', cg.replica_set.id),
            server.build_listen_uri('server2', cg.replica_set.id),
        },
        replication_timeout = 0.1,
        replication_synchro_timeout = 60,
    }
    cg.replication = box_cfg.replication
    cg.server1 = cg.replica_set:build_and_add_server{
        alias = 'server1',
        box_cfg = box_cfg,
    }
    cg.server2 = cg.replica_set:build_and_add_server{
        alias = 'server2',
        box_cfg = box_cfg,
    }
    cg.replica_set:start()
    cg.replica_set:wait_for_fullmesh()
    cg.server1:exec(function(check_sync_system_spaces)
        box.schema.func.create('check_sync_system_spaces', {
            body = check_sync_system_spaces,
        })
    end, {check_sync_system_spaces})
    cg.server1:wait_for_downstream_to(cg.server2)
end)

g_general.after_each(function(cg)
    cg.replica_set:drop()
end)

-- Test the old behaviour: the system spaces do not become synchronous when the
-- synchronous queue is claimed.
g_general.test_old_behaviour = function(cg)
    t.assert_equals(compat.box_consider_system_spaces_synchronous.default,
                    'old')
    cg.server1:exec(function()
        t.assert_equals(box.info.synchro.queue.owner, 0)
        box.schema.func.call('check_sync_system_spaces', false)
    end)
    cg.server2:exec(function()
        t.assert_equals(box.info.synchro.queue.owner, 0)
        box.schema.func.call('check_sync_system_spaces', false)
    end)

    cg.server1:exec(function()
        box.space._schema:alter{is_sync = true}
        t.assert_equals(box.space._schema.is_sync, true)
        t.assert_equals(box.space._schema.state.is_sync, true)
    end)
    cg.server1:wait_for_downstream_to(cg.server2)
    cg.server2:exec(function()
        t.assert_equals(box.space._schema.is_sync, true)
        t.assert_equals(box.space._schema.state.is_sync, true)
    end)
    cg.server1:exec(function()
        box.space._schema:alter{is_sync = false}
        t.assert_equals(box.space._schema.is_sync, false)
        t.assert_equals(box.space._schema.state.is_sync, false)
    end)
    cg.server1:wait_for_downstream_to(cg.server2)
    cg.server2:exec(function()
        t.assert_equals(box.space._schema.is_sync, false)
        t.assert_equals(box.space._schema.state.is_sync, false)
    end)

    -- Claim the synchronous queue, the system spaces must continue being
    -- asynchronous.
    cg.server1:exec(function()
        box.ctl.promote()
        t.assert_not_equals(box.info.synchro.queue.owner, 0)
        box.schema.func.call('check_sync_system_spaces', false)
    end)
    cg.server1:wait_for_downstream_to(cg.server2)
    cg.server2:exec(function()
        t.assert_not_equals(box.info.synchro.queue.owner, 0)
        box.schema.func.call('check_sync_system_spaces', false)
    end)

    -- Unclaim the synchronous queue, the system spaces must continue being
    -- asynchronous.
    cg.server1:exec(function()
        box.ctl.demote()
        t.assert_equals(box.info.synchro.queue.owner, 0)
        box.schema.func.call('check_sync_system_spaces', false)
    end)
    cg.server1:wait_for_downstream_to(cg.server2)
    cg.server2:exec(function()
        t.assert_equals(box.info.synchro.queue.owner, 0)
        box.schema.func.call('check_sync_system_spaces', false)
    end)

    -- Test that the state of synchronous replication for the system spaces is
    -- enabled by the user-provided 'is_sync'.
    cg.server1:exec(function()
        box.space._schema:alter{is_sync = true}
        t.assert_equals(box.space._schema.is_sync, true)
        t.assert_equals(box.space._schema.state.is_sync, true)
    end)
    cg.server1:wait_for_downstream_to(cg.server2)

    -- Claim the synchronous queue, the system space must continue being
    -- synchronous.
    cg.server1:exec(function()
        box.ctl.promote()
        t.assert_not_equals(box.info.synchro.queue.owner, 0)
        t.assert_equals(box.space._schema.is_sync, true)
        t.assert_equals(box.space._schema.state.is_sync, true)
    end)
    cg.server1:wait_for_downstream_to(cg.server2)
    cg.server2:exec(function()
        t.assert_not_equals(box.info.synchro.queue.owner, 0)
        t.assert_equals(box.space._schema.is_sync, true)
        t.assert_equals(box.space._schema.state.is_sync, true)
    end)

    -- Unclaim the synchronous queue, the system space must continue being
    -- synchronous.
    cg.server1:exec(function()
        box.ctl.demote()
        t.assert_equals(box.info.synchro.queue.owner, 0)
        t.assert_equals(box.space._schema.is_sync, true)
        t.assert_equals(box.space._schema.state.is_sync, true)
    end)
    cg.server1:wait_for_downstream_to(cg.server2)
    cg.server2:exec(function()
        t.assert_equals(box.info.synchro.queue.owner, 0)
        t.assert_equals(box.space._schema.is_sync, true)
        t.assert_equals(box.space._schema.state.is_sync, true)
    end)
end

g_general.before_test('test_new_behaviour', function(cg)
    cg.server1:exec(function()
        require('compat').box_consider_system_spaces_synchronous = 'new'
    end)
    cg.server2:exec(function()
        require('compat').box_consider_system_spaces_synchronous = 'new'
    end)
end)

-- Test the new behaviour: the system spaces become synchronous when the
-- synchronous queue is claimed.
g_general.test_new_behaviour = function(cg)
    cg.server1:exec(function()
        t.assert_equals(box.info.synchro.queue.owner, 0)
        box.schema.func.call('check_sync_system_spaces', false)
    end)
    cg.server2:exec(function()
        t.assert_equals(box.info.synchro.queue.owner, 0)
        box.schema.func.call('check_sync_system_spaces', false)
    end)

    -- Claim the synchronous queue, the system spaces must start being
    -- synchronous.
    cg.server1:exec(function()
        box.ctl.promote()
        t.assert_not_equals(box.info.synchro.queue.owner, 0)
        box.schema.func.call('check_sync_system_spaces', true)
    end)
    cg.server1:wait_for_downstream_to(cg.server2)
    cg.server2:exec(function()
        t.assert_not_equals(box.info.synchro.queue.owner, 0)
        box.schema.func.call('check_sync_system_spaces', true)
    end)

    -- Claim the synchronous queue on the other instance, the system spaces must
    -- continue being synchronous.
    cg.server2:exec(function()
        box.ctl.promote()
        t.assert_not_equals(box.info.synchro.queue.owner, 0)
        box.schema.func.call('check_sync_system_spaces', true)
    end)
    cg.server2:wait_for_downstream_to(cg.server1)
    cg.server1:exec(function()
        t.assert_not_equals(box.info.synchro.queue.owner, 0)
        box.schema.func.call('check_sync_system_spaces', true)
    end)

    -- Unclaim the synchronous queue, the system spaces must stop being
    -- synchronous.
    cg.server2:exec(function()
        box.ctl.demote()
        t.assert_equals(box.info.synchro.queue.owner, 0)
        box.schema.func.call('check_sync_system_spaces', false)
    end)
    cg.server2:wait_for_downstream_to(cg.server1)
    cg.server1:exec(function()
        t.assert_equals(box.info.synchro.queue.owner, 0)
        box.schema.func.call('check_sync_system_spaces', false)
    end)

    -- Test that the state of synchronous replication for the system spaces is
    -- the disjunction of the user-provided 'is_sync' with the synchronous queue
    -- state.
    cg.server1:exec(function()
        box.space._schema:alter{is_sync = true}
        t.assert_equals(box.space._schema.is_sync, true)
        t.assert_equals(box.space._schema.state.is_sync, true)
    end)
    cg.server1:wait_for_downstream_to(cg.server2)

    -- Claim the synchronous queue, the system space must continue being
    -- synchronous.
    cg.server1:exec(function()
        box.ctl.promote()
        t.assert_not_equals(box.info.synchro.queue.owner, 0)
        t.assert_equals(box.space._schema.is_sync, true)
        t.assert_equals(box.space._schema.state.is_sync, true)
    end)
    cg.server1:wait_for_downstream_to(cg.server2)
    cg.server2:exec(function()
        t.assert_not_equals(box.info.synchro.queue.owner, 0)
        t.assert_equals(box.space._schema.is_sync, true)
        t.assert_equals(box.space._schema.state.is_sync, true)
    end)

    -- Unclaim the synchronous queue, the system space must continue being
    -- synchronous.
    cg.server1:exec(function()
        box.ctl.demote()
        t.assert_equals(box.info.synchro.queue.owner, 0)
        t.assert_equals(box.space._schema.is_sync, true)
        t.assert_equals(box.space._schema.state.is_sync, true)
    end)
    cg.server1:wait_for_downstream_to(cg.server2)
    cg.server2:exec(function()
        t.assert_equals(box.info.synchro.queue.owner, 0)
        t.assert_equals(box.space._schema.is_sync, true)
        t.assert_equals(box.space._schema.state.is_sync, true)
    end)
end

g_general.before_test('test_new_behaviour_corner_cases', function(cg)
    cg.server1:exec(function()
        box.cfg{wal_queue_max_size = 1}
        require('compat').box_consider_system_spaces_synchronous = 'new'
    end)
    cg.server2:exec(function()
        box.cfg{wal_queue_max_size = 1}
        require('compat').box_consider_system_spaces_synchronous = 'new'
    end)
end)

-- Test the corner cases of the new behaviour.
g_general.test_new_behaviour_corner_cases = function(cg)
    t.tarantool.skip_if_not_debug()

    cg.server1:exec(function()
        local fiber = require('fiber')

        -- Test that synchronous replication for the system spaces is enabled
        -- once the PROMOTE request is prepared (i.e., before being written to
        -- the WAL).
        box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', 1)
        local f = fiber.new(function()
            box.ctl.promote()
        end)
        f:set_joinable(true)
        t.helpers.retrying({timeout = 60}, function()
            t.assert(box.error.injection.get('ERRINJ_WAL_DELAY'))
        end)
        t.assert_equals(box.info.synchro.queue.owner, 0)
        box.schema.func.call('check_sync_system_spaces', true)
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
        t.assert(f:join())
        t.assert_equals(box.info.synchro.queue.owner, box.info.id)
        box.schema.func.call('check_sync_system_spaces', true)

        -- Test that synchronous replication for the system spaces is disabled
        -- once the DEMOTE request is prepared (i.e., before being written to
        -- the WAL).
        box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', 1)
        f = fiber.new(function()
            box.ctl.demote()
        end)
        f:set_joinable(true)
        t.helpers.retrying({timeout = 60}, function()
            t.assert(box.error.injection.get('ERRINJ_WAL_DELAY'))
        end)
        t.assert_not_equals(box.info.synchro.queue.owner, 0)
        box.schema.func.call('check_sync_system_spaces', false)
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
        t.assert(f:join())
        t.assert_equals(box.info.synchro.queue.owner, 0)
        box.schema.func.call('check_sync_system_spaces', false)
    end)
    cg.server1:wait_for_downstream_to(cg.server2)

    -- Test that synchronous replication for the system spaces is handled
    -- correctly when a PROMOTE request is rolled back and the synchronous queue
    -- is not claimed.
    cg.server2:exec(function()
        t.assert_equals(box.info.synchro.queue.owner, 0)
        box.schema.func.call('check_sync_system_spaces', false)
        box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', 1)
    end)
    local fid = cg.server1:exec(function()
        box.error.injection.set('ERRINJ_RAFT_WAIT_TERM_PERSISTED_DELAY', true)
        local f = require('fiber').create(function()
            box.ctl.promote()
        end)
        f:set_joinable(true)
        return f:id()
    end)
    t.helpers.retrying({timeout = 60}, function()
        t.assert(cg.server2:grep_log('RAFT: persisted state {term: 4}'))
    end)
    cg.server1:exec(function(fid)
        box.error.injection.set('ERRINJ_RAFT_WAIT_TERM_PERSISTED_DELAY', false)
        t.assert(require('fiber').find(fid):join())
    end, {fid})
    cg.server2:exec(function()
        t.helpers.retrying({timeout = 60}, function()
            t.assert(box.error.injection.get('ERRINJ_WAL_DELAY'))
        end)
        t.assert(box.info.synchro.queue.busy)
        t.assert_equals(box.info.synchro.queue.owner, 0)
        box.schema.func.call('check_sync_system_spaces', true)
        box.error.injection.set('ERRINJ_WAL_FALLOCATE', 1)
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
        t.helpers.retrying({timeout = 60}, function()
            t.assert_not(box.info.synchro.queue.busy)
            t.assert_equals(box.info.synchro.queue.owner, 0)
            box.schema.func.call('check_sync_system_spaces', false)
        end)
    end)

    -- Restart replication.
    cg.server2:exec(function(replication)
        box.cfg{replication = ''}
        box.cfg{replication = replication}
        t.helpers.retrying({timeout = 60}, function()
            t.assert_not_equals(box.info.status, "orphan")
        end)
    end, {cg.replication})

    -- Test that synchronous replication for the system spaces is handled
    -- correctly when a PROMOTE request is rolled back and the synchronous queue
    -- is claimed.
    cg.server1:wait_for_downstream_to(cg.server2)
    cg.server2:exec(function()
        box.ctl.promote()
        t.assert_equals(box.info.synchro.queue.owner, box.info.id)
        box.schema.func.call('check_sync_system_spaces', true)
    end)
    cg.server2:wait_for_downstream_to(cg.server1)
    cg.server1:exec(function()
        t.assert_not_equals(box.info.synchro.queue.owner, 0)
        t.assert_not_equals(box.info.synchro.queue.owner, box.info.id)
        box.schema.func.call('check_sync_system_spaces', true)
    end)
    cg.server2:exec(function()
        box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', 1)
    end)
    local fid = cg.server1:exec(function()
        box.error.injection.set('ERRINJ_RAFT_WAIT_TERM_PERSISTED_DELAY', true)
        local f = require('fiber').create(function()
            box.ctl.promote()
        end)
        f:set_joinable(true)
        return f:id()
    end)
    t.helpers.retrying({timeout = 60}, function()
        t.assert(cg.server2:grep_log('RAFT: persisted state {term: 6}'))
    end)
    cg.server1:exec(function(fid)
        box.error.injection.set('ERRINJ_RAFT_WAIT_TERM_PERSISTED_DELAY', false)
        t.assert(require('fiber').find(fid):join())
    end, {fid})
    cg.server2:exec(function()
        t.helpers.retrying({timeout = 60}, function()
            t.assert(box.error.injection.get('ERRINJ_WAL_DELAY'))
        end)
        t.assert(box.info.synchro.queue.busy)
        t.assert_equals(box.info.synchro.queue.owner, box.info.id)
        box.schema.func.call('check_sync_system_spaces', true)
        box.error.injection.set('ERRINJ_WAL_FALLOCATE', 1)
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
        t.helpers.retrying({timeout = 60}, function()
            t.assert_not(box.info.synchro.queue.busy)
            t.assert_equals(box.info.synchro.queue.owner, box.info.id)
            box.schema.func.call('check_sync_system_spaces', true)
        end)
    end)

    -- Restart replication.
    cg.server2:exec(function(replication)
        box.cfg{replication = ''}
        box.cfg{replication = replication}
        t.helpers.retrying({timeout = 60}, function()
            t.assert_not_equals(box.info.status, "orphan")
        end)
    end, {cg.replication})

    -- Test that synchronous replication for the system spaces is handled
    -- correctly when a DEMOTE request is rolled back.
    cg.server1:wait_for_downstream_to(cg.server2)
    cg.server2:exec(function()
        t.assert_not_equals(box.info.synchro.queue.owner, 0)
        box.schema.func.call('check_sync_system_spaces', true)
        box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', 1)
    end)
    fid = cg.server1:exec(function()
        box.error.injection.set('ERRINJ_RAFT_WAIT_TERM_PERSISTED_DELAY', true)
        local f = require('fiber').create(function()
            box.ctl.demote()
        end)
        f:set_joinable(true)
        return f:id()
    end)
    t.helpers.retrying({timeout = 60}, function()
        t.assert(cg.server2:grep_log('RAFT: persisted state {term: 7}'))
    end)
    cg.server1:exec(function(fid)
        box.error.injection.set('ERRINJ_RAFT_WAIT_TERM_PERSISTED_DELAY', false)
        require('fiber').find(fid):join()
    end, {fid})
    cg.server2:exec(function()
        t.helpers.retrying({timeout = 60}, function()
            t.assert(box.error.injection.get('ERRINJ_WAL_DELAY'))
        end)
        t.assert_not_equals(box.info.synchro.queue.owner, 0)
        box.schema.func.call('check_sync_system_spaces', false)
        box.error.injection.set('ERRINJ_WAL_FALLOCATE', 1)
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
        t.helpers.retrying({timeout = 60}, function()
            t.assert_not(box.info.synchro.queue.busy)
            t.assert_not_equals(box.info.synchro.queue.owner, 0)
            box.schema.func.call('check_sync_system_spaces', true)
        end)
    end)
end

-- Test that the transition of the compat option works correctly.
g_general.test_compat_opt_transition = function(cg)
    cg.server1:exec(function()
        t.assert_equals(box.info.synchro.queue.owner, 0)
        box.schema.func.call('check_sync_system_spaces', false)
    end)
    cg.server2:exec(function()
        t.assert_equals(box.info.synchro.queue.owner, 0)
        box.schema.func.call('check_sync_system_spaces', false)
    end)

    cg.server1:exec(function()
        box.ctl.promote()
        t.assert_equals(box.info.synchro.queue.owner, box.info.id)
        box.schema.func.call('check_sync_system_spaces', false)
    end)
    cg.server1:wait_for_downstream_to(cg.server2)
    cg.server2:exec(function()
        t.assert_not_equals(box.info.synchro.queue.owner, 0)
        box.schema.func.call('check_sync_system_spaces', false)
    end)

    cg.server1:exec(function()
        require('compat').box_consider_system_spaces_synchronous = 'new'
        box.schema.func.call('check_sync_system_spaces', true)
    end)
    cg.server2:exec(function()
        require('compat').box_consider_system_spaces_synchronous = 'new'
        box.schema.func.call('check_sync_system_spaces', true)
    end)

    cg.server1:exec(function()
        require('compat').box_consider_system_spaces_synchronous = 'old'
        box.schema.func.call('check_sync_system_spaces', false)
    end)
    cg.server2:exec(function()
        require('compat').box_consider_system_spaces_synchronous = 'old'
        box.schema.func.call('check_sync_system_spaces', false)
    end)

    cg.server1:exec(function()
        require('compat').box_consider_system_spaces_synchronous = 'new'
        box.schema.func.call('check_sync_system_spaces', true)
    end)
    cg.server2:exec(function()
        require('compat').box_consider_system_spaces_synchronous = 'new'
        box.schema.func.call('check_sync_system_spaces', true)
    end)

    cg.server1:exec(function()
        box.ctl.demote()
        t.assert_equals(box.info.synchro.queue.owner, 0)
        box.schema.func.call('check_sync_system_spaces', false)
    end)
    cg.server1:wait_for_downstream_to(cg.server2)
    cg.server2:exec(function()
        t.assert_equals(box.info.synchro.queue.owner, 0)
        box.schema.func.call('check_sync_system_spaces', false)
    end)

    cg.server1:exec(function()
        require('compat').box_consider_system_spaces_synchronous = 'old'
        box.schema.func.call('check_sync_system_spaces', false)
    end)
    cg.server2:exec(function()
        require('compat').box_consider_system_spaces_synchronous = 'old'
        box.schema.func.call('check_sync_system_spaces', false)
    end)
end

g_recovery.before_all(cluster.init)
g_recovery.after_all(cluster.clean)
g_recovery.after_each(cluster.drop)

g_recovery.before_each(function(cg)
    local config = cbuilder.new()
        :add_instance('server', {})
        :set_instance_option('server', 'compat', {
            box_consider_system_spaces_synchronous = 'new',
        })
        :config()
    local cluster = cluster.new(cg, config)
    cluster:start()
    cluster.server:exec(function(check_sync_system_spaces)
        t.assert_equals(require("compat").
                        box_consider_system_spaces_synchronous.current, "new")
        box.schema.func.create('check_sync_system_spaces', {
            body = check_sync_system_spaces,
    })
    end, {check_sync_system_spaces})
end)

-- Test that system spaces are not synchronous after bootstrap.
g_recovery.test_bootstrap = function(cg)
    cg.cluster.server:exec(function()
        box.schema.func.call('check_sync_system_spaces', false)
    end)
end

-- Test that system spaces are synchronous after recovery from an xlog with a
-- promote entry.
g_recovery.test_recovery_from_xlog = function(cg)
    cg.cluster.server:exec(function()
        box.ctl.promote()
        box.schema.func.call('check_sync_system_spaces', true)
    end)
    cg.cluster.server:restart()
    cg.cluster.server:exec(function()
        box.schema.func.call('check_sync_system_spaces', true)
    end)
end

-- Test that system spaces are synchronous after recovery from a snapshot with a
-- promote entry.
g_recovery.test_recovery_from_snap = function(cg)
    cg.cluster.server:exec(function()
        box.ctl.promote()
        box.schema.func.call('check_sync_system_spaces', true)
        box.snapshot()
    end)
    cg.cluster.server:restart()
    cg.cluster.server:exec(function()
        box.schema.func.call('check_sync_system_spaces', true)
    end)
end

g_schema_upgrade.before_each(function(cg)
    treegen.init(cg)

    local rs_uuid = 'd266de7d-b3fb-45d8-b644-580700550614'
    local inst_uuid = 'bb89f474-7ed6-414c-a1d0-da7392c563d7'
    local datadir = 'test/box-luatest/upgrade/2.10.4'
    local dir = treegen.prepare_directory(cg, {}, {})
    local workdir = fio.pathjoin(dir, 'server')
    fio.mktree(workdir)
    fio.copytree(datadir, workdir)

    local config = cbuilder.new()
        :add_instance('server', {
            compat = {box_consider_system_spaces_synchronous = 'new'},
            snapshot = {dir = workdir},
            wal = {dir = workdir},
            database = {
                instance_uuid = inst_uuid, replicaset_uuid = rs_uuid,
                mode = 'ro',
            }})
        :config()
    local cfg = yaml.encode(config)
    local config_file = treegen.write_script(dir, 'cfg.yaml', cfg)
    local opts = {
        env = {LUA_PATH = os.environ()['LUA_PATH']},
        config_file = config_file,
        chdir = dir,
    }
    cg.server = server:new(fun.chain(opts, {alias = 'server'}):tomap())
    cg.server:start()
    cg.server:exec(function()
        t.assert_equals(require("compat").
                        box_consider_system_spaces_synchronous.current, "new")
    end)
end)

-- Test that a system space added after a schema upgrade, while the synchronous
-- queue is claimed, is synchronous.
g_schema_upgrade.test_upgrade_adding_new_space = function(cg)
    cg.server:exec(function(check_sync_system_spaces)
        t.assert_equals(box.space._schema:get{'version'}, {'version', 2, 10, 4})
        t.assert_equals(box.space._vspace_sequence, nil)
        box.cfg{read_only = false}
        box.ctl.promote()
        box.schema.upgrade()
        t.assert_not_equals(box.space._vspace_sequence, nil)
        box.schema.func.create('check_sync_system_spaces', {
            body = check_sync_system_spaces,
        })
        box.schema.func.call('check_sync_system_spaces', true)
    end, {check_sync_system_spaces})
end

g_schema_upgrade.after_each(function(cg)
    cg.server:drop()
    treegen.clean(cg)
end)
