local t = require('luatest')
local server = require('luatest.server')
local replica_set = require('luatest.replica_set')

local g = t.group('general')
local g_async_rollback = t.group('async_rollback')
local g_no_synchro_queue_owner = t.group('no_synchro_queue_owner')
local g_is_sync_txs = t.group('is_sync_txs')
local g_fully_local_txs = t.group('fully_local_txs')
local g_option_validation = t.group('option_validation')
local g_option_transition = t.group('option_transition', t.helpers.matrix{
    transition_back_to_rollback = {false, true},
})
local g_async_after_sync = t.group('async_after_sync', t.helpers.matrix{
    option_transition = {'rollback', 'none-rollback', 'rollback-none'},
    transition_back_to_rollback = {false, true},
})
local g_test_evicted_async_txs = t.group('evicted_txs')
local g_test_async_tx_wal_delay = t.group('async_tx_wal_delay')
local g_test_commit_submit_mode = t.group('commit_submit_mode')

local function setup_replica_set(cg)
    cg.replica_set = replica_set:new{}
    cg.box_cfg = {
        -- So that we see committed rather than prepared changes.
        memtx_use_mvcc_engine = true,
        replication = {
            server.build_listen_uri('master', cg.replica_set.id),
            server.build_listen_uri('replica', cg.replica_set.id),
        },
        replication_timeout = 0.1,
        replication_synchro_quorum = 3,
        replication_synchro_timeout = 120,
    }
    cg.master = cg.replica_set:build_and_add_server{
        alias = 'master',
        box_cfg = cg.box_cfg,
    }
    cg.replica = cg.replica_set:build_and_add_server{
        alias = 'replica',
        box_cfg = cg.box_cfg,
    }
    cg.replica_set:start()
    cg.replica_set:wait_for_fullmesh()
    cg.master:exec(function()
        box.cfg{replication_split_brain_handling_mode = 'rollback'}
        box.schema.space.create('a', {is_sync = false}):create_index('p')
        box.schema.space.create('s', {is_sync = true}):create_index('p')
        box.ctl.promote()
    end)
    cg.master:wait_for_downstream_to(cg.replica)
    cg.box_cfg.replication_synchro_quorum = 3
    cg.box_cfg.replication_split_brain_handling_mode = 'rollback'
end

local function setup_async_tx_in_synchro_queue(cg)
    setup_replica_set(cg)

    cg.replica:exec(function()
        t.assert_equals(box.info.synchro.queue.len, 0)
        t.assert_equals(box.info.synchro.queue.confirm_lag, 0)
    end)
    cg.master:exec(function()
        t.assert_equals(box.info.synchro.queue.len, 0)
        t.assert_equals(box.info.synchro.queue.confirm_lag, 0)
        box.space.a:replace{0}
        t.assert_equals(box.info.synchro.queue.len, 1)
        t.assert_equals(box.space.a:get{0}, {0})
    end)
    cg.master:wait_for_downstream_to(cg.replica)
    cg.replica:exec(function()
        t.assert_equals(box.info.synchro.queue.len, 1)
        t.assert_equals(box.space.a:get{0}, {0})
    end)
end

g.before_each(setup_async_tx_in_synchro_queue)

g.after_each(function(cg)
    cg.replica_set:drop()
end)

g.test_async_commit = function(cg)
    local total_commits = function() return box.stat.COMMIT.total end
    local master_commits_before = cg.master:exec(total_commits)
    local replica_commits_before = cg.replica:exec(total_commits)

    cg.master:update_box_cfg{replication_synchro_quorum = ''}
    cg.replica:update_box_cfg{replication_synchro_quorum = ''}

    cg.master:exec(function()
        t.helpers.retrying({timeout = 120}, function()
            t.assert_equals(box.info.synchro.queue.len, 0)
        end)
        t.assert_gt(box.info.synchro.queue.confirm_lag, 0)
    end)
    local master_commits_after = cg.master:exec(total_commits)
    t.assert_equals(master_commits_after, master_commits_before + 1)
    cg.replica:exec(function()
        t.helpers.retrying({timeout = 120}, function()
            t.assert_equals(box.info.synchro.queue.len, 0)
        end)
        t.assert_gt(box.info.synchro.queue.confirm_lag, 0)
    end)
    local replica_commits_after = cg.replica:exec(total_commits)
    t.assert_equals(replica_commits_after, replica_commits_before + 1)
