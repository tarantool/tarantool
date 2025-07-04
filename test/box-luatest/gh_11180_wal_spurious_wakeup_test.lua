local server = require('luatest.server')
local t = require('luatest')
local g = t.group()

g.before_all(function(cg)
    t.tarantool.skip_if_not_debug()
    cg.server = server:new({box_cfg = {wal_queue_max_size = 1000}})
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

--
-- gh-11180: the queued but not yet submitted WAL entries were not handling
-- spurious wakeups and could break in all sorts of ways due to that.
--
g.test_wal_queue_rollback_in_flight = function(cg)
    cg.server:exec(function()
        local fiber = require('fiber')
        local data = string.rep('a', 1000)
        local timeout = 60
        local s = box.schema.create_space('test')
        s:create_index('pk')

        local results = {}
        local function make_txn_fiber(id)
            return fiber.create(function()
                fiber.self():set_joinable(true)
                box.begin()
                box.on_commit(function()
                    table.insert(results, id)
                end)
                s:replace{id, data}
                box.commit()
            end)
        end
        box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', 0)
        local f2, f3 = nil
        --
        -- The first txn goes to WAL. The other 2 txns wait for free space in
        -- the journal queue. When the first txn gets committed, it wakes the
        -- next txn-fibers up in a wrong order. They should still be able to
        -- re-sort themselves.
        --
        local f1 = fiber.create(function()
            fiber.self():set_joinable(true)
            box.begin()
            box.on_commit(function()
                table.insert(results, 1)
                if f3 then
                    pcall(f3.wakeup, f3)
                end
                if f2 then
                    pcall(f2.wakeup, f2)
                end
            end)
            s:replace{1, data}
            box.commit()
        end)
        -- Make sure all 3 txns are waiting and aren't committed just one by one
        -- somehow.
        t.helpers.retrying({timeout = timeout}, function()
            t.assert(box.error.injection.get('ERRINJ_WAL_DELAY'))
        end)
        f2 = make_txn_fiber(2)
        f3 = make_txn_fiber(3)
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
        t.assert((f1:join(timeout)))
        t.assert((f2:join(timeout)))
        t.assert((f3:join(timeout)))
        t.assert_equals(results, {1, 2, 3})
        s:drop()
    end)
end
