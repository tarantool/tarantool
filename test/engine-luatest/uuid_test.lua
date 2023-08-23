local server = require('luatest.server')
local t = require('luatest')

local g = t.group('UUID tests', {
    {engine = 'memtx'},
    {engine = 'vinyl'},
})

g.before_all(function(cg)
    cg.server = server:new({
        alias = 'default',
    })
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.before_each(function(cg)
    cg.server:exec(function(engine)
        local uuid = require('uuid')
        box.schema.space.create('test', {engine = engine})
        box.space.test:create_index('pk', {parts={1, 'uuid'}})
        for _ = 1, 16 do
            box.space.test:insert({uuid.new()})
        end
    end, {cg.params.engine})
end)

g.after_each(function(cg)
    cg.server:exec(function()
        box.space.test:drop()
    end)
end)

g.test_uuid_order = function(cg)
    cg.server:exec(function()
        local tuples = box.space.test:select({})
        for i = 1, #tuples - 1 do
            t.assert(tostring(tuples[i][1]) < tostring(tuples[i + 1][1]))
        end
    end)
end
