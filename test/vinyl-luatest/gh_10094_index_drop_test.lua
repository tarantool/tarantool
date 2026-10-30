local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function(cg)
    t.tarantool.skip_if_not_debug()
    cg.server = server:new()
    cg.server:start()
    cg.server:exec(function()
        box.cfg{vinyl_cache = 0}
    end)
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.after_each(function(cg)
    cg.server:exec(function()
        if box.space.test ~= nil then
            box.space.test:drop()
        end
        box.error.injection.set('ERRINJ_VY_READ_PAGE_DELAY', false)
    end)
end)

g.test_index_drop = function(cg)
    cg.server:exec(function()
        local fiber = require('fiber')
        local s = box.schema.create_space('test', {engine = 'vinyl'})
        s:create_index('pk')
        s:create_index('sk', {parts = {{2, 'unsigned'}}})
        s:insert{1, 10}
        box.snapshot()
        box.error.injection.set('ERRINJ_VY_READ_PAGE_DELAY', true)
        fiber.create(function()
            s:insert{2, 10}
        end)
        s.index.sk:drop()
        box.error.injection.set('ERRINJ_VY_READ_PAGE_DELAY', false)
        t.assert_equals(s:select({}, {fullscan = true}), {{1, 10}})
    end)
end
