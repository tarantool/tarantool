local server = require('luatest.server')
local t = require('luatest')
local g = t.group()

g.before_all(function(cg)
    cg.server = server:new({alias = 'master'})
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:stop()
end)

g.test_1_6_style_parts_with_format = function(cg)
    cg.server:exec(function()
        local t = require('luatest')

        box.schema.space.create('test')
        box.space.test:format({
            {name = 'id', type = 'number'},
            {name = 'data', type = 'string', is_nullable = true},
        })
        box.space.test:create_index('primary')
        box.space.test:create_index('sec1', {parts = {2, 'string'}})
        t.assert(box.space.test.index.sec1.parts[1].is_nullable)
        box.space.test:create_index('sec2',
                                    {parts = {1, 'number', 2, 'string' }})
        t.assert(box.space.test.index.sec2.parts[2].is_nullable)
    end)
end
