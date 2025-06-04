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
        box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', 0)
        local f1 = make_txn_fiber(1)
        local f2 = make_txn_fiber(2)
        local f3 = make_txn_fiber(3)
        t.helpers.retrying({timeout = timeout}, function()
            t.assert(box.error.injection.get('ERRINJ_WAL_DELAY'))
        end)
        f2:cancel()
        t.assert_not((f2:join(timeout)))
        t.assert_not((f3:join(timeout)))
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
        t.assert((f1:join(timeout)))
        t.assert_equals(results, {'rollback 3', 'rollback 2', 'commit 1'})
        t.assert_equals(s:select(), {{1, data}})
    end)
end

g.test_spurious_wakeup = function(cg)
    cg.server:exec(function()
        local fiber = require('fiber')
        local s = box.space.test
        local data = string.rep('a', 1000)
        local timeout = 60
        local f1, f2, f3 = nil
        local wakeuper = fiber.create(function()
            while true do
                fiber.testcancel()
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