end

g.test_synchro_timeout = function(cg)
    cg.master:exec(function()
        local fiber = require('fiber')

        t.assert_equals(box.info.synchro.queue.len, 1)
        local f1 = fiber.create(function()
            box.space.s:replace{0}
        end)
        f1:set_joinable(true)
        local f2 = fiber.create(function()
            box.space.s:replace{1}
        end)
        f2:set_joinable(true)
        t.assert_equals(box.info.synchro.queue.len, 3)

        box.cfg{replication_synchro_timeout = 0.01}

        local ok, err = f1:join()
        t.assert_not(ok)
        local msg = "Quorum collection for a synchronous transaction is " ..
                    "timed out"
        t.assert_str_contains(err.message, msg)
        ok, err = f2:join()
        msg = "A rollback for a synchronous transaction is received"
        t.assert_not(ok)
        t.assert_str_contains(err.message, msg)

        t.assert_equals(box.info.synchro.queue.len, 1)
        t.assert_equals(box.space.a:get{0}, {0})
    end)
end

g.test_recovery = function(cg)
    cg.master:restart{box_cfg = cg.box_cfg}
    cg.replica_set:wait_for_fullmesh()
    cg.master:exec(function()
        t.assert_equals(box.info.synchro.queue.len, 1)
        t.assert_equals(box.space.a:get{0}, {0})
    end)

    cg.replica:restart()
    cg.replica_set:wait_for_fullmesh()
    cg.replica:exec(function()
        t.assert_equals(box.info.synchro.queue.len, 1)
        t.assert_equals(box.space.a:get{0}, {0})
    end)

    cg.master:update_box_cfg{
        replication_synchro_timeout = 0.01,
        replication_synchro_quorum = '',
    }
    cg.replica:update_box_cfg{replication_synchro_quorum = ''}
    cg.master:exec(function()
        box.ctl.promote()
    end)

    cg.master:exec(function()
        t.helpers.retrying({timeout = 120}, function()
            t.assert_equals(box.info.synchro.queue.len, 0)
        end)
    end)
    cg.replica:exec(function()
        t.helpers.retrying({timeout = 120}, function()
            t.assert_equals(box.info.synchro.queue.len, 0)
        end)
    end)

    -- A confirm is not written for asynchronous transactions, so after recovery
    -- they remain in the limbo waiting for confirmation by a quorum of vclocks.
    cg.master:exec(function()
        box.space.a:replace{1}
        t.helpers.retrying({timeout = 120}, function()
            t.assert_equals(box.info.synchro.queue.len, 0)
            t.assert_equals(box.space.a:get{1}, {1})
        end)
    end)
    cg.replica:exec(function()
        t.helpers.retrying({timeout = 120}, function()
            t.assert_equals(box.info.synchro.queue.len, 0)
            t.assert_equals(box.space.a:get{1}, {1})
        end)
    end)
    cg.master:restart{box_cfg = cg.box_cfg}
    cg.replica_set:wait_for_fullmesh()
    cg.master:exec(function()
        t.assert_equals(box.info.synchro.queue.len, 1)
        t.assert_equals(box.space.a:get{1}, {1})
    end)
    cg.replica:exec(function()
        t.assert_equals(box.info.synchro.queue.len, 0)
    end)

    -- Confirm the asynchronous transaction together with a synchronous one, in
    -- this case a confirm should be written.
    cg.master:update_box_cfg{
        replication_synchro_timeout = 0.01,
        replication_synchro_quorum = '',
    }
    cg.replica:update_box_cfg{replication_synchro_quorum = ''}
    cg.master:exec(function()
        box.ctl.promote()
    end)

    cg.master:update_box_cfg{
        replication_synchro_timeout = 120,
        replication_synchro_quorum = 3,
    }
    cg.replica:update_box_cfg{replication_synchro_quorum = 3}

    cg.master:exec(function()
        box.space.a:replace{2}
        local f_s = require('fiber').create(function()
            box.space.s:replace{0}
        end)
        f_s:set_joinable(true)
        t.helpers.retrying({timeout = 120}, function()
            t.assert_equals(box.info.synchro.queue.len, 2)
        end)

        box.cfg{replication_synchro_quorum = ''}
        t.assert(f_s:join())
        t.assert_equals(box.info.synchro.queue.len, 0)
    end)
    cg.master:restart{box_cfg = cg.box_cfg}
    cg.replica_set:wait_for_fullmesh()
    cg.master:exec(function()
        t.assert_equals(box.info.synchro.queue.len, 0)
    end)
