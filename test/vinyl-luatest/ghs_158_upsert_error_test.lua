local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function(cg)
    cg.server = server:new()
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.test_upsert_error = function(cg)
    cg.server:exec(function()
        box.cfg({vinyl_max_tuple_size = 100})
        local s = box.schema.space.create('test', {engine = 'vinyl'})
        s:create_index('primary')
        s:insert({1, string.rep('x', 50)})
        s:upsert({1, 'x'}, {{'!', 3, string.rep('y', 50)}, {'=', 1, 1}})
        t.assert_error_covers({
            type = 'ClientError',
            name = 'VINYL_MAX_TUPLE_SIZE',
        }, box.snapshot)
        box.cfg({vinyl_max_tuple_size = 1000})
        box.snapshot()
        s:drop()
    end)
end
