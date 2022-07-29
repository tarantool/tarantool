local server = require('test.luatest_helpers.server')
local t = require('luatest')

local g = t.group(nil, t.helpers.matrix{iter = {'LT', 'LE', 'REQ', 'EQ', 'GE',
                                                'ALL'}})

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
        s:create_index('pk', {parts = {{1, 'uint'}, {2, 'uint'}}})
    end)
end)

g.after_each(function(cg)
    cg.server:exec(function()
        box.space.s:drop()
    end)
end)

--[[
Checks that gap tracking correctly handles insertion of a key with the same
value as the gap item (full key).
]]
g.test_gap_tracking_full_key_corner_cases = function(cg)
    t.skip_if(cg.params.iter == 'REQ' or cg.params.iter == 'EQ')

    local stream1 = cg.server.net_box:new_stream()
    local stream2 = cg.server.net_box:new_stream()

    stream1:begin()
    stream2:begin()

    stream1.space.s:select({1, 1}, {iterator = cg.params.iter})
    stream1.space.s:insert{1, 1}

    stream2.space.s:insert{1, 0}
    stream2.space.s:insert{1, 2}
    stream2.space.s:insert{0, 0}

    stream2:commit()

    local conflict_err_msg = 'Transaction has been aborted by conflict'
    t.assert_error_msg_content_equals(conflict_err_msg, function()
        stream1:commit()
    end)
end

--[[
Checks that gap tracking correctly handles insertion of a key with the same
value as the gap item (partial key).
]]
g.test_gap_tracking_partial_key_corner_cases = function(cg)
    local stream1 = cg.server.net_box:new_stream()
    local stream2 = cg.server.net_box:new_stream()

    stream1:begin()
    stream2:begin()

    stream1.space.s:select({1}, {iterator = cg.params.iter})
    stream1.space.s:insert{1, 1}

    stream2.space.s:insert{1, 0}
    stream2.space.s:insert{0, 0}

    stream2:commit()

    local conflict_err_msg = 'Transaction has been aborted by conflict'
    t.assert_error_msg_content_equals(conflict_err_msg, function()
        stream1:commit()
    end)
end
