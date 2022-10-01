local server = require('test.luatest_helpers.server')
local t = require('luatest')

local g = t.group()

g.before_all(function(cg)
    cg.server = server:new {
        alias   = 'dflt',
        box_cfg = {memtx_use_mvcc_engine = true}
    }
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

--[[
Checks that `space:drop` does not trigger assertion.
]]
g.test_space_drop_does_not_trigger_assertion = function(cg)
    cg.server:exec(function()
        local s = box.schema.create_space('s')
        s:create_index('pk')
        s:insert{0}
        s:get{0}
        s:drop()
    end)
end
