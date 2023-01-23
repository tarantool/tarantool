local server = require('luatest.server')
local t = require('luatest')

local pg = t.group(nil, t.helpers.matrix{idx = {'TREE', 'HASH'}})

pg.before_all(function(cg)
    cg.server = server:new{
        alias   = 'dflt',
        box_cfg = {memtx_use_mvcc_engine = true}
    }
    cg.server:start()
    cg.server:exec(function(idx)
        local s = box.schema.create_space('s')
        s:create_index('pk')
        s:create_index('sk', {type = idx, parts = {2}})
    end, {cg.params.idx})
end)

pg.after_all(function(cg)
    cg.server:drop()
end)

--[[
Checks that committed tuple is not lost in secondary index.
]]
pg.test_idx_random_from_empty_space_tracking = function(cg)
    local stream = cg.server.net_box:new_stream()
    stream:begin()

    stream.space.s:insert{0, 1}
    cg.server:exec(function()
        box.space.s:insert{2, 1}
        box.internal.memtx_tx_gc(100)

        t.assert_equals(box.space.s.index[1]:select{}, {{2, 1}})
    end)
end
