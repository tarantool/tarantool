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

g.before_each(function(cg)
    cg.server:exec(function()
        local s = box.schema.create_space('s')
        s:create_index('pk')
        s:create_index('sk', {parts = {{2}}})
    end)
end)

g.after_each(function(cg)
    cg.server:exec(function()
        box.space.s:drop()
    end)
end)

--[[
Checks that during rollback read tracker is rebound in all indexes for stories with identical key parts.
]]
g.test_rollback_rebind_read_tracker_for_stories_with_identical_key_parts = function(cg)
    local stream1 = cg.server.net_box:new_stream()
    local stream2 = cg.server.net_box:new_stream()
    local stream3 = cg.server.net_box:new_stream()

    stream1:begin()
    stream2:begin()
    stream3:begin()

    stream1.space.s:insert{1, 0}
    stream2.space.s:insert{1, 0, 0}

    stream3.space.s.index[1]:get{0}
    stream3.space.s:get{1}

    stream1:rollback()

    cg.server:exec(function()
        box.space.s:insert{0, 0}
    end)

    t.assert_equals(stream3.space.s:select{}, {})
    t.assert_error_msg_content_equals('Transaction has been aborted by conflict',
                                      function() stream3.space.s:replace {0, 0} end)
end

--[[
Checks that during rollback read tracker is rebound in all indexes for stories with different key parts.
]]
g.test_rollback_rebind_read_tracker_for_stories_with_different_key_parts = function(cg)
    local stream1 = cg.server.net_box:new_stream()
    local stream2 = cg.server.net_box:new_stream()
    local stream3 = cg.server.net_box:new_stream()

    stream1:begin()
    stream2:begin()
    stream3:begin()

    stream1.space.s:replace{0, 1}

    stream3.space.s.index[1]:get{1}
    stream3.space.s:get{0}

    stream2.space.s:insert{1, 1}

    stream1:rollback()

    cg.server:exec(function()
        box.space.s:insert{0, 2}
    end)

    t.assert_equals(stream3.space.s:select{}, {})
    t.assert_error_msg_content_equals('Transaction has been aborted by conflict',
                                      function() stream3.space.s:replace {0, 0} end)
end
