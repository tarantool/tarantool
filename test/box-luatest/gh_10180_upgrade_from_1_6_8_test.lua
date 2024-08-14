local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function(cg)
    cg.server = server:new({
        datadir = 'test/box-luatest/upgrade/1.6.8',
    })
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.test_upgrade = function(cg)
    cg.server:exec(function()
        t.assert_equals(box.space._schema:get{'version'}, {'version', 1, 6, 8})
        t.assert(pcall(box.schema.upgrade))
    end)
    -- Restart the server and check, that we properly recover even if snapshot
    -- have not been taken. Luatest automatically calls wait_until_ready().
    cg.server:restart()
end
