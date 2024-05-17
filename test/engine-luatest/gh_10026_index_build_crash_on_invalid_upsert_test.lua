local t = require('luatest')

local server = require('luatest.server')

local g = t.group(nil, t.helpers.matrix{engine = {'memtx', 'vinyl'}})

g.before_all(function(cg)
    cg.server = server:new()
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.after_each(function(cg)
    cg.server:exec(function()
        box.error.injection.set('ERRINJ_BUILD_INDEX_DELAY', false)
        if box.space.test ~= nil then
            box.space.test:drop()
        end
    end)
end)

g.test_index_build_crash_on_invalid_upsert = function(cg)
    t.tarantool.skip_if_not_debug()
    cg.server:exec(function(engine)
        local fiber = require('fiber')
        local s = box.schema.space.create('test', {engine = engine,})
        s:create_index('primary')
        for i = 1, 5 do
            s:insert({i, i * 10})
        end
        box.error.injection.set('ERRINJ_BUILD_INDEX_DELAY', true)
        local f = fiber.new(s.create_index, s, 'secondary',
                            {parts = {2, 'unsigned'}})
        f:set_joinable(true)
        for i = 1, 5 do
            s:upsert({i, i * 100}, {{'+', 1, 1}})
        end
        box.error.injection.set('ERRINJ_BUILD_INDEX_DELAY', false)
        f:join()
        t.assert(s.index.secondary)
        t.assert_equals(s.index.secondary:select({}, {fullscan = true}),
                        {{1, 10}, {2, 20}, {3, 30}, {4, 40}, {5, 50}})
    end, {cg.params.engine})
end
