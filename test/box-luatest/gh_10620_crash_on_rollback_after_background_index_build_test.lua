local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_each(function(cg)
    cg.server = server:new()
    cg.server:start()
end)

g.after_each(function(cg)
    cg.server:drop()
end)

g.test_crash_on_wal_failure = function(cg)
    t.tarantool.skip_if_not_debug()
    cg.server:exec(function()
        local fiber = require('fiber')
        local space = box.schema.space.create('test')
        space:create_index('pk')
        for i = 1, 100 do
            space:replace({i, i})
        end
        local ch = fiber.channel(1)
        box.error.injection.set('ERRINJ_WAL_DELAY', true)
        fiber.create(function()
            box.begin()
            space:create_index('sk')
            ch:put(true)
            box.commit()
        end)
        fiber.create(function()
            box.begin()
            for i = 1, 100 do
                space:replace{i, i}
            end
            ch:put(true)
            box.commit()
        end)
        ch:get()
        ch:get()
        box.error.injection.set('ERRINJ_WAL_WRITE', true)
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
    end)
end