end

-- Test the eviction of old asynchronous transactions from a full synchro queue.
g.test_full_synchro_queue = function(cg)
    cg.master:exec(function()
        box.cfg{replication_synchro_queue_max_size = 1}
        t.assert_gt(box.info.synchro.queue.size,
                    box.cfg.replication_synchro_queue_max_size)
    end)
    cg.replica:exec(function()
        box.cfg{replication_synchro_queue_max_size = 1}
        t.assert_gt(box.info.synchro.queue.size,
                    box.cfg.replication_synchro_queue_max_size)
    end)
    cg.master:exec(function()
        box.space.a:replace{1}
        t.assert_equals(box.info.synchro.queue.len, 1)
        t.assert_equals(box.space.a:get{1}, {1})
    end)
    cg.master:wait_for_downstream_to(cg.replica)
    cg.replica:exec(function()
        t.assert_equals(box.info.synchro.queue.len, 1)
        t.assert_equals(box.space.a:get{1}, {1})
    end)

    -- Test that we get an error if the synchro queue is still full after
    -- eviction.
    cg.master:exec(function()
        box.cfg{replication_synchro_queue_max_size = 0}
        require('fiber').create(function()
            box.space.s:replace{0}
        end)
        box.cfg{replication_synchro_queue_max_size = 1}
        local msg = 'The synchronous transaction queue is full'
        t.assert_error_msg_content_equals(msg, function()
            box.space.a:replace{0}
        end)
    end)

    -- Test that eviction works during recovery.
    local box_cfg = table.copy(cg.box_cfg)
    box_cfg.replication_synchro_queue_max_size = 1

    cg.master:restart{box_cfg = box_cfg}
    cg.replica_set:wait_for_fullmesh()
    cg.master:exec(function()
        t.assert_equals(box.info.synchro.queue.len, 1)
    end)

    cg.replica:restart{box_cfg = box_cfg}
    cg.replica_set:wait_for_fullmesh()
    cg.replica:exec(function()
        t.assert_equals(box.info.synchro.queue.len, 1)
    end)
end

g_async_rollback.before_each(function(cg)
    setup_replica_set(cg)

    cg.replica:exec(function()
        box.cfg{replication = ''}
    end)
    cg.master:exec(function()
        box.space.a:replace{0}
    end)
end)

g_async_rollback.after_each(function(cg)
    cg.replica_set:drop()
end)

g_async_rollback.test_rollback = function(cg)
    local total_rollbacks = function() return box.stat.ROLLBACK.total end
    local master_rollbacks_before = cg.master:exec(total_rollbacks)
    cg.replica:exec(function()
        box.ctl.promote()
    end)
    cg.master:exec(function()
        t.helpers.retrying({timeout = 120}, function()
            t.assert_equals(box.info.synchro.queue.len, 0)
        end)
        t.assert_equals(box.info.synchro.queue.confirm_lag, 0)
        t.assert_equals(box.space.a:get{0}, nil)
    end)
    local master_rollbacks_after = cg.master:exec(total_rollbacks)
    t.assert_equals(master_rollbacks_after, master_rollbacks_before + 1)
end

g_no_synchro_queue_owner.before_each(setup_replica_set)

