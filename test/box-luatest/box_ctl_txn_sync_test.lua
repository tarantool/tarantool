local t = require('luatest')
local server = require('luatest.server')

local g = t.group('txn_sync')

g.before_all(function(cg)
    cg.server = server:new({box_cfg = {
        replication_synchro_timeout = 120,
        election_mode = 'manual',
    }})
    cg.server:start()
    cg.server:exec(function()
        box.ctl.promote()
        box.ctl.wait_rw()
        local sync = box.schema.space.create('sync', {is_sync = true})
        sync:create_index('pk')
        local async = box.schema.space.create('async')
        async:create_index('pk')
    end)
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.test_empty_and_active_transaction = function(cg)
    cg.server:exec(function()
        t.assert_equals({box.ctl.txn_sync()}, {})
        box.begin()
        t.assert_error_covers({code = box.error.ACTIVE_TRANSACTION},
                              box.ctl.txn_sync)
        t.assert(box.is_in_txn())
        box.rollback()
    end)
end

--
-- Start three transactions with WAL writes blocked and both queue limits
-- set to 1 byte. The first transaction fills the queue; the other two must
-- wait for space before they can be submitted to WAL.
--
-- In the sync case, they wait for space in the synchronous queue (limbo).
-- In the async case, they bypass the empty limbo and wait for space in the
-- WAL queue.
--
-- txn_sync() must wait for all three transactions to commit, including the
-- two that have not yet been submitted to WAL.
--
for _, name in ipairs({'sync', 'async'}) do
    g['test_queue_' .. name] = function(cg)
        t.tarantool.skip_if_not_debug()
        cg.server:exec(function(space)
            local fiber = require('fiber')
            local synchro_queue_max_size =
                box.cfg.replication_synchro_queue_max_size
            local wal_queue_max_size = box.cfg.wal_queue_max_size
            box.cfg{
                replication_synchro_queue_max_size = 1,
                wal_queue_max_size = 1,
            }
            box.error.injection.set('ERRINJ_WAL_DELAY', true)
            local writers = {}
            for i = 1, 3 do
                writers[i] = fiber.create(function()
                    fiber.self():set_joinable(true)
                    box.space[space]:insert{i}
                end)
            end
            local done = false
            local waiter = fiber.create(function()
                fiber.self():set_joinable(true)
                box.ctl.txn_sync()
                done = true
            end)
            t.assert_not(done)
            box.error.injection.set('ERRINJ_WAL_DELAY', false)
            t.assert(waiter:join())
            for _, f in ipairs(writers) do
                t.assert(f:join())
            end
            t.assert_equals(box.space[space]:count(), 3)
            t.assert_equals(box.info.synchro.queue.len, 0)
            box.cfg{
                replication_synchro_queue_max_size = synchro_queue_max_size,
                wal_queue_max_size = wal_queue_max_size,
            }
            box.space[space]:truncate()
        end, {name})
    end
end

g.test_confirmation = function(cg)
    t.tarantool.skip_if_not_debug()
    cg.server:exec(function()
        local fiber = require('fiber')
        local quorum = box.cfg.replication_synchro_quorum
        box.cfg{replication_synchro_quorum = 2}
        box.begin()
        box.space.sync:insert{1}
        box.commit{wait = 'submit'}
        box.ctl.wal_sync()
        local done = false
        local waiter = fiber.create(function()
            fiber.self():set_joinable(true)
            box.ctl.txn_sync()
            done = true
        end)
        t.assert_not(done)
        box.error.injection.set('ERRINJ_WAL_DELAY', true)
        box.cfg{replication_synchro_quorum = 1}
        fiber.yield()
        t.assert_not(done)
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
        t.assert(waiter:join())
        t.assert_equals(box.info.synchro.queue.len, 0)
        box.cfg{replication_synchro_quorum = quorum}
        box.space.sync:truncate()
    end)
end

g.test_cancellation_and_flush_error = function(cg)
    t.tarantool.skip_if_not_debug()
    cg.server:exec(function()
        local fiber = require('fiber')
        local quorum = box.cfg.replication_synchro_quorum
        box.cfg{replication_synchro_quorum = 2}
        box.begin()
        box.space.sync:insert{1}
        box.commit{wait = 'submit'}
        box.ctl.wal_sync()
        local waiter = fiber.create(function()
            fiber.self():set_joinable(true)
            box.ctl.txn_sync()
        end)
        waiter:cancel()
        local ok, err = waiter:join()
        t.assert_not(ok)
        t.assert_equals(err.type, 'FiberIsCancelled')
        box.error.injection.set('ERRINJ_WAL_SYNC', true)
        t.assert_error_covers({code = box.error.INJECTION}, box.ctl.txn_sync)
        box.error.injection.set('ERRINJ_WAL_SYNC', false)
        box.cfg{replication_synchro_quorum = 1}
        box.ctl.txn_sync()
        t.assert_equals(box.space.sync:get{1}, {1})
        box.cfg{replication_synchro_quorum = quorum}
        box.space.sync:truncate()
    end)
end

g.test_timeout = function(cg)
    cg.server:exec(function()
        local quorum = box.cfg.replication_synchro_quorum
        local timeout = box.cfg.replication_synchro_timeout
        box.cfg{
            replication_synchro_quorum = 2,
            replication_synchro_timeout = 0.01
        }
        box.begin()
        box.space.sync:insert{1}
        box.commit{wait = 'submit'}
        t.assert_error_covers({type = 'TimedOut'}, box.ctl.txn_sync)
        box.cfg{replication_synchro_quorum = 1}
        box.ctl.txn_sync()
        box.cfg{
            replication_synchro_quorum = quorum,
            replication_synchro_timeout = timeout,
        }
        box.space.sync:truncate()
    end)
end

g.test_wal_rollback = function(cg)
    t.tarantool.skip_if_not_debug()
    cg.server:exec(function()
        local fiber = require('fiber')
        box.error.injection.set('ERRINJ_WAL_DELAY', true)
        local writer = fiber.create(function()
            fiber.self():set_joinable(true)
            box.space.sync:insert{1}
        end)
        local waiter = fiber.create(function()
            fiber.self():set_joinable(true)
            box.ctl.txn_sync()
        end)
        box.error.injection.set('ERRINJ_WAL_WRITE', true)
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
        local ok = writer:join()
        t.assert_not(ok)
        local ok, err = waiter:join()
        t.assert_not(ok)
        t.assert_equals(err.code, box.error.CASCADE_ROLLBACK)
        box.error.injection.set('ERRINJ_WAL_WRITE', false)
        -- A WAL rollback freezes the limbo until the next promotion.
        box.ctl.promote()
        box.ctl.wait_rw(120)
    end)
end

g.test_synchronous_rollback = function(cg)
    cg.server:exec(function()
        local fiber = require('fiber')
        local quorum = box.cfg.replication_synchro_quorum
        local timeout = box.cfg.replication_synchro_timeout
        box.cfg{replication_synchro_quorum = 2}
        local writer = fiber.create(function()
            fiber.self():set_joinable(true)
            box.space.sync:insert{1}
        end)
        box.ctl.wal_sync()
        local waiter = fiber.create(function()
            fiber.self():set_joinable(true)
            box.ctl.txn_sync()
        end)
        -- Pass the journal barrier before shortening the writer's timeout.
        box.ctl.wal_sync()
        box.cfg{replication_synchro_timeout = 0.01}
        local ok = writer:join()
        t.assert_not(ok)
        local ok, err = waiter:join()
        t.assert_not(ok)
        t.assert_equals(err.code, box.error.SYNC_ROLLBACK)
        box.cfg{
            replication_synchro_quorum = quorum,
            replication_synchro_timeout = timeout,
        }
    end)
end
