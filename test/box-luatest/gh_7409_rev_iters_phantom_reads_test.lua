local server = require('test.luatest_helpers.server')
local t = require('luatest')

local g = t.group(nil, t.helpers.matrix{iter = {'LT', 'LE', 'REQ'}})

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
        s:create_index('pk')
    end)
end)

g.after_each(function(cg)
    cg.server:exec(function()
        box.space.s:drop()
    end)
end)

--[[
Checks that gap tracking is correctly handled for the first key in reverse
iteration sequence.
]]
g.test_rev_iters_phantom_read_first_key = function(cg)
    local stream1 = cg.server.net_box:new_stream()
    local stream2 = cg.server.net_box:new_stream()
    local stream3 = cg.server.net_box:new_stream()

    cg.server:exec(function()
        box.space.s:insert{1}
    end)

    stream1:begin()
    stream2:begin()
    stream3:begin()

    stream1.space.s:replace{1}
    stream2.space.s:select({}, {iterator = cg.params.iter})
    stream3.space.s:insert{0}
    stream3:commit()

    t.assert_equals(stream2.space.s:select{}, {{1}})
end

--[[
Checks that gap tracking is correctly handled for subsequent keys in reverse
iteration sequence.
]]
g.test_rev_iters_phantom_read_subsequent_key = function(cg)
    local stream1 = cg.server.net_box:new_stream()
    local stream2 = cg.server.net_box:new_stream()
    local stream3 = cg.server.net_box:new_stream()

    cg.server:exec(function()
        box.space.s:insert{2}
        box.space.s:insert{1}
    end)

    stream1:begin()
    stream2:begin()
    stream3:begin()

    stream1.space.s:replace{1}
    stream2.space.s:select({}, {iterator = cg.params.iter})
    stream3.space.s:insert{0}
    stream3:commit()

    t.assert_equals(stream2.space.s:select{}, {{1}, {2}})
end
