local server = require('test.luatest_helpers.server')
local t = require('luatest')

local g_bitset = t.group(nil, t.helpers.matrix{idx  = {'BITSET'},
                                               iter = {'ALL', 'EQ', 'BITS_ALL_SET',
                                                'BITS_ANY_SET',
                                                'BITS_ALL_NOT_SET'}})

g_bitset.before_all(function(cg)
    cg.server = server:new{
        alias   = 'dflt',
        box_cfg = {memtx_use_mvcc_engine = true}
    }
    cg.server:start()
end)

g_bitset.after_all(function(cg)
    cg.server:drop()
end)

g_bitset.before_each(function(cg)
    cg.server:exec(function()
        local s = box.schema.create_space('s')
        s:create_index('pk')
        s:create_index('sk', {type = 'BITSET', unique = false,
                              parts = {{2, 'uint'}}})
    end)
end)

g_bitset.after_each(function(cg)
    cg.server:exec(function()
        box.space.s:drop()
    end)
end)

-- Checks that reads are tracked correctly for all BITSET index iterator types.
g_bitset.test_read_tracking = function(cg)
    local stream1 = cg.server.net_box:new_stream()
    local stream2 = cg.server.net_box:new_stream()

    stream1:begin()
    stream2:begin()

    stream1.space.s.index[1]:select(0, {iterator = cg.params.iter})
    stream2.space.s:insert{0, 0}
    stream1.space.s:insert{1, 0}

    stream2:commit()

    local conflict_err_msg = 'Transaction has been aborted by conflict'
    t.assert_error_msg_content_equals(conflict_err_msg, function()
        stream1:commit()
    end)
end

local g_rtree = t.group(nil, t.helpers.matrix{idx  = {'RTREE'},
                                              iter = {'ALL', 'EQ', 'GT', 'GE',
                                                      'LT', 'LE', 'OVERLAPS',
                                                      'NEIGHBOR'}})

g_rtree.before_all(function(cg)
    cg.server = server:new{
        alias   = 'dflt',
        box_cfg = {memtx_use_mvcc_engine = true}
    }
    cg.server:start()
end)

g_rtree.after_all(function(cg)
    cg.server:drop()
end)

g_rtree.before_each(function(cg)
    cg.server:exec(function()
        local s = box.schema.create_space('s')
        s:create_index('pk')
        s:create_index('sk', {type = 'RTREE', unique = false,
                              parts = {{2, 'array'}}})
    end)
end)

g_rtree.after_each(function(cg)
    cg.server:exec(function()
        box.space.s:drop()
    end)
end)

-- Checks that reads are tracked correctly for all RTREE index iterator types.
g_rtree.test_read_tracking = function(cg)
    local stream1 = cg.server.net_box:new_stream()
    local stream2 = cg.server.net_box:new_stream()

    stream1:begin()
    stream2:begin()

    stream1.space.s.index[1]:select({0, 0, 1, 1}, {iterator = cg.params.iter})
    stream2.space.s:insert{0, {0, 0}}
    stream1.space.s:insert{1, {0, 0}}

    stream2:commit()

    local conflict_err_msg = 'Transaction has been aborted by conflict'
    t.assert_error_msg_content_equals(conflict_err_msg, function()
        stream1:commit()
    end)

    cg.server:exec(function()
        box.space.s:delete{0}
    end)

    stream1:begin()
    stream2:begin()

    stream1.space.s.index[1]:select({0, 0}, {iterator = cg.params.iter})
    stream2.space.s:insert{0, {0, 0}}
    stream1.space.s:insert{1, {0, 0}}

    stream2:commit()

    local conflict_err_msg = 'Transaction has been aborted by conflict'
    t.assert_error_msg_content_equals(conflict_err_msg, function()
        stream1:commit()
    end)
end
