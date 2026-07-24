local server = require('luatest.server')
local t = require('luatest')

local g = t.group('gh-6835', {
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

    stream4.space.s:replace{10, {10}}
    stream4:commit()

    t.assert_equals(stream4.space.s.index.mk:select{},
                    {{0, {1, 2, 1}}, {0, {1, 2, 1}}, {10, {10}}})

    t.assert_error_msg_equals('Transaction has been aborted by conflict',
                              function() stream3.space.s:replace{0, {0}} end)
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
    end)

    stream1:begin()

    t.assert_equals(stream1.space.s.index.mk:count{}, 2)
    if op == 'replace' then
        stream2.space.s:replace{0, {1, 2, 1}}
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
