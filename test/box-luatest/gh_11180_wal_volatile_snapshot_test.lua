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
-- gh-11180: the snapshot creation used to ignore the volatile journal entries
-- waiting for the space in the journal queue. It could result into some data
-- being present in the snapshot with an old vclock. And then on restart those
-- entries would be re-applied from the following xlogs, leading to the
-- operations applied twice.
--
g.test_wal_queue_rollback_in_flight = function(cg)
    cg.server:exec(function()
        local fiber = require('fiber')
        local data = string.rep('a', 1000)
        local timeout = 60
        local s = box.schema.create_space('test')
        s:create_index('pk')

        box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', 0)
        local function make_txn_fiber(id)
            return fiber.create(function()
                fiber.self():set_joinable(true)
                -- Insert will fail if would try to be applied both from the
                -- snapshot and from the following xlog.
                s:insert{id, data}
            end)
        end
        -- One txn is in WAL. Another is waiting for space.
        local f1 = make_txn_fiber(1)
        local f2 = make_txn_fiber(2)
        t.helpers.retrying({timeout = timeout}, function()
            t.assert(box.error.injection.get('ERRINJ_WAL_DELAY'))
        end)
        -- The snapshot must wait until all the journal entries are flushed.
        local f_snap = fiber.create(function()
            fiber.self():set_joinable(true)
            box.snapshot()
        end)
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
        t.assert((f1:join()))
        t.assert((f2:join()))
        t.assert((f_snap:join()))
    end)
    cg.server:restart()
    cg.server:exec(function()
        t.assert(box.space.test:get{1})
        t.assert(box.space.test:get{2})
    end)
end
