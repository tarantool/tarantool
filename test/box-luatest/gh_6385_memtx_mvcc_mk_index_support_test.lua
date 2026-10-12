local server = require('luatest.server')
local t = require('luatest')

local g = t.group('gh-6385', {
    {index_opts = {parts = {{field = 2, path = '[*]'}}, unique = true}},
    {index_opts = {parts = {{1, 'unsigned'}}, func = 'f', unique = true}},
})

g.before_all(function(cg)
    cg.server = server:new{
        box_cfg = {memtx_use_mvcc_engine = true}
    }
    cg.server:start()
    cg.wal_queue_max_size = cg.server:exec(function()
        box.schema.func.create('f', {
            body = [[
                function(tuple)
                    local keys = {}
                    for _, k in ipairs(tuple[2]) do
                        table.insert(keys, {k})
                    end
                    return keys
                end
            ]],
            is_deterministic = true,
            is_sandboxed = true,
            opts = {is_multikey = true},
        })
        return box.cfg.wal_queue_max_size
    end)
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.before_each(function(cg)
    cg.server:exec(function(index_opts)
        local s = box.schema.create_space('s')
        s:create_index('pk')
        s:create_index('mk', index_opts)
        box.internal.memtx_tx_gc(100)
    end, {cg.params.index_opts})
end)

g.after_each(function(cg)
    cg.server:exec(function()
        box.space.s:drop()
    end)
end)

-- Check dirty reads over multikey index are handled correctly.
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

    stream1.space.s:replace{0, {0, 1, 0}}
    stream2.space.s:replace{0, {1, 2, 1}}

    t.assert_equals(stream3.space.s.index.mk:select{}, {})
    t.assert_equals(stream4.space.s.index.mk:select{0}, {})
    t.assert_equals(stream5.space.s.index.mk:select{1}, {})
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

-- Check non-repeatable reads over multikey index are handled correctly.
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

    stream1.space.s:replace{0, {0, 1, 0}}
    stream2.space.s:replace{0, {1, 2, 1}}

    t.assert_equals(stream3.space.s.index.mk:select{}, {})
    t.assert_equals(stream4.space.s.index.mk:select{0}, {})
    t.assert_equals(stream5.space.s.index.mk:select{1}, {})
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

-- Check phantom reads over multikey index are handled correctly.
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
    t.assert_equals(stream6.space.s.index.mk:select{2}, {})

    stream1.space.s:replace{0, {0, 1, 0}}
    stream2.space.s:replace{0, {1, 2, 1}}

    stream1:commit()
    stream2:commit()

    t.assert_equals(cg.server.net_box.space.s.index.mk:select{},
                    {{0, {1, 2, 1}}, {0, {1, 2, 1}}})

    t.assert_error_msg_equals('Transaction has been aborted by conflict',
                              function() stream3.space.s:replace{0, {0}} end)
    t.assert_error_msg_equals('Transaction has been aborted by conflict',
                              function() stream4.space.s:replace{10, {10}} end)
    t.assert_error_msg_equals('Transaction has been aborted by conflict',
                              function() stream5.space.s:replace{0, {0}} end)
    t.assert_error_msg_equals('Transaction has been aborted by conflict',
                              function() stream6.space.s:replace{0, {0}} end)
end

g.before_test('test_gap_tracking', function(cg)
    if cg.params.index_opts.func == nil then
       cg.server:exec(function()
           box.space.s.index.mk:drop()
           local parts = {{2, 'unsigned', path = '[*]'}, {3, 'unsigned'}}
           box.space.s:create_index('mk', {unique = true, parts = parts})
        end)
    end
end)

-- Check gap tracking over multikey index is handled correctly.
g.test_gap_tracking = function(cg)
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

    local tuple = {0, {1, 3, 5, 7, 9, 11, 9, 7, 5, 3, 1}, 0}
    cg.server:exec(function(tuple)
        box.space.s:replace(tuple)
    end, {tuple})

    local index_is_func = cg.params.index_opts.func ~= nil

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

    local function check_gaps()
        t.assert_equals(stream1.space.s.index.mk:select{},
                    {tuple, tuple, tuple, tuple, tuple, tuple})
        t.assert_equals(stream3.space.s.index.mk:select({2}, {iterator = 'EQ'}),
                        {})
        t.assert_equals(stream5.space.s.index.mk:select({3}, {iterator = 'GE'}),
                        {tuple, tuple, tuple, tuple, tuple})
        t.assert_equals(stream7.space.s.index.mk:select({5}, {iterator = 'GT'}),
                        {tuple, tuple, tuple})

        t.assert_equals(
            stream9.space.s.index.mk:select({8}, {iterator = 'REQ'}), {})
        t.assert_equals(
            stream11.space.s.index.mk:select({9}, {iterator = 'LE'}),
            {tuple, tuple, tuple, tuple, tuple})
        t.assert_equals(
            stream13.space.s.index.mk:select({11}, {iterator = 'LT'}),
            {tuple, tuple, tuple, tuple, tuple})
        if not index_is_func then
            t.assert_equals(
                stream2.space.s.index.mk:select({2, 0},
                                                {iterator = 'EQ'}), {})
            t.assert_equals(
                stream4.space.s.index.mk:select({3, 0},
                                                {iterator = 'GE'}),
                {tuple, tuple, tuple, tuple, tuple})
            t.assert_equals(
                stream6.space.s.index.mk:select({5, 0},
                                                {iterator = 'GT'}),
                {tuple, tuple, tuple})
            t.assert_equals(
                stream8.space.s.index.mk:select({8, 0},
                                                {iterator = 'REQ'}), {})
            t.assert_equals(
                stream10.space.s.index.mk:select({9, 0},
                                                 {iterator = 'LE'}),
                {tuple, tuple, tuple, tuple, tuple})
            t.assert_equals(stream12.space.s.index.mk:select({11, 0},
                                                         {iterator = 'LT'}),
                        {tuple, tuple, tuple, tuple, tuple})
        end
    end

    check_gaps()
    cg.server:exec(function()
        box.space.s:replace{1, {0, 2, 4, 6, 8, 10, 8, 6, 4, 2, 0}, 0}
    end)
    -- Check read gaps are repeatable.
    check_gaps()

    t.assert_error_msg_equals('Transaction has been aborted by conflict',
                              function() stream1.space.s:replace{0, {0}} end)
    t.assert_error_msg_equals('Transaction has been aborted by conflict',
                              function() stream3.space.s:replace{0, {0}} end)
    t.assert_error_msg_equals('Transaction has been aborted by conflict',
                              function() stream5.space.s:replace{0, {0}} end)
    t.assert_error_msg_equals('Transaction has been aborted by conflict',
                              function() stream7.space.s:replace{0, {0}} end)
    t.assert_error_msg_equals('Transaction has been aborted by conflict',
                              function() stream9.space.s:replace{0, {0}} end)
    t.assert_error_msg_equals('Transaction has been aborted by conflict',
                              function() stream11.space.s:replace{0, {0}} end)
    t.assert_error_msg_equals('Transaction has been aborted by conflict',
                              function() stream13.space.s:replace{0, {0}} end)
    if not index_is_func then
        t.assert_error_msg_equals('Transaction has been aborted by conflict',
                              function() stream2.space.s:replace{0, {0}} end)
        t.assert_error_msg_equals('Transaction has been aborted by conflict',
                          function() stream4.space.s:replace{0, {0}} end)
        t.assert_error_msg_equals('Transaction has been aborted by conflict',
                          function() stream6.space.s:replace{0, {0}} end)
        t.assert_error_msg_equals('Transaction has been aborted by conflict',
                          function() stream8.space.s:replace{0, {0}} end)
        t.assert_error_msg_equals('Transaction has been aborted by conflict',
                              function() stream10.space.s:replace{0, {0}} end)
        t.assert_error_msg_equals('Transaction has been aborted by conflict',
                              function() stream12.space.s:replace{0, {0}} end)
    end
end

-- Check gap tracking survives a tuple with duplicate multikey keys.
local function test_duplicate_key_gap_tracking(cg)
    local stream1 = cg.server.net_box:new_stream()
    local stream2 = cg.server.net_box:new_stream()
    local stream3 = cg.server.net_box:new_stream()
    local stream4 = cg.server.net_box:new_stream()

    stream1:begin()
    stream2:begin()
    stream3:begin()
    stream4:begin()

    t.assert_equals(stream1.space.s.index.mk:select{1}, {})
    t.assert_equals(stream2.space.s.index.mk:select({1}, {iterator = 'REQ'}),
                    {})
    t.assert_equals(stream3.space.s.index.mk:select({1}, {iterator = 'LE'}),
                    {})
    t.assert_equals(stream4.space.s.index.mk:select({6}, {iterator = 'LE'}),
                    {})

    cg.server.net_box.space.s:replace{8, {8, 5, 8}}
    cg.server.net_box.space.s:replace{2, {1}}

    t.assert_error_msg_equals('Transaction has been aborted by conflict',
                              function() stream1.space.s:replace{0, {0}} end)
    t.assert_error_msg_equals('Transaction has been aborted by conflict',
                              function() stream2.space.s:replace{0, {0}} end)
    t.assert_error_msg_equals('Transaction has been aborted by conflict',
                              function() stream3.space.s:replace{0, {0}} end)
    t.assert_error_msg_equals('Transaction has been aborted by conflict',
                              function() stream4.space.s:replace{0, {0}} end)
end

g.test_duplicate_key_gap_tracking = function(cg)
    test_duplicate_key_gap_tracking(cg)
end

g.before_test('test_duplicate_key_gap_tracking_non_unique', function(cg)
    cg.server:exec(function(index_opts)
        box.space.s.index.mk:drop()
        index_opts.unique = false
        box.space.s:create_index('mk', index_opts)
    end, {cg.params.index_opts})
end)

g.test_duplicate_key_gap_tracking_non_unique = function(cg)
    test_duplicate_key_gap_tracking(cg)
end

-- Check that a successor key stays alive for the duration of the transaction.
g.test_successor_key_lifetime = function(cg)
    cg.server:exec(function()
        local s = box.space.s
        local old_tuple = {3, {3}}
        local new_tuple = {1, {2, 1, 2}}
        local expected = {new_tuple, new_tuple, old_tuple}

        s:replace(old_tuple)

        box.begin()
        s:replace(new_tuple)
        t.assert_equals(s.index.mk:select{}, expected)
        box.rollback()
        t.assert_equals(s.index.mk:select{}, {old_tuple})

        box.begin()
        s:replace(new_tuple)
        t.assert_equals(s.index.mk:select{}, expected)
        box.commit()
        t.assert_equals(s.index.mk:select{}, expected)
    end)
end

-- Check range delete read and write conflicts over multikey index variants.
g.test_range_delete_conflicts = function(cg)
    cg.server:exec(function()
        local txn_proxy = require('test.box.lua.txn_proxy')
        local s = box.space.s
        local conflict = {
            {error = 'Transaction has been aborted by conflict'},
        }
        local success = ''

        s:replace{10, {100}}
        s:replace{20, {200}}
        s:replace{30, {300}}
        s:replace{40, {400}}
        s:replace{50, {500}}

        local range = txn_proxy.new()
        local writer = txn_proxy.new()
        range:begin()
        range('box.space.s:delete_range(10, 11)')
        writer:begin()
        writer('box.space.s:replace{10, {101}}')
        t.assert_equals(writer:commit(), success)
        t.assert_equals(range('box.space.s:replace{90, {900}}'),
                        conflict)
        range:rollback()

        range = txn_proxy.new()
        writer = txn_proxy.new()
        range:begin()
        range('box.space.s:delete_range(20, 30)')
        writer:begin()
        writer('box.space.s:replace{25, {250}}')
        t.assert_equals(writer:commit(), success)
        t.assert_equals(range('box.space.s:replace{91, {910}}'),
                        conflict)
        range:rollback()

        range = txn_proxy.new()
        writer = txn_proxy.new()
        range:begin()
        range('box.space.s:delete_range(41, 49)')
        writer:begin()
        writer('box.space.s:replace{45, {450}}')
        t.assert_equals(writer:commit(), success)
        t.assert_equals(range('box.space.s:replace{92, {920}}'),
                        conflict)
        range:rollback()

        s:replace{60, {600, 601}}
        s:replace{61, {610, 611}}

        local point_reader = txn_proxy.new()
        local gap_reader = txn_proxy.new()
        local hole_reader = txn_proxy.new()
        local deleter = txn_proxy.new()
        point_reader:begin()
        gap_reader:begin()
        hole_reader:begin()
        point_reader('box.space.s.index.mk:get{600}')
        gap_reader([[box.space.s.index.mk:select({605},
                    {iterator = 'GE', limit = 1})]])
        hole_reader('box.space.s.index.mk:get{605}')

        deleter:begin()
        deleter('box.space.s:delete_range(60, 62)')
        t.assert_equals(deleter:commit(), success)

        t.assert_equals(point_reader(
                            'box.space.s:replace{70, {700}}'),
                        conflict)
        point_reader:rollback()
        t.assert_equals(gap_reader(
                            'box.space.s:replace{71, {710}}'),
                        conflict)
        gap_reader:rollback()
        hole_reader('box.space.s:replace{72, {720}}')
        t.assert_equals(hole_reader:commit(), success)
    end)
end

-- Check GC of present story is handled correctly.
g.test_gc_of_present_new_story = function(cg)
    local stream1 = cg.server.net_box:new_stream()
    local stream2 = cg.server.net_box:new_stream()
    local stream3 = cg.server.net_box:new_stream()

    stream1:begin()
    stream2:begin()
    stream3:begin()

    stream1.space.s:replace{0, {0, 1, 0}}
    t.assert_equals(stream3.space.s.index.mk:select{}, {})
    stream2.space.s:replace{0, {1, 0, 1}}

    stream1:commit()
    stream2:rollback()
    stream3:commit()

    cg.server:exec(function()
        box.internal.memtx_tx_gc(100)
        t.assert_equals(box.space.s.index.mk:select{},
                        {{0, {0, 1, 0}}, {0, {0, 1, 0}}})
    end)
end

-- Check GC of deleted old story is handled correctly.
g.test_gc_of_deleted_new_story = function(cg)
    local stream1 = cg.server.net_box:new_stream()
    local stream2 = cg.server.net_box:new_stream()
    local stream3 = cg.server.net_box:new_stream()

    stream1:begin()
    stream2:begin()
    stream3:begin()

    stream1.space.s:replace{0, {0, 1, 0}}
    t.assert_equals(stream3.space.s.index.mk:select{}, {})
    stream2.space.s:replace{0, {1, 0, 1}}

    stream1:commit()
    stream2:commit()
    stream3:commit()

    cg.server:exec(function()
        box.internal.memtx_tx_gc(100)
        t.assert_equals(box.space.s.index.mk:select{},
                        {{0, {1, 0, 1}}, {0, {1, 0, 1}}})
    end)
end

-- Check GC of present old story is handled correctly.
g.test_gc_of_present_old_story = function(cg)
    local stream1 = cg.server.net_box:new_stream()
    local stream2 = cg.server.net_box:new_stream()

    cg.server:exec(function()
        box.space.s:replace{0, {0, 1, 0}}
        box.internal.memtx_tx_gc(100)
    end)
    -- The story for {0, {0, 1}} will need to be re-created.
    stream1:begin()

    stream1.space.s:replace{0, {1, 0, 1}}
    t.assert_equals(stream2.space.s.index.mk:select{},
                    {{0, {0, 1, 0}}, {0, {0, 1, 0}}})
    stream1:rollback()
    cg.server:exec(function()
        box.internal.memtx_tx_gc(100)
        t.assert_equals(box.space.s.index.mk:select{},
                        {{0, {0, 1, 0}}, {0, {0, 1, 0}}})
    end)
end

-- Check GC of deleted old story is handled correctly.
g.test_gc_of_deleted_old_story = function(cg)
    local stream1 = cg.server.net_box:new_stream()
    local stream2 = cg.server.net_box:new_stream()

    cg.server:exec(function()
        box.space.s:replace{0, {0, 1, 0}}
        box.internal.memtx_tx_gc(100)
    end)
    -- The story for {0, {0, 1}} will need to be re-created.
    stream1:begin()
    stream1.space.s:replace{0, {1, 0, 1}}
    t.assert_equals(stream2.space.s.index.mk:select{},
                    {{0, {0, 1, 0}}, {0, {0, 1, 0}}})
    stream1:commit()
    cg.server:exec(function()
        box.internal.memtx_tx_gc(100)
        t.assert_equals(box.space.s.index.mk:select{},
                        {{0, {1, 0, 1}}, {0, {1, 0, 1}}})
    end)
end

-- Check GC keeps overlapping key that was replaced by another story.
g.test_gc_keeps_overlapping_keys = function(cg)
    local stream = cg.server.net_box:new_stream()

    cg.server:exec(function()
        box.space.s:replace{0, {1, 2}}
        box.internal.memtx_tx_gc(100)
    end)

    stream:begin()
    t.assert_equals(stream.space.s.index.mk:get{2}, {0, {1, 2}})

    cg.server:exec(function()
        box.space.s:replace{0, {2, 3}}
    end)

    stream:commit()

    cg.server:exec(function()
        box.internal.memtx_tx_gc(100)
        t.assert_equals(box.space.s.index.mk:get{1}, nil)
        t.assert_equals(box.space.s.index.mk:get{2}, {0, {2, 3}})
        t.assert_equals(box.space.s.index.mk:get{3}, {0, {2, 3}})
    end)
end

-- Check GC keeps overlapping keys that were replaced by other stories.
g.test_gc_of_middle_version_keeps_newer_overlapping_keys = function(cg)
    local stream1 = cg.server.net_box:new_stream()
    local stream2 = cg.server.net_box:new_stream()

    cg.server:exec(function()
        box.space.s:replace{0, {1, 2}}
        box.internal.memtx_tx_gc(100)
    end)

    stream1:begin()
    t.assert_equals(stream1.space.s.index.mk:get{2}, {0, {1, 2}})

    cg.server:exec(function()
        box.space.s:replace{0, {2, 3}}
    end)

    stream2:begin()
    t.assert_equals(stream2.space.s.index.mk:get{ 3}, {0, {2, 3}})

    cg.server:exec(function()
        box.space.s:replace{0, {3, 4}}
    end)

    stream1:commit()
    stream2:commit()

    cg.server:exec(function()
        box.internal.memtx_tx_gc(100)
        t.assert_equals(box.space.s.index.mk:get{1}, nil)
        t.assert_equals(box.space.s.index.mk:get{2}, nil)
        t.assert_equals(box.space.s.index.mk:get{3}, {0, {3, 4}})
        t.assert_equals(box.space.s.index.mk:get{4}, {0, {3, 4}})
    end)
end

-- Check abort of old space schema reader is handled correctly.
g.test_abort_old_space_schema_reader = function(cg)
    local stream = cg.server.net_box:new_stream()

    cg.server:exec(function()
        box.space.s:replace{0, {0, 1, 0}}
    end)

    stream:begin()
    t.assert_equals(stream.space.s.index.mk:select{},
                    {{0, {0, 1, 0}}, {0, {0, 1, 0}}})
    cg.server:exec(function()
        box.space.s:create_index('sk')
    end)

    local err_msg = 'Transaction has been aborted by conflict'
    t.assert_error_msg_content_equals(err_msg, function() stream:commit() end)
end

local function test_count_on_op(cg, op)
    local stream1 = cg.server.net_box:new_stream()
    local stream2 = cg.server.net_box:new_stream()

    cg.server:exec(function()
        box.space.s:replace{0, {0, 1, 0}}
        -- Count must rely on gap tracking, not on an existing story.
        box.internal.memtx_tx_gc(100)
    end)

    stream1:begin()

    t.assert_equals(stream1.space.s.index.mk:count{}, 2)
    if op == 'replace' then
        stream2.space.s:replace{0, {1, 2, 1}}
    elseif op == 'replace_dropping_entries' then
        -- Nothing is inserted into the index, only the entry of key 0 is
        -- dropped.
        stream2.space.s:replace{0, {1}}
    else
        stream2.space.s:delete{0}
    end
    t.assert_equals(stream1.space.s.index.mk:count{}, 2)

    local err_msg = 'Transaction has been aborted by conflict'
    t.assert_error_msg_content_equals(err_msg, function()
        stream1.space.s:replace{0, {0, 1, 0}}
    end)
end

-- Check count conflict works correctly on insertion of tuple.
g.test_count_on_insert = function(cg)
    test_count_on_op(cg, 'replace')
end

-- Check count conflict works correctly on deletion of tuple.
g.test_count_on_delete = function(cg)
    test_count_on_op(cg, 'delete')
end

-- Check count conflict works correctly on replace dropping index entries.
g.test_count_on_replace_dropping_entries = function(cg)
    test_count_on_op(cg, 'replace_dropping_entries')
end

-- Check space invalidation on DDL works correctly.
g.test_space_invalidation = function(cg)
    local stream1 = cg.server.net_box:new_stream()
    local stream2 = cg.server.net_box:new_stream()
    local stream3 = cg.server.net_box:new_stream()

    cg.server:exec(function()
        box.space.s:replace{0, {1, 2, 1}}
    end)

    stream1:begin()
    stream2:begin()

    stream1.space.s:replace{0, {0, 1, 0}}
    stream2.space.s:replace{0, {2, 3, 2}}

    cg.server:exec(function()
        box.space.s:create_index('sk')
    end)

    local err_msg = 'Transaction has been aborted by conflict'
    t.assert_error_msg_content_equals(err_msg, function()
        stream1:commit()
    end)
    t.assert_error_msg_content_equals(err_msg, function()
        stream2:commit()
    end)

    t.assert_equals(stream3.space.s.index.mk:select{},
                    {{0, {1, 2, 1}}, {0, {1, 2, 1}}})
end

-- Check snapshot creation and recovery work correctly.
g.test_snapshot_and_recovery = function(cg)
    local stream1 = cg.server.net_box:new_stream()
    local stream2 = cg.server.net_box:new_stream()

    cg.server:exec(function()
        box.space.s:replace{0, {1, 2, 1}}
    end)

    stream1:begin()
    stream2:begin()

    stream1.space.s:replace{0, {0, 1, 0}}
    stream2.space.s:replace{0, {2, 3, 2}}

    cg.server:exec(function()
        box.snapshot()
    end)

    stream1:rollback()
    stream2:rollback()

    cg.server:restart({box_cfg = {memtx_use_mvcc_engine = true}})

    cg.server:exec(function()
        t.assert_equals(box.space.s.index.mk:select{},
                        {{0, {1, 2, 1}}, {0, {1, 2, 1}}})
    end)
end

-- Check read views over multikey index see each index entry separately.
g.test_read_view = function(cg)
    local stream = cg.server.net_box:new_stream()

    cg.server:exec(function()
        box.space.s:replace{0, {0, 1}}
    end)

    stream:begin()
    stream.space.s:replace{0, {0, 2}}

    cg.server:exec(function()
        local rv = box.read_view.open()
        local mk = rv.space.s.index.mk
        t.assert_equals(mk:select{}, {{0, {0, 1}}, {0, {0, 1}}})
        t.assert_equals(mk:select{0}, {{0, {0, 1}}})
        t.assert_equals(mk:select{1}, {{0, {0, 1}}})
        t.assert_equals(mk:select{2}, {})
        t.assert_equals(mk:count(), 2)
        rv:close()
    end)

    stream:rollback()
end

-- Check read views do not see deleted tuples whose story links were created
-- lazily: such links may identify the index entry by a different multikey
-- position (duplicate keys) or functional key tuple than the index itself.
g.test_read_view_lazy_links = function(cg)
    local stream1 = cg.server.net_box:new_stream()
    local stream2 = cg.server.net_box:new_stream()

    cg.server:exec(function()
        box.space.s:replace{0, {5, 5}}
        box.internal.memtx_tx_gc(100)
    end)

    -- Force the story and its links for the clean tuple, and keep the story
    -- alive after the tuple is deleted.
    stream1:begin()
    stream1.space.s:get{0}
    stream1.space.s.index.mk:count{}

    stream2.space.s:replace{0, {6}}

    cg.server:exec(function()
        box.internal.memtx_tx_gc(100)
        local rv = box.read_view.open()
        t.assert_equals(rv.space.s.index.mk:select{}, {{0, {6}}})
        t.assert_equals(rv.space.s.index.mk:select{5}, {})
        t.assert_equals(rv.space.s.index.mk:count(), 1)
        rv:close()
    end)

    stream1:rollback()
end

-- Check that a read view outlives the story of a deleted tuple whose index
-- entry is cleaned by the read view.
g.test_read_view_outlives_story = function(cg)
    local stream = cg.server.net_box:new_stream()

    cg.server:exec(function()
        box.space.s:replace{0, {5}}
        box.space.s:replace{1, {7}}
        box.internal.memtx_tx_gc(100)
    end)

    -- Keep the story of the deleted tuple alive until the read view is open.
    stream:begin()
    stream.space.s:get{0}
    cg.server:exec(function()
        box.space.s:delete{0}
        box.internal.memtx_tx_gc(100)
        rawset(_G, 'rv', box.read_view.open())
    end)
    stream:rollback()

    cg.server:exec(function()
        -- Garbage collect the story and reuse the released memory.
        box.internal.memtx_tx_gc(100)
        for i = 10, 40 do
            box.space.s:replace{i, {i}}
        end
        box.internal.memtx_tx_gc(100)
        local rv = rawget(_G, 'rv')
        t.assert_equals(rv.space.s.index.mk:select{}, {{1, {7}}})
        t.assert_equals(rv.space.s.index.mk:select{5}, {})
        rv:close()
        rawset(_G, 'rv', nil)
    end)
end

-- Check that a snapshot with sort data is written while the index has
-- invisible entries: the snapshot thread cleans them without tuple formats.
g.test_snapshot_sort_data_with_stories = function(cg)
    local stream = cg.server.net_box:new_stream()

    cg.server:exec(function()
        box.space.s:replace{0, {5, 5}}
        box.space.s:replace{1, {7}}
        box.internal.memtx_tx_gc(100)
    end)

    stream:begin()
    stream.space.s:replace{0, {5, 6}}
    stream.space.s:delete{1}
    stream.space.s:replace{2, {9}}

    cg.server:exec(function()
        box.cfg{memtx_use_sort_data = true}
        box.snapshot()
        box.cfg{memtx_use_sort_data = false}
    end)
    stream:rollback()
end

-- Check that a story link key moved into the index by a rollback outlives the
-- index entry in a read view, like the keys created by the index.
g.test_read_view_relinked_key = function(cg)
    local stream1 = cg.server.net_box:new_stream()
    local stream2 = cg.server.net_box:new_stream()

    cg.server:exec(function()
        box.space.s:replace{0, {5}}
        box.space.s:replace{1, {7}}
        box.internal.memtx_tx_gc(100)
    end)

    -- The delete creates the links of {0, {5}} with keys of its own, the
    -- rollback of the replacement moves them into the index.
    stream1:begin()
    stream1.space.s:delete{0}
    stream1.space.s:replace{0, {5}}
    stream1:rollback()

    -- Open a read view with a story to clean, so that the read view looks
    -- up its entries.
    stream2:begin()
    stream2.space.s:replace{2, {9}}
    cg.server:exec(function()
        box.internal.memtx_tx_gc(100)
        rawset(_G, 'rv', box.read_view.open())
    end)
    stream2:rollback()

    cg.server:exec(function()
        -- Remove the relinked entry from the index and reuse its memory.
        box.space.s:delete{0}
        box.internal.memtx_tx_gc(100)
        for i = 10, 40 do
            box.space.s:replace{i, {i}}
        end
        box.internal.memtx_tx_gc(100)
        local rv = rawget(_G, 'rv')
        t.assert_equals(rv.space.s.index.mk:select{}, {{0, {5}}, {1, {7}}})
        t.assert_equals(rv.space.s.index.mk:select{5}, {{0, {5}}})
        rv:close()
        rawset(_G, 'rv', nil)
    end)
end

-- Check that a rollback moving an entry back into the index does not
-- allocate: the links of {0, {5}} are created by the delete with keys of
-- their own, the replacement takes the index keys over to the links, and the
-- rollback moves them back while tuple allocation fails.
g.test_rollback_relinks_index_key = function(cg)
    cg.server:exec(function()
        local s = box.space.s
        s:replace{0, {5}}
        box.internal.memtx_tx_gc(100)
        box.begin()
        s:delete{0}
        s:replace{0, {5}}
        box.error.injection.set('ERRINJ_TUPLE_ALLOC', true)
        box.rollback()
        box.error.injection.set('ERRINJ_TUPLE_ALLOC', false)
        t.assert_equals(s.index.mk:select{}, {{0, {5}}})
        t.assert_equals(s.index.mk:select{5}, {{0, {5}}})
    end)
end

-- Check that a count tracker bounded by an index entry survives the
-- replacement of the bound entry: the bound is described by the key stored in
-- the index, which is released with the entry.
g.test_count_until_bound_replaced = function(cg)
    local stream1 = cg.server.net_box:new_stream()
    local stream2 = cg.server.net_box:new_stream()

    cg.server:exec(function()
        box.space.s:replace{0, {5}}
        box.space.s:replace{1, {6}}
        box.internal.memtx_tx_gc(100)
    end)

    stream1:begin()
    -- Materialize the stories, the garbage collector creates their links.
    stream1.space.s:get{0}
    stream1.space.s:get{1}
    cg.server:exec(function()
        box.internal.memtx_tx_gc(100)
    end)
    -- Skip one tuple and reach the bound {1, {6}}: the skipped tuples are
    -- tracked by a count up to the bound.
    t.assert_equals(stream1.space.s.index.mk:select({}, {offset = 1}),
                    {{1, {6}}})

    -- Replace the bound: its old index entry is released.
    stream2.space.s:replace{1, {6}}
    -- Reuse the released memory with a key that sorts after any other key.
    stream2.space.s:replace{2, {50}}
    cg.server:exec(function()
        box.internal.memtx_tx_gc(100)
    end)

    -- Insert into the index: the count tracker is checked against the bound.
    stream2.space.s:replace{3, {7}}
    t.assert_equals(stream1.space.s.index.mk:select({}, {offset = 1}),
                    {{1, {6}}})
    stream1:commit()
end

-- Check that a count tracker bounded by an index entry survives the rollback
-- and garbage collection of the story of the bound.
g.test_count_until_bound_rolled_back = function(cg)
    local stream1 = cg.server.net_box:new_stream()
    local stream2 = cg.server.net_box:new_stream()

    cg.server:exec(function()
        box.space.s:replace{0, {5}}
        box.space.s:replace{1, {7}}
        box.internal.memtx_tx_gc(100)
    end)

    -- The bound is the invisible replacement of {1, {7}}: the transaction
    -- reads the older version of the tuple and does not keep the bound alive.
    stream1:begin()
    stream1.space.s:replace{1, {7}}
    stream2:begin()
    t.assert_equals(stream2.space.s.index.mk:select({}, {offset = 1}),
                    {{1, {7}}})
    stream1:rollback()

    cg.server:exec(function()
        -- Garbage collect the story of the bound and reuse its memory.
        box.internal.memtx_tx_gc(100)
        for i = 10, 40 do
            box.space.s:replace{i, {i}}
        end
        for i = 10, 40 do
            box.space.s:delete{i}
        end
        box.internal.memtx_tx_gc(100)
        -- Insert into the index: the count tracker is checked against the
        -- bound.
        box.space.s:replace{2, {6}}
    end)
    t.assert_equals(stream2.space.s.index.mk:select({}, {offset = 1}),
                    {{1, {7}}})
    stream2:commit()
end

-- Check that a count conflicts with the replacement of a counted tuple, which
-- moves its key out of the counted range, while the counting transaction has
-- replaced the same tuple keeping the key.
g.test_count_conflicts_with_replace_of_own_replaced = function(cg)
    local stream1 = cg.server.net_box:new_stream()
    local stream2 = cg.server.net_box:new_stream()

    cg.server:exec(function()
        box.space.s:replace{0, {3}}
        box.space.s:replace{1, {2}}
        box.internal.memtx_tx_gc(100)
    end)

    stream1:begin()
    t.assert_equals(stream1.space.s.index.mk:count({3}, {iterator = 'LE'}),
                    2)
    stream1.space.s:replace{0, {3}}
    -- Serialized before stream1: its count becomes 1.
    stream2.space.s:replace{0, {5}}
    t.assert_error_msg_content_equals(
        'Transaction has been aborted by conflict',
        function() stream1:commit() end)
end

-- Run the scenario of a count conflict with a replace of a counted tuple
-- under an uncommitted version of its entry by another transaction, which is
-- rolled back later, on the index @a index_name. The replace retains or drops
-- the counted key 1, which has duplicate positions in the old and the new
-- tuple.
local function test_count_on_replace_under_uncommitted_version(cg, index_name,
                                                               drops_key)
    local stream1 = cg.server.net_box:new_stream()
    local stream2 = cg.server.net_box:new_stream()

    cg.server:exec(function()
        box.space.s:truncate()
        box.space.s:replace{0, {1, 1}}
        box.space.s:replace{1, {2, 2}}
        box.internal.memtx_tx_gc(100)
    end)

    local index = stream1.space.s.index[index_name]
    stream1:begin()
    t.assert_equals(index:count({2}, {iterator = 'LE'}), 2)
    -- Another transaction puts an uncommitted version of the entry of the
    -- key 1 on top of the counted one.
    stream2:begin()
    stream2.space.s:replace{0, {1, 1}}
    cg.server:exec(function(drops_key)
        box.space.s:replace{0, drops_key and {3, 3} or {1, 1}}
    end, {drops_key})
    stream2:rollback()
    -- The counting transaction either sees the same keys or is sent to a read
    -- view, which it cannot write from.
    t.assert_equals(index:count({2}, {iterator = 'LE'}), 2)
    if drops_key then
        t.assert_error_msg_content_equals(
            'Transaction has been aborted by conflict',
            function() stream1.space.s:replace{5, {5}} end)
    else
        stream1.space.s:replace{5, {5}}
        stream1:commit()
    end
end

-- Run @a scenario on the unique index of the space and on a non-unique one.
local function test_on_unique_and_non_unique_index(cg, scenario)
    scenario(cg, 'mk')
    cg.server:exec(function(index_opts)
        index_opts.unique = false
        box.space.s:create_index('nu', index_opts)
    end, {cg.params.index_opts})
    scenario(cg, 'nu')
end

-- Check that a replace retaining a counted key under an uncommitted version of
-- its entry does not conflict with the count.
g.test_count_on_replace_retaining_key_under_uncommitted_version = function(cg)
    test_on_unique_and_non_unique_index(cg, function(cg, index_name)
        test_count_on_replace_under_uncommitted_version(cg, index_name, false)
    end)
end

-- Check that a replace dropping a counted key under an uncommitted version of
-- its entry conflicts with the count.
g.test_count_on_replace_dropping_key_under_uncommitted_version = function(cg)
    test_on_unique_and_non_unique_index(cg, function(cg, index_name)
        test_count_on_replace_under_uncommitted_version(cg, index_name, true)
    end)
end

-- Check that an offset select outside of a transaction reaches a bound that
-- has no story.
g.test_count_until_bound_no_txn = function(cg)
    cg.server:exec(function()
        box.space.s:replace{0, {5}}
        box.space.s:replace{1, {6}}
        box.internal.memtx_tx_gc(100)
        t.assert_equals(box.space.s.index.mk:select({}, {offset = 1}),
                        {{1, {6}}})
    end)
end

-- Check sinking of story for commit preparation works correctly.
g.test_prepare_story_sink = function(cg)
    local stream1 = cg.server.net_box:new_stream()
    local stream2 = cg.server.net_box:new_stream()
    local stream3 = cg.server.net_box:new_stream()

    stream1:begin()
    stream2:begin()

    stream1.space.s:replace{0, {0, 1, 0}}
    stream2.space.s:replace{0, {1, 2, 1}}

    -- Stream 2's story is sunk along key {1}, but not along key {0}.
    stream2:commit()

    t.assert_equals(stream3.space.s.index.mk:select{},
                    {{0, {1, 2, 1}}, {0, {1, 2, 1}}})
    t.assert_equals(stream3.space.s.index.mk:get{0}, nil)

    -- Stream 1's story is sunk along keys {1}, but not along key {2}.
    stream1:commit()
    t.assert_equals(stream3.space.s.index.mk:select{},
                    {{0, {0, 1, 0}}, {0, {0, 1, 0}}})
    t.assert_equals(stream3.space.s.index.mk:get{2}, nil)
end

-- Check sinking of story for rollback works correctly.
g.test_rollback_story_sink = function(cg)
    local stream1 = cg.server.net_box:new_stream()
    local stream2 = cg.server.net_box:new_stream()
    local stream3 = cg.server.net_box:new_stream()

    cg.server:exec(function()
        box.space.s:replace{0, {1, 2, 1}}
    end)

    stream1:begin()
    stream2:begin()

    stream1.space.s:replace{0, {0, 1, 0}}
    stream2.space.s:replace{0, {2, 3, 2}}

    -- Stream 2's story is sunk along key {2}, but not along key {3}.
    stream2:rollback()

    t.assert_equals(stream3.space.s.index.mk:select{},
                    {{0, {1, 2, 1}}, {0, {1, 2, 1}}})
    t.assert_equals(stream3.space.s.index.mk:get{0}, nil)
    t.assert_equals(stream3.space.s.index.mk:get{3}, nil)

    -- Stream 1's story is sunk along keys {1}, but not along key {2}.
    stream1:rollback()
    t.assert_equals(stream3.space.s.index.mk:select{},
                    {{0, {1, 2, 1}}, {0, {1, 2, 1}}})
    t.assert_equals(stream3.space.s.index.mk:get{0}, nil)
    t.assert_equals(stream3.space.s.index.mk:get{3}, nil)
end

-- Check duplicate resolution on replace works correctly.
g.test_check_dup_on_replace = function(cg)
    local stream1 = cg.server.net_box:new_stream()
    local stream2 = cg.server.net_box:new_stream()

    cg.server:exec(function()
        box.space.s:replace{0, {1, 2, 1}}
    end)

    stream1:begin()
    stream2:begin()

    stream1.space.s:replace{0, {2, 3, 2}}

    local err_msg = 'Duplicate key exists in unique index \"mk\"'
    t.assert_error_msg_contains(err_msg, function()
        stream2.space.s:replace{1, {0, 1, 0}}
    end)

    stream1:commit()

    err_msg = 'Transaction has been aborted by conflict'
    t.assert_error_msg_content_equals(err_msg, function()
        stream2.space.s:replace{1, {0, 1, 0}}
    end)
end

-- Check duplicate handling on transaction preparation works correctly.
g.test_handle_dups_on_prepare = function(cg)
    local stream1 = cg.server.net_box:new_stream()
    local stream2 = cg.server.net_box:new_stream()
    local stream3 = cg.server.net_box:new_stream()
    local stream4 = cg.server.net_box:new_stream()

    stream1:begin()
    stream2:begin()
    stream3:begin()

    stream1.space.s:replace{0, {0, 1, 0}}
    stream2.space.s:replace{1, {1, 2, 1}}
    stream3.space.s:replace{0, {0, 1, 2, 2, 0, 1}}

    stream1:commit()

    local err_msg = 'Transaction has been aborted by conflict'
    t.assert_error_msg_content_equals(err_msg, function()
        stream2:commit()
    end)

    t.assert_equals(stream4.space.s.index.mk:select{},
                    {{0, {0, 1, 0}}, {0, {0, 1, 0}}})

    stream3:commit()

    t.assert_equals(stream4.space.s.index.mk:select{},
                    {{0, {0, 1, 2, 2, 0, 1}}, {0, {0, 1, 2, 2, 0, 1}},
                     {0, {0, 1, 2, 2, 0, 1}}})
end

local function test_handle_dups_and_gaps_on_rollback_of_prepared_op(cg, op)
    t.tarantool.skip_if_not_debug()

    local stream1 = cg.server.net_box:new_stream()
    local stream2 = cg.server.net_box:new_stream()
    local stream3 = cg.server.net_box:new_stream()
    local stream4 = cg.server.net_box:new_stream()

    cg.server:exec(function(op)
        box.space.s:replace{0, {0, 1, 0}}

        box.cfg{wal_queue_max_size = 1}
        box.error.injection.set('ERRINJ_WAL_DELAY', true)
        box.atomic({wait = 'none'}, function()
            box.space.s:replace{10, {10}}
        end)

        require('fiber').create(function()
            if op == 'replace' then
                box.space.s:replace{0, {1, 2, 1}}
            else
                box.space.s:delete{0}
            end
        end)
    end, {op})

    stream1:begin()
    stream2:begin()
    stream3:begin()

    stream1.space.s:replace{1, {0, 3, 0}}
    stream2.space.s:replace{10, {10}}
    stream2.space.s.index.mk:get{0}
    stream3.space.s:replace{0, {0, 1, 2, 2, 0, 1}}

    cg.server:exec(function()
        box.error.injection.set('ERRINJ_WAL_IO_COUNTDOWN', 0)
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
        t.helpers.retrying({}, function()
            t.assert(box.error.injection.get('ERRINJ_WAL_IO'))
        end)
        box.error.injection.set('ERRINJ_WAL_IO', false)
    end)

    local err_msg = 'Transaction has been aborted by conflict'
    t.assert_error_msg_content_equals(err_msg, function()
        stream1:commit()
    end)
    t.assert_error_msg_content_equals(err_msg, function()
        stream2:commit()
    end)

    t.assert_equals(stream4.space.s.index.mk:select{},
                    {{0, {0, 1, 0}}, {0, {0, 1, 0}}, {10, {10}}})

    stream3:commit()

    t.assert_equals(stream4.space.s.index.mk:select{},
                    {{0, {0, 1, 2, 2, 0, 1}}, {0, {0, 1, 2, 2, 0, 1}},
                     {0, {0, 1, 2, 2, 0, 1}}, {10, {10}}})
end

local function cleanup_injections(cg)
    t.tarantool.skip_if_not_debug()
    cg.server:exec(function(wal_queue_max_size)
        box.cfg{wal_queue_max_size = wal_queue_max_size}
        box.error.injection.set("ERRINJ_WAL_IO", false)
        box.error.injection.set("ERRINJ_WAL_DELAY", false)
    end, {cg.wal_queue_max_size})
end

-- Check duplicate and gaps handling on rollback of prepared replace works
-- correctly.
g.test_handle_dups_and_gaps_on_rollback_of_prepared_replace = function(cg)
    test_handle_dups_and_gaps_on_rollback_of_prepared_op(cg, 'replace')
end

g.after_test('test_handle_dups_and_gaps_on_rollback_of_prepared_replace',
             cleanup_injections)

-- Check duplicate and gap handling on rollback of prepared delete works
-- correctly.
g.test_handle_dups_and_gaps_on_rollback_of_prepared_delete = function(cg)
    test_handle_dups_and_gaps_on_rollback_of_prepared_op(cg, 'delete')
end

g.after_test('test_handle_dups_and_gaps_on_rollback_of_prepared_delete',
             cleanup_injections)

g.before_test('test_nullable', function(cg)
    cg.server:exec(function(index_opts)
        index_opts.parts[1].is_nullable = true
        index_opts.parts[1].exclude_null = true
        box.space.s.index.mk:drop()
        box.space.s:create_index('mk', index_opts)
    end, {cg.params.index_opts})
end)

-- Check that excluded keys are handled correctly.
g.test_nullable = function(cg)
    local stream1 = cg.server.net_box:new_stream()
    local stream2 = cg.server.net_box:new_stream()

    cg.server:exec(function()
        box.space.s:replace{0, {0, box.NULL, 0}}
        box.space.s:replace{1, {box.NULL, box.NULL}}
    end)

    stream1:begin()

    -- Check that count with excluded tuples is handled correctly.
    t.assert_equals(stream1.space.s.index.mk:count{}, 1)
    -- We deleted a tuple for which all values are excluded from the index, so
    -- this does not conflict stream 1.
    stream2.space.s:delete{1}

    -- Check that stream 1 did not get conflicted.
    stream1.space.s:replace{1, {box.NULL, box.NULL}}

    -- Check that DDL with excluded tuples is handled correctly.
    cg.server:exec(function()
        box.space.s:create_index('sk')
    end)
    local err_msg = 'Transaction has been aborted by conflict'
    t.assert_error_msg_content_equals(err_msg, function()
        stream1:commit()
    end)
    t.assert_equals(stream2.space.s.index.mk:select{}, {{0, {0, box.NULL, 0}}})

    stream1:begin()
    stream1.space.s:replace{0, {box.NULL, 0, box.NULL}}

    -- Check that snapshot creation with excluded tuples is handled correctly.
    cg.server:exec(function()
        box.snapshot()
    end)

    stream1:rollback()

    cg.server:restart({box_cfg = {memtx_use_mvcc_engine = true}})

    cg.server:exec(function()
        t.assert_equals(box.space.s.index.mk:select{},
                        {{0, {0, box.NULL, 0}}})
    end)
end

-- With MVCC off, a snapshot cleaner has records of the yielding DDL
-- statements, which have no key, unlike the records of a functional index.
local g_mvcc_off = t.group('gh-6385-mvcc-off')

g_mvcc_off.before_all(function(cg)
    -- The index build is parked by an error injection.
    t.tarantool.skip_if_not_debug()
    cg.server = server:new{
        box_cfg = {memtx_use_mvcc_engine = false}
    }
    cg.server:start()
end)

g_mvcc_off.after_all(function(cg)
    cg.server:drop()
end)

-- Check that a read view of a functional index with a record of a yielding
-- DDL statement hides the record and is closed correctly.
g_mvcc_off.test_read_view_with_alter_stmt = function(cg)
    cg.server:exec(function()
        local fiber = require('fiber')
        box.schema.func.create('f', {
            body = 'function(tuple) return {tuple[1]} end',
            is_deterministic = true,
            is_sandboxed = true,
        })
        box.space._index:create_index('f', {parts = {{1, 'unsigned'}},
                                            unique = false, func = 'f'})
        local s = box.schema.create_space('s')
        s:create_index('pk')
        s:insert{1}
        box.error.injection.set('ERRINJ_BUILD_INDEX_DELAY', true)
        local f = fiber.new(function()
            s:create_index('sk', {parts = {{1, 'unsigned'}}, unique = false})
        end)
        f:set_joinable(true)
        -- Let the build of the index start and yield.
        fiber.yield()
        local rv = box.read_view.open()
        -- The record of the index being built is not visible.
        t.assert_equals(#rv.space._index.index.primary:select{s.id}, 1)
        t.assert_equals(#rv.space._index.index.f:select{s.id}, 1)
        rv:close()
        box.error.injection.set('ERRINJ_BUILD_INDEX_DELAY', false)
        f:join()
    end)
end
