local t = require('luatest')
local server = require('luatest.server')
local cluster = require('luatest.replica_set')

local g = t.group('Upgrade', {{version = '2.11.0'}, {version = '3.0.0'}})

g.before_each(function(cg)
    cg.server = server:new({
        datadir = 'test/box-luatest/upgrade/' .. cg.params.version,
    })
    cg.server:start()
end)

g.after_each(function(cg)
    cg.server:drop()
end)

g.test_upgrade = function(cg)
    cg.server:exec(function()
        local _priv = box.space._priv
        local space_id = box.schema.GC_CONSUMERS_ID

        t.assert_equals(box.space._space:get(space_id), nil)
        t.assert_equals(_priv.index.object:select{'space', space_id}, {})

        box.schema.upgrade()

        t.assert_not_equals(box.space._space:get(space_id), nil)
        t.assert_equals(#_priv.index.object:select{'space', space_id}, 1)
    end)
end
