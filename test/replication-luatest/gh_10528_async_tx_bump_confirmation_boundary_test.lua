local t = require('luatest')
local server = require('luatest.server')
local replica_set = require('luatest.replica_set')

local g = t.group(nil, t.helpers.matrix{is_local = {false, true}})

g.before_each(function(cg)
    cg.replica_set = replica_set:new{}
    cg.replication = {
        server.build_listen_uri('master', cg.replica_set.id),
        server.build_listen_uri('replica', cg.replica_set.id),
    }
    local box_cfg = {
        wal_cleanup_delay = 0,
        replication = cg.replication,
        replication_timeout = 0.1,
        replication_synchro_timeout = 120,
    }
    cg.master = cg.replica_set:build_and_add_server{
        alias = 'master',
        box_cfg = box_cfg,
    }
    cg.replica = cg.replica_set:build_and_add_server{
        alias = 'replica',
        box_cfg = box_cfg,
    }
    cg.replica_set:start()
    cg.replica_set:wait_for_fullmesh()
    cg.master:exec(function(is_local)
        box.schema.space.create('a', {is_sync = false,
                                      is_local = is_local}):create_index('p')
        box.schema.space.create('s', {is_sync = true}):create_index('p')
        box.ctl.promote()
    end, {cg.params.is_local})
    cg.master:wait_for_downstream_to(cg.replica)
end)

g.after_each(function(cg)
    cg.replica_set:drop()
end)

-- Setup a trivial split-brain situation.
local function setup_trivial_split_brain(cg)
    cg.replica:update_box_cfg{replication = ''}

    cg.master:exec(function()
        box.space.a:replace{0}
    end)
end

-- Trigger the split-brain situation by promoting a node that did not receive
-- the preceding asynchronous transactions.
local function trigger_split_brain(cg)
    cg.replica:exec(function()
        box.ctl.promote()
        t.assert_equals(box.space.a:get{0}, nil)
    end)
end

-- Check that the new leader did not trigger a split-brain on the old leader.
local function check_there_is_no_split_brain(old_leader, new_leader)
    old_leader:exec(function(replica_id)
        t.assert_equals(box.info.replication[replica_id].upstream.status,
                            'follow')
    end, {new_leader:get_instance_id()})
end

-- Check whether the new leader caused a split-brain on the old leader
local function check_for_split_brain(cg)
    if cg.params.is_local then
        cg.master:exec(function()
            t.assert(box.space.a.is_local)
        end)
        cg.replica:wait_for_downstream_to(cg.master)
        check_there_is_no_split_brain(cg.master, cg.replica)
        return
    end
    cg.master:exec(function(replica_id)
        t.helpers.retrying({timeout = 120}, function()
            t.assert_equals(box.info.replication[replica_id].upstream.status,
                            'stopped')
            local msg = "Split-Brain discovered: got a request with lsn " ..
                        "from an already processed range"
            t.assert_equals(box.info.replication[replica_id].upstream.message,
                            msg)
        end)
    end, {cg.replica:get_instance_id()})
end

g.test_basic_async = function(cg)
    setup_trivial_split_brain(cg)
    trigger_split_brain(cg)
    check_for_split_brain(cg)
end

-- Test that asynchronous transactions blocked by synchronous transactions bump
-- the confirmation boundary when the synchronous transactions get confirmed.
g.test_async_after_sync = function(cg)
    t.tarantool.skip_if_not_debug()

    cg.master:exec(function()
        -- Block the WAL on the CONFIRM write.
        box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', 1)

        require('fiber').create(function()
            box.space.s:replace{0}
        end)

        t.helpers.retrying({timeout = 120}, function()
            t.assert_equals(box.error.injection.get('ERRINJ_WAL_DELAY'), true)
        end)
    end)

    cg.replica:exec(function()
        t.assert_equals(box.info.synchro.queue.len, 1)
        t.assert_equals(box.space.s:get{0}, {0})
    end)

    local f_a_id = cg.master:exec(function()
        local f_a = require('fiber').create(function()
            box.space.a:replace{0}
        end)
        f_a:set_joinable(true)
        t.assert_equals(box.info.synchro.queue.len, 2)

        -- Block the WAL on the asynchronous transaction write.
        box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', 0)
        box.error.injection.set('ERRINJ_WAL_DELAY', false)

        t.helpers.retrying({timeout = 120}, function()
            t.assert_equals(box.error.injection.get('ERRINJ_WAL_DELAY'), true)
        end)
        t.assert_equals(box.info.synchro.queue.len, 0)

        return f_a:id()
    end)

    cg.replica:exec(function()
        t.assert_equals(box.info.synchro.queue.len, 0)
        box.cfg{replication = ''}
    end)

    cg.master:exec(function(f_a_id)
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
        t.assert(require('fiber').find(f_a_id):join())
    end, {f_a_id})

    trigger_split_brain(cg)
    check_for_split_brain(cg)
end

-- Test that the confirmation boundary bumped by asynchronous transactions is
-- correctly restored after snapshot recovery.
g.test_snapshot_recovery = function(cg)
    setup_trivial_split_brain(cg)

    cg.master:exec(function()
        box.snapshot()
    end)

    cg.master:restart()
    t.helpers.retrying({timeout = 120}, function()
        cg.master:assert_follows_upstream(cg.replica:get_instance_id())
    end)

    trigger_split_brain(cg)
    check_for_split_brain(cg)
end

-- Test that if a new leader receives an old term synchronous transaction and
-- nops it, the synchronous transaction does not bump the confirmation boundary
-- — otherwise it would cause a split-brain on the new leader later on.
g.nopped_sync_tx_does_not_cause_split_brain = function(cg)
    cg.replica:update_box_cfg{replication = ''}

    local f_id = cg.master:exec(function()
        box.cfg{replication_synchro_quorum = 2}

        local f = require('fiber').create(function()
            box.space.s:replace{0}
        end)
        f:set_joinable(true)
        return f:id()
    end)

    trigger_split_brain(cg)
    cg.replica:update_box_cfg{replication = cg.replication}
    cg.replica_set:wait_for_fullmesh()
    cg.replica:wait_for_downstream_to(cg.master)
    cg.master:exec(function(f_id)
        t.assert_not(require('fiber').find(f_id):join())
    end, {f_id})
    cg.master:exec(function()
        box.ctl.promote()
    end)
    cg.master:wait_for_downstream_to(cg.replica)
    check_there_is_no_split_brain(cg.replica, cg.master)
end

g.before_test('test_async_long_wal_no_split_brain', function(cg)
    cg.master:exec(function()
        box.ctl.demote()
    end)
    cg.master:wait_for_downstream_to(cg.replica)
end)

g.test_async_long_wal_no_split_brain = function(cg)
    t.tarantool.skip_if_not_debug()
    t.skip_if(cg.params.is_local)

    cg.replica:exec(function()
        box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', 0)
    end)
    cg.master:exec(function()
        box.space.a:replace{0}
    end)
    cg.replica:exec(function()
        t.helpers.retrying({timeout = 5}, function()
            t.assert(box.error.injection.get('ERRINJ_WAL_DELAY'))
        end)
    end)
    cg.master:exec(function()
        box.ctl.promote()
    end)
    cg.replica:exec(function()
        t.helpers.retrying({timeout = 5}, function()
            t.assert(box.info.synchro.queue.busy)
        end)
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
    end)
    cg.master:wait_for_downstream_to(cg.replica)
    check_there_is_no_split_brain(cg.replica, cg.master)
end
