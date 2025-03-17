local t = require('luatest')
local server = require('luatest.server')

local g = t.group('gh-10181-txn-synchro-timeout')
--
-- gh-10181: txn_synchro_timeout
--
local wait_timeout = 10

g.before_all(function(cg)
    -- Default config
    cg.box_cfg = {
        replication_synchro_quorum = box.NULL,
        replication_synchro_timeout = box.NULL,
        replication_timeout = 0.1,
    }

    cg.master = server:new{
        alias = 'master',
        box_cfg = cg.box_cfg,
    }
    cg.master:start()
    cg.master:exec(function()
        box.ctl.promote()
        box.ctl.wait_rw()
        box.schema.space.create('test', {is_sync = true})
        box.space.test:create_index('pk')
    end)
end)

g.after_all(function(cg)
    cg.master:drop()
end)

-- Set default config
g.after_each(function(cg)
    cg.master:exec(function(box_cfg)
        box.cfg(box_cfg)
    end, { cg.box_cfg })
end)

g.test_txn_synchro_timeout_disabled_with_old_behaviour = function(cg)
    cg.master:exec(function()
        require('compat').replication_synchro_timeout = 'old'
        local err_msg =
            "Incorrect value for option 'txn_synchro_timeout': option is " ..
            "disabled if compat option `replication_synchro_timeout` is set to 'old'"
        t.assert_error_msg_equals(
            err_msg, box.cfg, { txn_synchro_timeout = 0.1, })
    end)
end

local function check_fiber_detached(cg)
    cg.master:exec(function()
        require('compat').replication_synchro_timeout = 'new'
        box.cfg{
            replication_synchro_quorum = 2,
            txn_synchro_timeout = 0.1,
        }
        local err_msg =
            'Quorum collection for a synchronous transaction is timed out. ' ..
            'The transaction is detached from this fiber ' ..
            'and continues waiting for quorum'
        t.assert_error_msg_equals(
            err_msg, box.space.test.insert, box.space.test, {1})
    end)
end

-- Assumptions:
-- `box.cfg.replication_synchro_quorum = 2`
-- `compat.replication_synchro_timeout = 'new'`
local function check_rollback(cg, err_msg)
    cg.master:exec(function(err_msg)
        require('compat').replication_synchro_timeout = 'old'
        box.cfg{ replication_synchro_timeout = 0.1, }

        t.assert_error_msg_equals(
            err_msg, box.space.test.insert, box.space.test, {2})
        t.assert_equals(box.space.test:get{2}, nil)
    end, { err_msg })
end

local function wait_confirm_and_truncate(cg)
    cg.master:exec(function(wait_timeout)
        local lsn = box.info.lsn
        box.cfg{ replication_synchro_quorum = box.NULL, }
        t.helpers.retrying({timeout = wait_timeout},
            function() t.assert(box.info.lsn >= lsn + 1) end)
        box.space.test:delete{1}
    end, { wait_timeout })
end

g.test_return_if_quorum_collection_is_timed_out = function(cg)
    check_fiber_detached(cg)
    -- Set default `replication_synchro_quorum` and wait confirm.
    wait_confirm_and_truncate(cg)
end

g.test_no_rollback_if_there_is_no_waiting_fiber = function(cg)
    -- The user fiber detached from the first transaction
    -- due to `txn_synchro_timeout`.
    check_fiber_detached(cg)
    -- `replication_synchro_timeout` was enabled back and then a new
    -- transaction was created which was immediately rolled back
    -- due to `replication_synchro_timeout`.
    local err_msg =
        'Quorum collection for a synchronous transaction is timed out'
    check_rollback(cg, err_msg)
    -- The first transaction was not rolled back because at that point
    -- the user fiber was already detached.
    wait_confirm_and_truncate(cg)
end

g.test_rollback_if_there_is_a_waiting_fiber = function(cg)
    -- The first transaction was created and hung waiting for quorum
    -- due to a long `txn_synchro_timeout`. Fiber `f` is alive.
    cg.master:exec(function()
        require('compat').replication_synchro_timeout = 'new'
        box.cfg{
            replication_synchro_quorum = 2,
            txn_synchro_timeout = 100000,
        }
        rawset(_G, "f", require('fiber')
            .create(box.space.test.insert, box.space.test, {1}))
        _G.f:set_joinable(true)
    end)
    -- `replication_synchro_timeout` was enabled back and then a new
    -- transaction was created which was immediately rolled back
    -- together with the first one due to `replication_synchro_timeout`.
    local err_msg = 'A rollback for a synchronous transaction is received'
    check_rollback(cg, err_msg)
    -- The first transaction was rolled back.
    cg.master:exec(function()
        local err_msg =
            'Quorum collection for a synchronous transaction is timed out'
        local _, err = _G.f:join()
        t.assert_equals(err.message, err_msg)
        t.assert_equals(box.space.test:get{1}, nil)
        box.cfg{ replication_synchro_quorum = box.NULL, }
    end)
end

g.test_release_fiber_on_timeout_reconfig = function(cg)
    cg.master:exec(function()
        require('compat').replication_synchro_timeout = 'new'
        box.cfg{
            replication_synchro_quorum = 2,
            txn_synchro_timeout = 100000,
        }
        local f = require('fiber').create(
            box.space.test.insert, box.space.test, {1})
        f:set_joinable(true)

        require('fiber').sleep(5)
        box.cfg{ txn_synchro_timeout = 0.1, }
        local err_msg =
            'Quorum collection for a synchronous transaction is timed out. ' ..
            'The transaction is detached from this fiber ' ..
            'and continues waiting for quorum'
        local _, err = f:join()
        t.assert_equals(err.message, err_msg)
        t.assert_not_equals(box.space.test:get{1}, nil)
    end)

    wait_confirm_and_truncate(cg)
end
