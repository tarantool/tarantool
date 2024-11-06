local server = require('luatest.server')
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
        box.internal.memtx_tx_gc(100)
    end)
end)

g.after_all(function(cg)
    cg.server:drop()
end)

--[[
Checks that assertion is fired if memtx transaction manager MVCC invariant is
violated.
]]
g.test_memtx_tx_manager_mvcc_invariant_violation = function(cg)
    local stream = cg.server.net_box:new_stream()
    stream:begin()

    cg.server:exec(function()
        box.space.s:insert{0, 0}
    end)

    -- Pins {0, 0} so it doesn't get garbage collected early.
    stream.space.s:select{0}

    cg.server:exec(function()
        box.space.s:replace{0, 1}
    end)

    -- Releases {0, 0}.
    stream:commit()
    -- Now {0, 1} which is at the top of {0} key's history chain can get garbage
    -- collected.
    cg.server:exec(function()
        box.space.s:delete{0}
        box.internal.memtx_tx_gc(100)
    end)
end
