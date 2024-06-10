local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

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
        if box.space.test ~= nil then
            box.space.test:drop()
        end
        box.error.injection.set('ERRINJ_VY_COMPACTION_DELAY', false)
    end)
end)

g.test_read_upsert = function(cg)
    cg.server:exec(function()
        box.error.injection.set('ERRINJ_VY_COMPACTION_DELAY', true)
        local s = box.schema.create_space('test', {engine = 'vinyl'})
        s:create_index('pk')
        s:insert({100})
        box.snapshot()
        s:upsert({200}, {})
        s:upsert({300}, {})
        s:upsert({400}, {})
        box.snapshot()
        t.assert_equals(s:select({500}, {iterator = 'lt', fullscan = true}),
                        {{400}, {300}, {200}, {100}})
    end)
end
