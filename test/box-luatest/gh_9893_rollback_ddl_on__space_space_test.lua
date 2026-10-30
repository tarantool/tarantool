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

-- Test that rollback of DDL statements on the `_space` space works correctly.
g.test_rollback_ddl_on__space_space = function(cg)
    cg.server:exec(function()
        local _space = box.space._space:get{box.space._space.id}:totable()

        box.begin()
        box.space._space:alter{is_sync = false, defer_deletes = true}
        box.rollback()

        -- Test that the server does not crash because of heap-use-after-free.
        collectgarbage()
        box.space._space:select{}
        collectgarbage()

        t.assert_equals(box.space._space:get{box.space._space.id}:totable(),
                        _space)
    end)
end
