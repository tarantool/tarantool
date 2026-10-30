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
        local err_msg = "Incorrect value for option " ..
            "'txn_synchro_timeout': option is disabled if compat " ..
            "option `replication_synchro_timeout` is set to 'old'"
        t.assert_error_msg_equals(
            err_msg, box.cfg, { txn_synchro_timeout = 0.1, })
    end)
end

local function master_define_check_insert_fails(cg, autocommit)
    cg.master:exec(function(autocommit)
        if autocommit then
            rawset(_G, "check_insert_fails", function(err_msg, ...)
                t.assert_error_msg_equals(
                    err_msg, box.space.test.insert, box.space.test, ...) end)
        else
            rawset(_G, "check_insert_fails", function(err_msg, ...)
                box.begin()
                box.space.test:insert(...)
                t.assert_error_msg_equals(err_msg, box.commit)
            end)
        end
    end, { autocommit })
end

local function check_fiber_detached(cg)
    cg.master:exec(function()
        local prev_cfg = {
            replication_synchro_quorum = box.cfg.replication_synchro_quorum,
            replication_synchro_timeout = box.cfg.replication_synchro_timeout,
        }

        require('compat').replication_synchro_timeout = 'new'
        box.cfg{
            replication_synchro_quorum = 2,
            txn_synchro_timeout = 0.1,
        }
        local err_msg =
            'Quorum collection for a synchronous transaction is ' ..
            'timed out. The transaction is detached from this fiber ' ..
            'and continues waiting for quorum'
        _G.check_insert_fails(err_msg, {1})
        box.cfg(prev_cfg)
    end)
end

local function check_rollback(cg, err_msg)
    cg.master:exec(function(err_msg)
        local prev_cfg = {
            replication_synchro_quorum = box.cfg.replication_synchro_quorum,
            replication_synchro_timeout = box.cfg.replication_synchro_timeout,
        }

        require('compat').replication_synchro_timeout = 'old'
        box.cfg{
            replication_synchro_quorum = 2,
            replication_synchro_timeout = 0.1,
        }

        _G.check_insert_fails(err_msg, {2})
        t.assert_equals(box.space.test:get{2}, nil)

        box.cfg(prev_cfg)
    end, { err_msg })
end

local function wait_confirm_and_truncate(cg)
    cg.master:exec(function(wait_timeout)
        t.helpers.retrying({timeout = wait_timeout},
            function() t.assert_equals(box.info.synchro.queue.len, 0) end)
        t.assert_equals(box.space.test:select{}, { { 1, } })
        box.space.test:delete{1}
    end, { wait_timeout })
end

for _, case in ipairs({ { autocommit = true }, { autocommit = false }, }) do
    local name_suffix = (case.autocommit and "_autocommit" or "")
    g['test_return_if_quorum_collection_is_timed_out' .. name_suffix] =
    function(cg)
        master_define_check_insert_fails(cg, case.autocommit)
        check_fiber_detached(cg)
        wait_confirm_and_truncate(cg)
    end
end

for _, case in ipairs({ { autocommit = true }, { autocommit = false }, }) do
    local name_suffix = (case.autocommit and "_autocommit" or "")
    g['test_no_rollback_if_there_is_no_waiting_fiber' .. name_suffix] =
    function(cg)
        master_define_check_insert_fails(cg, case.autocommit)
        cg.master:exec(function() box.cfg{replication_synchro_quorum = 2,} end)
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
        cg.master:exec(function() box.cfg{replication_synchro_quorum = 1,} end)
        wait_confirm_and_truncate(cg)
    end
end

for _, case in ipairs({ { autocommit = true }, { autocommit = false }, }) do
    local name_suffix = (case.autocommit and "_autocommit" or "")
    g['test_rollback_if_there_is_a_waiting_fiber' .. name_suffix] =
    function(cg)
        master_define_check_insert_fails(cg, case.autocommit)
        -- The first transaction was created and hung waiting for quorum
        -- due to a long `txn_synchro_timeout`. Fiber `f` is alive.
        cg.master:exec(function()
            require('compat').replication_synchro_timeout = 'new'
            box.cfg{
                replication_synchro_quorum = 2,
                txn_synchro_timeout = 100000,
            }
            local err_msg =
                'Quorum collection for a synchronous transaction is timed out'
            rawset(_G, "f", require('fiber')
                .create(_G.check_insert_fails, err_msg, {1}))
            _G.f:set_joinable(true)
        end)
        -- `replication_synchro_timeout` was enabled back and then a new
        -- transaction was created which was immediately rolled back
        -- together with the first one due to `replication_synchro_timeout`.
        local err_msg = 'A rollback for a synchronous transaction is received'
        check_rollback(cg, err_msg)
        -- The first transaction was rolled back.
        cg.master:exec(function()
            t.assert(_G.f:join())
            t.assert_equals(box.space.test:get{1}, nil)
        end)
    end
end

for _, case in ipairs({ { autocommit = true }, { autocommit = false }, }) do
    local name_suffix = (case.autocommit and "_autocommit" or "")
    g['test_release_fiber_on_timeout_reconfig' .. name_suffix] =
    function(cg)
        master_define_check_insert_fails(cg, case.autocommit)
        cg.master:exec(function()
            require('compat').replication_synchro_timeout = 'new'
            box.cfg{
                replication_synchro_quorum = 2,
                txn_synchro_timeout = 100000,
            }
            local err_msg =
                'Quorum collection for a synchronous transaction is ' ..
                'timed out. The transaction is detached from this fiber ' ..
                'and continues waiting for quorum'
            local f = require('fiber')
                .create(_G.check_insert_fails, err_msg, {1})
            f:set_joinable(true)

            require('fiber').sleep(5)
            box.cfg{ txn_synchro_timeout = 0.1, }
            t.assert(f:join())
            t.assert_not_equals(box.space.test:get{1}, nil)

            box.cfg{ replication_synchro_quorum = 1, }
        end)
        wait_confirm_and_truncate(cg)
    end
end
