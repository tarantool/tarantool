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
        s:create_index('pk')
        s:create_index('mk', {unique = false,
                              parts = {{2, 'unsigned', path = '[*]'}}})
    end)
end)

g.after_each(function(cg)
    cg.server:exec(function()
        box.space.s:drop()
    end)
end)

-- Checks that dirty reads over multikey index are handled correctly.
g.test_dirty_reads = function(cg)
    local stream1 = cg.server.net_box:new_stream()
    local stream2 = cg.server.net_box:new_stream()
    local stream3 = cg.server.net_box:new_stream()
    local stream4 = cg.server.net_box:new_stream()
    local stream5 = cg.server.net_box:new_stream()
    local stream6 = cg.server.net_box:new_stream()

    stream1:begin()
    stream2:begin()
    stream3:begin()
    stream4:begin()
    stream5:begin()
    stream6:begin()

    stream1.space.s:replace{0, {0, 1}}
    stream2.space.s:replace{0, {1, 2}}

    t.assert_equals(stream3.space.s.index.mk:select{}, {})
    t.assert_equals(stream4.space.s.index.mk:select{0}, {})
    t.assert_equals(stream5.space.s.index.mk:select{1}, {})
    -- Checks that reading a key indirectly related to the committed key is
    -- detected.
    t.assert_equals(stream6.space.s.index.mk:select{2}, {})

    stream1:commit()
    stream2:commit()

    t.assert_error_msg_equals('Transaction has been aborted by conflict',
                              function() stream3.space.s:replace{0, {0}} end)
    t.assert_error_msg_equals('Transaction has been aborted by conflict',
                              function() stream4.space.s:replace{0, {0}} end)
    t.assert_error_msg_equals('Transaction has been aborted by conflict',
                              function() stream5.space.s:replace{0, {0}} end)
    t.assert_error_msg_equals('Transaction has been aborted by conflict',
                              function() stream6.space.s:replace{0, {0}} end)
end

-- Checks that non-repeatable reads over multikey index are handled correctly.
g.test_non_repeatable_reads = function(cg)
    local stream1 = cg.server.net_box:new_stream()
    local stream2 = cg.server.net_box:new_stream()
    local stream3 = cg.server.net_box:new_stream()
    local stream4 = cg.server.net_box:new_stream()
    local stream5 = cg.server.net_box:new_stream()
    local stream6 = cg.server.net_box:new_stream()

    stream1:begin()
    stream2:begin()
    stream3:begin()
    stream4:begin()
    stream5:begin()
    stream6:begin()

    stream1.space.s:replace{0, {0, 1}}
    stream2.space.s:replace{0, {1, 2}}

    t.assert_equals(stream3.space.s.index.mk:select{}, {})
    t.assert_equals(stream4.space.s.index.mk:select{0}, {})
    t.assert_equals(stream5.space.s.index.mk:select{1}, {})
    -- Checks that reading a key indirectly related to the committed key is
    -- detected.
    t.assert_equals(stream6.space.s.index.mk:select{2}, {})

    stream1:commit()
    stream2:commit()

    t.assert_equals(stream3.space.s.index.mk:select{}, {})
    t.assert_equals(stream4.space.s.index.mk:select{0}, {})
    t.assert_equals(stream5.space.s.index.mk:select{1}, {})
    t.assert_equals(stream6.space.s.index.mk:select{2}, {})

    t.assert_error_msg_equals('Transaction has been aborted by conflict',
                              function() stream3.space.s:replace{0, {0}} end)
    t.assert_error_msg_equals('Transaction has been aborted by conflict',
                              function() stream4.space.s:replace{0, {0}} end)
    t.assert_error_msg_equals('Transaction has been aborted by conflict',
                              function() stream5.space.s:replace{0, {0}} end)
    t.assert_error_msg_equals('Transaction has been aborted by conflict',
                              function() stream6.space.s:replace{0, {0}} end)
end

-- Checks that phantom reads over multikey index are handled correctly.
g.test_phantom_reads = function(cg)
    local stream1 = cg.server.net_box:new_stream()
    local stream2 = cg.server.net_box:new_stream()
    local stream3 = cg.server.net_box:new_stream()
    local stream4 = cg.server.net_box:new_stream()
    local stream5 = cg.server.net_box:new_stream()
    local stream6 = cg.server.net_box:new_stream()

    stream1:begin()
    stream2:begin()
    stream3:begin()
    stream4:begin()
    stream5:begin()
    stream6:begin()

    t.assert_equals(stream3.space.s.index.mk:select{}, {})
    t.assert_equals(stream4.space.s.index.mk:select{0}, {})
    t.assert_equals(stream5.space.s.index.mk:select{1}, {})
    -- Checks that reading a key indirectly related to the committed key is
    -- detected.
    t.assert_equals(stream6.space.s.index.mk:select{2}, {})

    stream1.space.s:replace{0, {0, 1}}
    stream2.space.s:replace{0, {1, 2}}

    stream1:commit()
    stream2:commit()

    t.assert_error_msg_equals('Transaction has been aborted by conflict',
                              function() stream3.space.s:replace{0, {0}} end)
    t.assert_error_msg_equals('Transaction has been aborted by conflict',
                              function() stream4.space.s:replace{0, {0}} end)
    t.assert_error_msg_equals('Transaction has been aborted by conflict',
                              function() stream5.space.s:replace{0, {0}} end)
    t.assert_error_msg_equals('Transaction has been aborted by conflict',
                              function() stream6.space.s:replace{0, {0}} end)
