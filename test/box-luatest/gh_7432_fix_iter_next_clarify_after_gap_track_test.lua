local server = require('test.luatest_helpers.server')
local t = require('luatest')

local g = t.group()

g.before_all(function(cg)
    cg.server = server:new{
        alias   = 'dflt',
        box_cfg = {memtx_use_mvcc_engine = true}
    }
    cg.server:start()
    cg.server:exec(function()
        local s = box.schema.create_space('s')
        s:create_index('pk')
        s:insert{0}
        s:insert{1}
        box.internal.memtx_tx_gc(100)
    end)
end)

g.after_all(function(cg)
    cg.server:drop()
end)

--[[
Checks that `select` with `LE` iterator does not return deleted tuple do to
clarification of garbage collected tuple story (triggered by gap tracking).
]]
g.test_gap_tracking_full_key_corner_cases = function(cg)
    cg.server:exec(function()
        box.space.s:delete{0}
    end)

    local stream = cg.server.net_box:new_stream()
    stream:begin()
    t.assert_equals(stream.space.s:select({1}, {iterator = 'LE'}), {{1}})
end