g_no_synchro_queue_owner.after_each(function(cg)
    cg.replica_set:drop()
end)

g_no_synchro_queue_owner.test_old_behavior = function(cg)
    cg.master:exec(function()
        box.ctl.demote()
        t.assert_equals(box.info.synchro.queue.len, 0)
        box.space.a:replace{0}
        t.assert_equals(box.info.synchro.queue.len, 0)
        t.assert_equals(box.space.a:get{0}, {0})
    end)
end

g_is_sync_txs.before_each(setup_replica_set)

g_is_sync_txs.after_each(function(cg)
    cg.replica_set:drop()
end)

g_is_sync_txs.test_not_affected = function(cg)
    cg.master:exec(function()
        local fiber = require('fiber')

        local f_a = fiber.create(function()
            box.begin{is_sync = true}
            box.space.a:replace{0}
            box.commit()
        end)
        f_a:set_joinable(true)

        t.assert_equals(box.info.synchro.queue.len, 1)
        t.assert_equals(box.space.a:get{0}, nil)

        box.cfg{replication_synchro_quorum = ''}

        f_a:join()
        t.assert_equals(box.info.synchro.queue.len, 0)
        t.assert_equals(box.space.a:get{0}, {0})
    end)
end

g_fully_local_txs.before_each(setup_replica_set)

g_fully_local_txs.after_each(function(cg)
    cg.replica_set:drop()
end)

g_fully_local_txs.test_not_affected = function(cg)
    cg.master:exec(function()
        local fiber = require('fiber')

        box.cfg{replication_split_brain_handling_mode = 'none'}
        box.schema.space.create('l', {is_local = true}):create_index('p')
        box.cfg{replication_split_brain_handling_mode = 'rollback'}

        t.assert_equals(box.info.synchro.queue.len, 0)
        box.space.l:replace{0}
        t.assert_equals(box.info.synchro.queue.len, 0)
        t.assert_equals(box.space.l:get{0}, {0})

        fiber.create(function()
            box.space.s:replace{0}
        end)
        t.assert_equals(box.info.synchro.queue.len, 1)
        fiber.create(function()
            box.space.l:replace{1}
        end)
        t.assert_equals(box.info.synchro.queue.len, 2)
        t.assert_equals(box.space.l:get{1}, nil)

        box.cfg{replication_synchro_quorum = ''}

        t.helpers.retrying({timeout = 120}, function()
            t.assert_equals(box.info.synchro.queue.len, 0)
        end)
        t.assert_equals(box.space.l:get{1}, {1})
    end)
end

g_option_validation.before_each(setup_replica_set)

g_option_validation.after_each(function(cg)
    cg.replica_set:drop()
end)

g_option_validation.test_option_validation = function(cg)
    cg.replica:exec(function()
        t.assert_equals(box.cfg.replication_split_brain_handling_mode, 'none')
        local valid_option_values = {'none', 'rollback'}
        local invalid_option_values = {777, 'split_brain'}
        for _, opt in ipairs(valid_option_values) do
            box.cfg{replication_split_brain_handling_mode = opt}
        end
        local msg =
            "Incorrect value for option 'replication_split_brain_handling_mode'"
        for _, opt in ipairs(invalid_option_values) do
            t.assert_error_msg_contains(msg, function()
                box.cfg{replication_split_brain_handling_mode = opt}
            end)
        end
    end)
end