end

-- Checks that gap tracking over multikey index is handled correctly.
g.test_phantom_reads = function(cg)
    local stream1 = cg.server.net_box:new_stream()
    local stream2 = cg.server.net_box:new_stream()
    local stream3 = cg.server.net_box:new_stream()
    local stream4 = cg.server.net_box:new_stream()
    local stream5 = cg.server.net_box:new_stream()
    local stream6 = cg.server.net_box:new_stream()
    local stream7 = cg.server.net_box:new_stream()
    local stream8 = cg.server.net_box:new_stream()
    local stream9 = cg.server.net_box:new_stream()
    local stream10 = cg.server.net_box:new_stream()
    local stream11 = cg.server.net_box:new_stream()
    local stream12 = cg.server.net_box:new_stream()
    local stream13 = cg.server.net_box:new_stream()

    cg.server:exec(function()
        box.space.s.index.mk:drop()
        box.space.s:create_index('mk', {unique = false,
                                        parts = {
                                                {2, 'unsigned', path = '[*]'},
                                                {3, 'unsigned'}
                                        }})
        box.space.s:replace{0, {1, 3, 5, 7, 9, 11}, 0}
    end)

    stream1:begin()
    stream2:begin()
    stream3:begin()
    stream4:begin()
    stream5:begin()
    stream6:begin()
    stream7:begin()
    stream8:begin()
    stream9:begin()
    stream10:begin()
    stream11:begin()
    stream12:begin()
    stream13:begin()

    stream1.space.s.index.mk:select{}
    stream2.space.s.index.mk:select({2, 0}, {iterator = 'EQ'})
    stream3.space.s.index.mk:select({2}, {iterator = 'EQ'})
    stream4.space.s.index.mk:select({3, 0}, {iterator = 'GE'})
    stream5.space.s.index.mk:select({3}, {iterator = 'GE'})
    stream6.space.s.index.mk:select({5, 0}, {iterator = 'GT'})
    stream7.space.s.index.mk:select({5}, {iterator = 'GT'})
    stream8.space.s.index.mk:select({8, 0}, {iterator = 'REQ'})
    stream9.space.s.index.mk:select({8}, {iterator = 'REQ'})
    stream10.space.s.index.mk:select({9, 0}, {iterator = 'LE'})
    stream11.space.s.index.mk:select({9}, {iterator = 'LE'})
    stream12.space.s.index.mk:select({11, 0}, {iterator = 'LT'})
    stream13.space.s.index.mk:select({11}, {iterator = 'LT'})

    cg.server:exec(function()
        box.space.s:replace{1, {0, 2, 4, 6, 8, 10}, 0}
    end)

    t.assert_error_msg_equals('Transaction has been aborted by conflict',
                              function() stream1.space.s:replace{0, {0}} end)
    t.assert_error_msg_equals('Transaction has been aborted by conflict',
                              function() stream2.space.s:replace{0, {0}} end)
    t.assert_error_msg_equals('Transaction has been aborted by conflict',
                              function() stream3.space.s:replace{0, {0}} end)
    t.assert_error_msg_equals('Transaction has been aborted by conflict',
                              function() stream4.space.s:replace{0, {0}} end)
    t.assert_error_msg_equals('Transaction has been aborted by conflict',
                              function() stream5.space.s:replace{0, {0}} end)
    t.assert_error_msg_equals('Transaction has been aborted by conflict',
                              function() stream6.space.s:replace{0, {0}} end)
    t.assert_error_msg_equals('Transaction has been aborted by conflict',
                              function() stream7.space.s:replace{0, {0}} end)
    t.assert_error_msg_equals('Transaction has been aborted by conflict',
                              function() stream8.space.s:replace{0, {0}} end)
    t.assert_error_msg_equals('Transaction has been aborted by conflict',
                              function() stream9.space.s:replace{0, {0}} end)
    t.assert_error_msg_equals('Transaction has been aborted by conflict',
                              function() stream10.space.s:replace{0, {0}} end)
    t.assert_error_msg_equals('Transaction has been aborted by conflict',
                              function() stream11.space.s:replace{0, {0}} end)
    t.assert_error_msg_equals('Transaction has been aborted by conflict',
                              function() stream12.space.s:replace{0, {0}} end)
    t.assert_error_msg_equals('Transaction has been aborted by conflict',
                              function() stream13.space.s:replace{0, {0}} end)
end
