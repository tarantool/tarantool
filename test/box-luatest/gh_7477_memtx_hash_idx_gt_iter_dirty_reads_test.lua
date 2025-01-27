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
        s:create_index('pk', {type = 'HASH'})
    end)
end)

g.after_all(function(cg)
    cg.server:drop()
end)

--[[
Checks that HASH index `select` with `GT` iterator does not return dirty tuples.
]]
g.test_memtx_hash_idx_gt_iterator_dirty_reads = function(cg)
    local stream1 = cg.server.net_box:new_stream()
    local stream2 = cg.server.net_box:new_stream()
    local stream3 = cg.server.net_box:new_stream()
    local stream4 = cg.server.net_box:new_stream()

    stream1:begin()
    stream2:begin()
    stream3:begin()
    stream4:begin()

    for i = 1, 100 do
        stream1.space.s:insert{i}
    end
    for i = 101, 200 do
        stream2.space.s:insert{i}
    end
    for i = 201, 300 do
        stream3.space.s:insert{i}
    end
    stream3:commit()

    local res = stream4.space.s:select({1}, {iterator = 'GT'})
    t.assert((next(res)))
    for _, tuple in pairs(res) do
        t.assert_ge(tuple[1], 201)
        t.assert_le(tuple[1], 300)
    end
end
