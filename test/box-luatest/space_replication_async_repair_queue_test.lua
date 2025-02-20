local t = require('luatest')
local server = require('luatest.server')

local g = t.group()

g.before_each(function(cg)
    cg.server = server:new({
        datadir = 'test/box-luatest/upgrade/3.3.0',
    })
    cg.server:start()
end)

g.after_each(function(cg)
    cg.server:drop()
end)

g.test_upgrade = function(cg)
    cg.server:exec(function()
        local _priv = box.space._priv
        local space_id = box.schema.REPLICATION_ASYNC_REPAIR_QUEUE_ID

        t.assert_equals(box.space._space:get(space_id), nil)
        t.assert_equals(_priv.index.object:select{'space', space_id}, {})

        box.schema.upgrade()

        t.assert_not_equals(box.space._space:get(space_id), nil)
        t.assert_equals(_priv.index.object:count{'space', space_id}, 1)
    end)
end
