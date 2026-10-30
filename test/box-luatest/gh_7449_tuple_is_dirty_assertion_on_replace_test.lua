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
        s:create_index('sk1', {unique = false, parts = {{2, 'uint'}}})
        s:create_index('sk2', {unique = false, parts = {{3, 'uint'}}})
        s:insert{0, 0, 0}
        s:insert{1, 1, 1}
        box.internal.memtx_tx_gc(100)
    end)
end)

g.after_all(function(cg)
    cg.server:drop()
end)

--[[
Checks that replaced story (the one that was in primary index) does not suddenly
get garbage collected during `replace` handling.
]]
g.test_replaced_story_does_not_get_garbage_collected = function(cg)
    local stream1 = cg.server.net_box:new_stream()
    local stream2 = cg.server.net_box:new_stream()

    stream1:begin()
    stream2:begin()

    -- Pins {0, 0, 0}'s story so it doesn't get garbage collected.
    stream1.space.s:select{0}
    -- A gap tracker is attached to {1, 1, 1}'s story.
    stream2.space.s.index[1]:select({0}, {iterator = 'GT'})

    cg.server:exec(function()
        box.space.s:delete{0}
    end)

    -- Releases {0, 0, 0}'s story so it can potentially get garbage collected.
    stream1:commit()

    cg.server:exec(function()
        --[[
        The replaced tuple's story, {0, 0, 0}, can potentially get garbage
        collected: a gap write is handled with {1, 1, 1} as a successor and read
        of {1, 1, 1}'s story is tracked, effectively triggering story garbage
        collection.
        ]]
        box.space.s:replace{0, 1, 1}
    end)
end

--[[
Checks that replaced story (the one that was in secondary index 'sk2') does not
suddenly get garbage collected during `replace` handling.
]]
g.test_replaced_story_does_not_get_garbage_collected = function(cg)
    local stream1 = cg.server.net_box:new_stream()
    local stream2 = cg.server.net_box:new_stream()

    stream1:begin()
    stream2:begin()

    stream1.space.s:select{0}
    stream2.space.s.index[1]:select({0}, {iterator = 'GT'})

    cg.server:exec(function()
        box.space.s:delete{0}
    end)

    stream1:commit()

    cg.server:exec(function()
        box.space.s:replace{0, 1, 0}
    end)
end
