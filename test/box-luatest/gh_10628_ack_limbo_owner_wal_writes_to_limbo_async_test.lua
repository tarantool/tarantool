local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_each(function(cg)
    cg.s = server:new({
        alias = 'default',
        box_cfg = {
            replication_synchro_timeout = 120,
        },
    })
    cg.s:start()
    cg.s:exec(function()
        rawset(_G, 'fiber', require('fiber'))

        box.schema.create_space('s', {is_sync = true}):create_index('pk')
        box.ctl.promote()
    end)
end)

g.after_each(function(cg)
    cg.s:drop()
end)

-- Test that CONFIRM request WAL write failure is retried.
g.test_confirm_write_failure = function(cg)
    t.tarantool.skip_if_not_debug()

    local wal_error_occurred = false
    cg.s.net_box:watch('box.wal_error', function(k, v)
        t.assert_equals(k, 'box.wal_error')
        if v ~= nil then
           wal_error_occurred = true
        end
    end)

    cg.s:exec(function()
        -- Make the CONFIRM request WAL write fail.
        box.error.injection.set('ERRINJ_WAL_IO_COUNTDOWN', 1)
        local f = _G.fiber.create(function() box.space.s:replace{0} end)
        f:set_joinable(true)

        t.helpers.retrying({timeout = 120}, function()
            t.assert(box.error.injection.get('ERRINJ_WAL_IO'))
        end)

        -- Allow the CONFIRM request WAL write to succeed.
        box.error.injection.set('ERRINJ_WAL_IO', false)
        t.assert(f:join())
    end)
    cg.s:grep_log('txn_limbo_worker .+ ER_WAL_IO: Failed to write to disk')
    t.assert(wal_error_occurred)
end

-- Test that CONFIRM request WAL write concurrently with new transaction
-- confirmation works correctly.
g.test_confirm_write_failure = function(cg)
    t.tarantool.skip_if_not_debug()

    cg.s:exec(function()
        -- Make the CONFIRM request WAL write hang.
        box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', 1)
        -- Bump the volatile confirmed LSN.
        local f1 = _G.fiber.create(function() box.space.s:replace{0} end)
        f1:set_joinable(true)
        t.helpers.retrying({timeout = 120}, function()
            t.assert_equals(box.info.synchro.queue.len, 1)
            t.assert(box.error.injection.get('ERRINJ_WAL_DELAY'))
        end)

        -- Make the limbo worker hang after the CONFIRM request WAL write
        -- finishes.
        box.error.injection.set('ERRINJ_TXN_LIMBO_WORKER_DELAY', true)

        -- Bump the volatile confirmed LSN concurrently.
        local f2 = _G.fiber.create(function() box.space.s:replace{0} end)
        f2:set_joinable(true)
        t.helpers.retrying({timeout = 120}, function()
            t.assert_equals(box.info.synchro.queue.len, 2)
        end)

        local lsn = box.info.lsn
        -- Allow the WAL writes to proceed.
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
        t.helpers.retrying({timeout = 120}, function()
            t.assert_equals(box.info.lsn, lsn + 2)
        end)

        -- Allow the limbo's worker fiber to proceed.
        box.error.injection.set('ERRINJ_TXN_LIMBO_WORKER_DELAY', false)

        t.assert(f1:join())
        t.assert(f2:join())
    end)
end
