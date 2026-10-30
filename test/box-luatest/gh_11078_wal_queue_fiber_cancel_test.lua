local server = require('luatest.server')
local t = require('luatest')

local variants = {}
for i = 1, 5 do
    table.insert(variants, {pos = i})
end
local g = t.group('wal', variants)

g.before_all(function(cg)
    t.tarantool.skip_if_not_debug()
    cg.server = server:new()
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.after_each(function(cg)
    cg.server:exec(function()
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
        box.space.test:drop()
    end)
end)

g.test_wal_queue_fiber_cancel = function(cg)
    cg.server:exec(function(pos)
        local fiber = require('fiber')
        local s = box.schema.create_space('test')
        s:create_index('pk')
        box.cfg{wal_queue_max_size = 100}
        box.error.injection.set('ERRINJ_WAL_DELAY', true)
        fiber.create(function()
            box.begin()
            s:insert({100, string.rep('a', 1000)})
            s:insert({0})
            box.commit()
        end)
        local fibers = {}
        for i = 1, 5 do
            local f = fiber.new(function()
                -- Crafted to check that WAL records are in correct order
                -- on recovery after restart.
                box.begin()
                s:delete({i - 1})
                s:insert({i})
                box.commit()
            end)
            f:set_joinable(true)
            table.insert(fibers, f)
            fiber.yield()
        end
        fibers[pos]:cancel()
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
        for i = 1, pos - 1 do
            t.assert_equals({fibers[i]:join()}, {true})
        end
        local ok, err = fibers[pos]:join()
        t.assert_not(ok)
        t.assert_covers(err:unpack(), {
            type = 'FiberIsCancelled',
        })
        for i = pos + 1, 5 do
            local ok, err = fibers[i]:join()
            t.assert_not(ok)
            t.assert_covers(err:unpack(), {
                type = 'ClientError',
                code = box.error.CASCADE_ROLLBACK,
                message = "WAL has a rollback in progress",
            })
        end
        t.assert_equals(s:select({100}, {iterator = 'lt'}), {{pos - 1}})
    end, {cg.params.pos})
    cg.server:restart()
    cg.server:exec(function(pos)
        local s = box.space.test
        t.assert_equals(s:select({100}, {iterator = 'lt'}), {{pos - 1}})
    end, {cg.params.pos})
end
