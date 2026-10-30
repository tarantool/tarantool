local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function(cg)
    cg.server = server:new{
        alias   = 'dflt',
        box_cfg = {memtx_use_mvcc_engine = true}
    }
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.before_each(function(cg)
    cg.server:exec(function()
        local s = box.schema.create_space('s')
        s:create_index('pk', {type = 'HASH'})
    end)
end)

g.after_each(function(cg)
    cg.server:exec(function()
        box.space.s:drop()
    end)
end)

--[[
Checks that commit of a tuple replacing a gap-inserted tuple from a concurrent
transaction is correctly handled.
]]
g.test_replace_of_gap_inserted_tuple_into_hash_idx = function(cg)
    local stream1 = cg.server.net_box:new_stream()
    local stream2 = cg.server.net_box:new_stream()
    local stream3 = cg.server.net_box:new_stream()

    stream1:begin()
    stream2:begin()
    stream3:begin()

    stream1.space.s:select({})
    -- Gap write is handled here.
    stream2.space.s:insert{0}
    --[[
    {0} is present in index, hence this insertion is not considered a gap write.
    ]]
    stream3.space.s:insert{0}

    stream3:commit()

    t.assert_equals(stream1.space.s:select({}), {})
end
