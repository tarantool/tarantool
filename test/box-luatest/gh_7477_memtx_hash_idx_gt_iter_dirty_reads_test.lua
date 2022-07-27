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

    stream1.space.s:insert{4}
    stream2.space.s:insert{8}

    stream3.space.s:insert{2}
    stream3:commit()

    t.assert_equals(stream4.space.s:select({8}, {iterator = 'GT'}), {{2}})
end
