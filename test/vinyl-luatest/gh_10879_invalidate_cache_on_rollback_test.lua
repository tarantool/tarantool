local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function(cg)
    t.tarantool.skip_if_not_debug()
    cg.server = server:new({
        box_cfg = {
            vinyl_cache = 1024 * 1024,
        },
    })
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.after_each(function(cg)
    cg.server:exec(function()
        box.error.injection.set('ERRINJ_WAL_WRITE', false)
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
        if box.space.test ~= nil then
            box.space.test:drop()
        end
    end)
end)

g.test_invalidate_cache_on_rollback = function(cg)
    cg.server:exec(function()
        local fiber = require('fiber')

        local s = box.schema.space.create('test', {engine = 'vinyl'})
        s:create_index('primary')

        local function read_committed()
            return box.atomic({txn_isolation = 'read-committed'}, s.select, s)
        end

        s:insert({10})
        s:insert({20})
        s:insert({30})
        s:insert({40})
        s:insert({50})
        box.snapshot()

        box.error.injection.set('ERRINJ_WAL_DELAY', true)

        local fibers = {}
        table.insert(fibers, fiber.new(s.replace, s, {10, 1}))
        table.insert(fibers, fiber.new(s.delete, s, {30}))
        table.insert(fibers, fiber.new(s.replace, s, {60, 1}))
        for _, f in ipairs(fibers) do
            f:set_joinable(true)
        end
        fiber.yield()

        t.assert_equals(read_committed(), {{10, 1}, {20}, {40}, {50}, {60, 1}})

        box.error.injection.set('ERRINJ_WAL_WRITE', true)
        box.error.injection.set('ERRINJ_WAL_DELAY', false)

        for _, f in ipairs(fibers) do
            local ok, err = f:join(5)
            t.assert_not(ok)
            t.assert_items_include({
                box.error.WAL_IO,
                box.error.CASCADE_ROLLBACK,
            }, {err.code})
        end

        t.assert_equals(read_committed(), {{10}, {20}, {30}, {40}, {50}})
    end)
end
