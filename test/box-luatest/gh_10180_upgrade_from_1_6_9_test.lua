local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function(cg)
    cg.server = server:new({
        datadir = 'test/box-luatest/upgrade/1.6.9',
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
    -- Restart the server and check that we recover properly even if the
    -- snapshot has not been taken. Luatest automatically calls
    -- wait_until_ready().
    cg.server:restart()
    -- Now check that recovery is successful after snapshot is taken.
    cg.server:exec(function()
        box.snapshot()
    end)
    cg.server:restart()
end
