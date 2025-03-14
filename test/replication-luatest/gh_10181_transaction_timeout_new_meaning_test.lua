local t = require('luatest')
local server = require('luatest.server')

local g = t.group('gh-10181-transaction-timeout-new-meaning')
--
-- gh-10181: transaction timeout new meaning
--
local wait_timeout = 10

g.before_all(function(cg)
    cg.master = server:new{
        alias = 'master',
        box_cfg = {
            replication_timeout = 0.1,
            memtx_use_mvcc_engine = true,
        },
        env = {
            TARANTOOL_RUN_BEFORE_BOX_CFG = [[
                require('compat')({
                    replication_synchro_timeout = 'new',
                    box_begin_timeout_meaning = 'new',
                })
            ]]
        },
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

local err_msg_rollback = 'Transaction has been aborted by timeout'
local err_msg_return = 'Transaction timeout has expired during commit. ' ..
    'It\'s not aborted and continues to commit in background.'

g.test_rollback_if_timeout_expired_at_yield = function(cg)
    cg.master:exec(function(err_msg)
        t.assert_error_msg_equals(err_msg, function()
            box.begin({timeout = 0.05}) require('fiber').sleep(0.5) box.commit()
        end)
    end, {err_msg_rollback})
end

g.test_rollback_if_timeout_expired_at_yield_in_before_commit_trigger =
function(cg)
    cg.master:exec(function(err_msg)
        local trigger = require('trigger')
        trigger.set('box.before_commit.space.test', 't',
            function() require('fiber').sleep(0.5) end)
        t.assert_error_msg_equals(err_msg, function()
            box.begin({timeout = 0.05}) box.space.test:insert{1} box.commit()
        end)
        trigger.del('box.before_commit.space.test', 't')
    end, {err_msg_rollback})
end

for _, case in ipairs({
    {
        skip_if_not_debug = true,
        preamble_cb = function()
            box.error.injection.set('ERRINJ_WAL_DELAY', true)
        end,
        epilogue_cb = function()
            box.error.injection.set('ERRINJ_WAL_DELAY', false)
        end,
        expired_at = 'WAL write',
    },
    {
        skip_if_not_debug = false,
        preamble_cb = function()
            box.cfg{replication_synchro_quorum = 2}
        end,
        epilogue_cb = function()
            box.cfg{replication_synchro_quorum = box.NULL}
        end,
        expired_at = 'quorum collection',
    },
    {
        skip_if_not_debug = true,
        preamble_cb = function()
            box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', 1)
        end,
        epilogue_cb = function()
            box.error.injection.set('ERRINJ_WAL_DELAY', false)
        end,
        expired_at = 'CONFIRM write',
    },
}) do
    local case_name_suffix = string.gsub(case.expired_at, ' ', '_')
    g['test_return_if_timeout_expired_at_' .. case_name_suffix] = function(cg)
        if case.skip_if_not_debug then
            t.tarantool.skip_if_not_debug()
        end
        cg.master:exec(case.preamble_cb)
        cg.master:exec(function(expired_at, err_msg)
            rawset(_G, 'lsn', box.info.lsn)
            local ok, err = pcall(function()
                box.begin({timeout = 0.5})
                box.space.test:insert{1}
                box.commit()
            end)
            t.assert(not ok)
            t.assert_equals(err.type, 'ClientError')
            t.assert_equals(err.message, err_msg)
            t.assert_equals(err.expired_at, expired_at)
        end, {case.expired_at, err_msg_return})
        cg.master:exec(case.epilogue_cb)
        cg.master:exec(function(wait_timeout)
            t.helpers.retrying({timeout = wait_timeout},
                function() t.assert(box.info.lsn >= _G.lsn + 1) end)
            box.space.test:delete{1}
        end, {wait_timeout})
    end
end