local function test_confirmation_of_old_async_tx(transition_back_to_rollback)
    local fiber = require('fiber')

    local synchro_queue_entry_size = box.info.synchro.queue.size

    box.space.a:replace{1}
    t.assert_equals(box.info.synchro.queue.len, 2)

    box.cfg{replication_split_brain_handling_mode = 'none'}

    -- A synchronous transaction does not confirm old asynchronous transactions.
    fiber.create(function()
        box.space.s:replace{0}
    end)
    t.assert_equals(box.info.synchro.queue.len, 3)

    -- Make sure that all old asynchronous transactions are confirmed, not just
    -- the ones that free the limbo space up.
    box.cfg{replication_synchro_queue_max_size = synchro_queue_entry_size * 3}
    t.assert_equals(box.info.synchro.queue.size,
                    box.cfg.replication_synchro_queue_max_size)

    if transition_back_to_rollback then
        box.cfg{replication_split_brain_handling_mode = 'rollback'}
    end

    fiber.create(function()
        box.space.a:replace{2}
    end)
    -- Either 1 entry will be evicted to make space, or 2 entries will be
    -- confirmed.
    transition_back_to_rollback = (transition_back_to_rollback and 1) or 0
    t.assert_equals(box.info.synchro.queue.len, 2 + transition_back_to_rollback)

    box.cfg{replication_synchro_quorum = ''}

    t.helpers.retrying({timeout = 120}, function()
        t.assert_equals(box.info.synchro.queue.len, 0)
    end)
end

g_option_transition.before_each(setup_async_tx_in_synchro_queue)

g_option_transition.after_each(function(cg)
    cg.replica_set:drop()
end)

-- Test the option transition from 'rollback' to 'none'. The case when an
-- asynchronous transaction is blocked by a synchronous transaction is tested
-- below in `test_async_after_sync`.
g_option_transition.test_option_transition_from_rollback_to_none = function(cg)
    cg.master:exec(test_confirmation_of_old_async_tx,
                   {cg.params.transition_back_to_rollback})
end

g_async_after_sync.before_each(function(cg)
    t.tarantool.skip_if_not_debug()

    setup_replica_set(cg)

    cg.master:exec(function(option_transition)
        if option_transition == 'rollback' or
           option_transition == 'rollback-none' then
           box.cfg{replication_split_brain_handling_mode = 'rollback'}
        else
            box.cfg{replication_split_brain_handling_mode = 'none'}
        end

        box.cfg{replication_synchro_quorum = ''}

        -- Block the WAL on the confirm write.
        box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', 1)

        require('fiber').create(function()
            box.space.s:replace{0}
        end)

        t.helpers.retrying({timeout = 120}, function()
            t.assert_equals(box.error.injection.get('ERRINJ_WAL_DELAY'), true)
        end)
    end, {cg.params.option_transition})
    cg.replica:exec(function()
        t.helpers.retrying({timeout = 120}, function()
            t.assert_equals(box.info.synchro.queue.len, 1)
        end)
    end)
    cg.f_a_id = cg.master:exec(function()
        box.cfg{replication_synchro_quorum = 3}

        local f_a = require('fiber').create(function()
            box.space.a:replace{0}
        end)
        f_a:set_joinable(true)

        t.assert_equals(box.info.synchro.queue.len, 2)
        t.assert_equals(box.space.a:get{0}, nil)

        return f_a:id()
    end)
end)

g_async_after_sync.after_each(function(cg)
    cg.replica_set:drop()
end)

g_async_after_sync.test_async_after_sync = function(cg)
    cg.master:exec(function(f_a_id, option_transition)
        local fiber = require('fiber')

        if option_transition == 'none-rollback' then
            box.cfg{replication_split_brain_handling_mode = 'rollback'}
        elseif option_transition == 'rollback-none' then
            box.cfg{replication_split_brain_handling_mode = 'none'}
        end

        box.error.injection.set('ERRINJ_WAL_DELAY', false)

        t.assert(fiber.join(fiber.find(f_a_id)))

        t.assert_equals(box.info.synchro.queue.len, 1)
        t.assert_equals(box.space.a:get{0}, {0})
    end, {cg.f_a_id, cg.params.option_transition})
    -- By the time we made the option transition, the asynchronous transition
    -- has already been submitted to the WAL, so it was replicated as a regular
    -- asynchronous transaction.
    if cg.params.option_transition == 'none-rollback' then
        cg.replica:exec(function()
           t.helpers.retrying({timeout = 120}, function()
               t.assert_equals(box.info.synchro.queue.len, 0)
               t.assert_equals(box.space.a:get{0}, {0})
            end)
        end)
        return
    end

    cg.replica:exec(function()
       t.helpers.retrying({timeout = 120}, function()
           t.assert_equals(box.info.synchro.queue.len, 1)
           t.assert_equals(box.space.a:get{0}, {0})
        end)
    end)

    if cg.params.option_transition == 'rollback-none' then
        cg.master:update_box_cfg{
            replication_split_brain_handling_mode = 'rollback',
        }
        cg.master:exec(test_confirmation_of_old_async_tx,
                       {cg.params.transition_back_to_rollback})
    end
