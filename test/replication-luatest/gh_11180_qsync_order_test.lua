local server = require('luatest.server')
local t = require('luatest')
local g = t.group()

g.before_all(function(cg)
    t.tarantool.skip_if_not_debug()
    cg.server = server:new({
        box_cfg = {
            replication_synchro_queue_max_size = 1000,
            wal_queue_max_size = 1000,
            replication_synchro_timeout = 1000,
            election_mode = 'manual',
        }
    })
    cg.server:start()
    cg.server:exec(function()
        box.ctl.promote()
        box.ctl.wait_rw()
        local s = box.schema.create_space('test', {is_sync = true})
        s:create_index('pk')
    end)
end)

g.after_each(function(cg)
    cg.server:exec(function()
        box.space.test:truncate()
    end)
end)

g.after_all(function(cg)
    cg.server:drop()
end)

--
-- gh-11180: rollback of a txn waiting for space in the limbo wouldn't cascading
-- rollback the newer txns.
--
g.test_cascading_rollback_while_waiting_for_limbo_space = function(cg)
    cg.server:exec(function()
        local fiber = require('fiber')
        local data = string.rep('a', 1000)
        local timeout = 60
        local s = box.space.test

        local results = {}
        local function make_txn_fiber(id)
            return fiber.create(function()
                fiber.self():set_joinable(true)
                box.begin()
                box.on_rollback(function()
                    table.insert(results, ('rollback %s'):format(id))
                end)
                box.on_commit(function()
                    table.insert(results, ('commit %s'):format(id))
                end)
                s:replace{id, data}
                box.commit()
            end)
        end
        local function join_with_error(f, expected_err)
            local ok, err = f:join()
            t.assert_not(ok)
            t.assert_covers(err:unpack(), expected_err)
        end
        --
        -- txn1 is stuck in WAL, txn2-4 are waiting for limbo space.
        --
        box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', 0)
        local f1 = make_txn_fiber(1)
        local f2 = make_txn_fiber(2)
        local f3 = make_txn_fiber(3)
        local f4 = make_txn_fiber(4)
        t.helpers.retrying({timeout = timeout}, function()
            t.assert(box.error.injection.get('ERRINJ_WAL_DELAY'))
        end)
        --
        -- txn3 is cancelled and rolled back. txn4 is cascading rolled back.
        --
        f3:cancel()
        -- Limbo doesn't set the cascading rollback error. No reason why.
        join_with_error(f3, {name = 'SYNC_ROLLBACK'})
        join_with_error(f4, {name = 'SYNC_ROLLBACK'})
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
        --
        -- The older txns were not affected by the rollback of newer txns.
        --
        t.assert((f1:join(timeout)))
        t.assert((f2:join(timeout)))
        t.assert_equals(results, {'rollback 4', 'rollback 3',
                                  'commit 1', 'commit 2'})
        t.assert_equals(s:select(), {{1, data}, {2, data}})
    end)
end

--
-- One txn is submitted to the limbo and is stuck in WAL. The other txn is
-- volatile and waits for limbo space. Suddenly there becomes enough space. The
-- second then should see that it is not the first one, but it is the first
-- volatile one and hence can proceed to WAL.
--
g.test_unblock_nonfirst_volatile_entry = function(cg)
    cg.server:exec(function()
        local fiber = require('fiber')
        local s = box.space.test
        local data = string.rep('a', 1000)
        local timeout = 60
        box.error.injection.set('ERRINJ_WAL_DELAY', true)
        local f1 = fiber.create(function()
            fiber.self():set_joinable(true)
            s:replace{1, data}
        end)
        local f2 = fiber.create(function()
            fiber.self():set_joinable(true)
            s:replace{2, data}
        end)
        t.assert_equals(box.info.synchro.queue.len, 1)
        local old_size = box.cfg.replication_synchro_queue_max_size
        box.cfg{replication_synchro_queue_max_size = 1000000}
        f2:wakeup()
        t.helpers.retrying({timeout = timeout}, function()
            t.assert_equals(box.info.synchro.queue.len, 2)
        end)
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
        t.assert((f1:join()))
        t.assert((f2:join()))
        t.assert_equals(s:select(), {{1, data}, {2, data}})
        box.cfg{replication_synchro_queue_max_size = old_size}
    end)
end

--
-- gh-11180: volatile txns waiting for the limbo space didn't form any queue
-- and their spurious wakeups could lead to them finishing the commits not in
-- the same order as the commits were started.
--
g.test_spurious_wakeup = function(cg)
    cg.server:exec(function()
        local fiber = require('fiber')
        local s = box.space.test
        local data = string.rep('a', 1000)
        local timeout = 60
        local f1, f2, f3
        --
        -- This fiber gets executed on each event loop iteration, right after
        -- the scheduler-fiber. It is able then to mess up any plans that the
        -- next fibers would have about waking each other up in any special
        -- order.
        --
        local wakeuper = fiber.create(function()
            while true do
                fiber.testcancel()
                --
                -- f2 is supposed to finish before f3, but lets wake them up
                -- always in the reversed order.
                --
                if f3 then
                    pcall(f3.wakeup, f3)
                end
                if f2 then
                    pcall(f2.wakeup, f2)
                end
                fiber.yield()
            end
        end)
        box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', 0)
        local results = {}
        local function make_txn_fiber(id)
            return fiber.create(function()
                fiber.self():set_joinable(true)
                box.begin()
                box.on_rollback(function()
                    table.insert(results, ('rollback %s'):format(id))
                end)
                box.on_commit(function()
                    table.insert(results, ('commit %s'):format(id))
                end)
                s:replace{id, data}
                box.commit()
            end)
        end
        f1 = make_txn_fiber(1)
        f2 = make_txn_fiber(2)
        f3 = make_txn_fiber(3)
        t.helpers.retrying({timeout = timeout}, function()
            t.assert(box.error.injection.get('ERRINJ_WAL_DELAY'))
        end)
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
        t.assert((f1:join(timeout)))
        t.assert((f2:join(timeout)))
        t.assert((f3:join(timeout)))
        wakeuper:cancel()
        t.assert_equals(results, {'commit 1', 'commit 2', 'commit 3'})
        t.assert_equals(s:select(), {{1, data}, {2, data}, {3, data}})
    end)
end