end

g_test_evicted_async_txs.before_each(setup_replica_set)

g_test_evicted_async_txs.after_each(function(cg)
    cg.replica_set:drop()
end)

g_test_evicted_async_txs.test_split_brain = function(cg)
    cg.replica:update_box_cfg{replication = ''}

    cg.master:exec(function()
        box.cfg{replication_synchro_queue_max_size = 1}

        box.space.a:replace{0}
        t.assert_equals(box.info.synchro.queue.len, 1)
        t.assert_gt(box.info.synchro.queue.size,
                    box.cfg.replication_synchro_queue_max_size)
        box.space.a:replace{1}
        t.assert_equals(box.info.synchro.queue.len, 1)
    end)

    cg.replica:exec(function()
        t.assert_equals(box.info.synchro.queue.len, 0)
        box.ctl.promote()
    end)
    cg.master:exec(function(replica_id)
        t.helpers.retrying({timeout = 120}, function()
            t.assert_equals(box.info.replication[replica_id].upstream.status,
                            'stopped')
            local msg = "Split-Brain discovered: got a request with lsn " ..
                        "from an already processed range"
            t.assert_equals(box.info.replication[replica_id].upstream.message,
                            msg)
        end)
    end, {cg.replica:exec(function() return box.info.id end)})
end

g_test_async_tx_wal_delay.before_each(setup_replica_set)

g_test_async_tx_wal_delay.after_each(function(cg)
    cg.replica_set:drop()
end)

g_test_async_tx_wal_delay.test_async_tx_not_confirmed = function(cg)
    t.tarantool.skip_if_not_debug()

    cg.master:exec(function()
        local fiber = require('fiber')

        box.error.injection.set('ERRINJ_WAL_DELAY', true)
        box.cfg{replication_synchro_queue_max_size = 1}

        fiber.create(function() box.space.a:replace{0} end)
        t.assert_equals(box.info.synchro.queue.len, 1)
        t.assert_gt(box.info.synchro.queue.size,
                    box.cfg.replication_synchro_queue_max_size)

        local msg = 'The synchronous transaction queue is full'
        t.assert_error_msg_content_equals(msg, function()
            box.space.a:replace{1}
        end)
        t.assert_equals(box.info.synchro.queue.len, 1)

        box.cfg{
            replication_synchro_queue_max_size = 0,
            replication_split_brain_handling_mode = 'none',
        }

        local f_a = fiber.create(function() box.space.a:replace{1} end)
        f_a:set_joinable(true)
        t.assert_equals(box.info.synchro.queue.len, 1)

        box.error.injection.set('ERRINJ_WAL_DELAY', false)

        t.assert(f_a:join())
        t.assert_equals(box.info.synchro.queue.len, 1)

        box.cfg{replication_synchro_quorum = ''}
        t.helpers.retrying({timeout = 120}, function()
            t.assert_equals(box.info.synchro.queue.len, 0)
        end)
    end)
end

g_test_commit_submit_mode.before_each(setup_replica_set)

g_test_commit_submit_mode.after_each(function(cg)
    cg.replica_set:drop()
end)

g_test_commit_submit_mode.test_not_affected = function(cg)
    cg.master:exec(function()
        box.begin()
        box.space.a:replace{0}
        box.commit{wait = 'submit'}
        t.assert_equals(box.info.synchro.queue.len, 1)

        box.cfg{replication_synchro_quorum = ''}
        t.helpers.retrying({timeout = 120}, function()
            t.assert_equals(box.info.synchro.queue.len, 0)
        end)
        t.assert_equals(box.space.a:get{0}, {0})
    end)
end
