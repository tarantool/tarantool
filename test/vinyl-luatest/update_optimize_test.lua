local t = require('luatest')

local server = require('test.luatest_helpers.server')
local common = require('test.vinyl-luatest.common')

local g = t.group()

g.before_all(function()
    g.server = server:new(
        {alias = 'master', box_cfg = common.default_box_cfg()}
    )
    g.server:start()
    g.server:exec(function()
        rawset(_G, 'dump_stmt_count', function(indexes)
            local dumped_count = 0
            for _, i in ipairs(indexes) do
                dumped_count = dumped_count +
                    box.space.test.index[i]:stat().disk.dump.output.rows
            end
            return dumped_count
        end)
    end)
end)

g.before_each(function()
    g.server:exec(function()
        box.schema.space.create('test', {engine = 'vinyl'})
    end)
end)

g.after_each(function()
    g.server:exec(function() box.space.test:drop() end)
end)

g.test_optimize_one_index = function()
    g.server:exec(function()
        local t = require('luatest')

        box.space.test:create_index('primary', {run_count_per_level = 20})
        box.space.test:create_index('secondary',
            {parts = {5, 'unsigned'}, run_count_per_level = 20})

        box.snapshot()

        local old_stmt_count = _G.dump_stmt_count({'primary', 'secondary'})

        box.space.test:insert({1, 2, 3, 4, 5})
        box.space.test:insert({2, 3, 4, 5, 6})
        box.space.test:insert({3, 4, 5, 6, 7})
        box.space.test:insert({4, 5, 6, 7, 8})
        box.snapshot()

        local new_stmt_count = _G.dump_stmt_count({'primary', 'secondary'})
        t.assert_equals(new_stmt_count - old_stmt_count, 8)

        -- Not optimized updates.

        -- Change secondary index field.
        t.assert_equals(
            box.space.test:update(1, {{'=', 5, 10}}), {1, 2, 3, 4, 10}
        )
        -- Need a snapshot after each operation to avoid purging some statements
        -- in vy_write_iterator during dump.
        box.snapshot()

        -- Move range containing index field.
        t.assert_equals(
            box.space.test:update(1, {{'!', 4, 20}}), {1, 2, 3, 20, 4, 10}
        )
        box.snapshot()

        -- Move range containing index field.
        t.assert_equals(
            box.space.test:update(1, {{'#', 3, 1}}), {1, 2, 20, 4, 10}
        )
        box.snapshot()

        old_stmt_count = new_stmt_count
        new_stmt_count = _G.dump_stmt_count({'primary', 'secondary'})
        t.assert_equals(new_stmt_count - old_stmt_count, 9)

        t.assert_equals(
            box.space.test.index.primary:select {},
            {
                {1, 2, 20, 4, 10},
                {2, 3, 4, 5, 6},
                {3, 4, 5, 6, 7},
                {4, 5, 6, 7, 8}
            }
        )
        t.assert_equals(
            box.space.test.index.secondary:select {},
            {
                {2, 3, 4, 5, 6},
                {3, 4, 5, 6, 7},
                {4, 5, 6, 7, 8},
                {1, 2, 20, 4, 10}
            }
        )

        -- Optimized updates.

        -- Change not indexed field.
        t.assert_equals(
            box.space.test:update(2, {{'=', 6, 10}}), {2, 3, 4, 5, 6, 10}
        )
        box.snapshot()

        -- Move range that doesn't contain indexed fields.
        t.assert_equals(
            box.space.test:update(2, {{'!', 7, 20}}), {2, 3, 4, 5, 6, 10, 20}
        )
        box.snapshot()

        t.assert_equals(
            box.space.test:update(2, {{'#', 6, 1}}), {2, 3, 4, 5, 6, 20}
        )
        box.snapshot()

        old_stmt_count = new_stmt_count
        new_stmt_count = _G.dump_stmt_count({'primary', 'secondary'})
        t.assert_equals(new_stmt_count - old_stmt_count, 3)

        t.assert_equals(
            box.space.test.index.primary:select {},
            {
                {1, 2, 20, 4, 10},
                {2, 3, 4, 5, 6, 20},
                {3, 4, 5, 6, 7},
                {4, 5, 6, 7, 8}
            }
        )
        t.assert_equals(
            box.space.test.index.secondary:select {},
            {
                {2, 3, 4, 5, 6, 20},
                {3, 4, 5, 6, 7},
                {4, 5, 6, 7, 8},
                {1, 2, 20, 4, 10}
            }
        )
    end)
end

g.test_optimize_two_indexes = function()
    g.server:exec(function()
        local t = require('luatest')

        box.space.test:create_index('primary',
            {parts = {2, 'unsigned'}, run_count_per_level = 20})
        box.space.test:create_index('secondary',
            {parts = {4, 'unsigned', 3, 'unsigned'}, run_count_per_level = 20})
        box.space.test:create_index('third',
            {parts = {5, 'unsigned'}, run_count_per_level = 20})

        box.snapshot()

        local old_stmt_count = _G.dump_stmt_count(
            {'primary', 'secondary', 'third'}
        )

        box.space.test:insert({1, 2, 3, 4, 5})
        box.space.test:insert({2, 3, 4, 5, 6})
        box.space.test:insert({3, 4, 5, 6, 7})
        box.space.test:insert({4, 5, 6, 7, 8})
        box.snapshot()

        local new_stmt_count = _G.dump_stmt_count(
            {'primary', 'secondary', 'third'}
        )
        t.assert_equals(new_stmt_count - old_stmt_count, 12)

        -- Not optimized updates.

        -- Change all fields.
        t.assert_equals(
            box.space.test:update(
                2, {{'+', 1, 10}, {'+', 3, 10}, {'+', 4, 10}, {'+', 5, 10}}
            ),
            {11, 2, 13, 14, 15}
        )
        box.snapshot()

        -- Move range containing all indexes.
        t.assert_equals(
            box.space.test:update(2, {{'!', 3, 20}}), {11, 2, 20, 13, 14, 15}
        )
        box.snapshot()

        -- Change two cols but then move range with all indexed fields.
        t.assert_equals(
            box.space.test:update(
                2, {{'=', 7, 100}, {'+', 5, 10}, {'#', 3, 1}}
            ),
            {11, 2, 13, 24, 15, 100}
        )
        box.snapshot()

        old_stmt_count = new_stmt_count
        new_stmt_count = _G.dump_stmt_count({'primary', 'secondary', 'third'})
        t.assert_equals(new_stmt_count - old_stmt_count, 15)

        t.assert_equals(
            box.space.test.index.primary:select {},
            {
                {11, 2, 13, 24, 15, 100},
                {2, 3, 4, 5, 6},
                {3, 4, 5, 6, 7},
                {4, 5, 6, 7, 8}
            }
        )
        t.assert_equals(
            box.space.test.index.secondary:select {},
            {
                {2, 3, 4, 5, 6},
                {3, 4, 5, 6, 7},
                {4, 5, 6, 7, 8},
                {11, 2, 13, 24, 15, 100}
            }
        )
        t.assert_equals(
            box.space.test.index.third:select {},
            {
                {2, 3, 4, 5, 6},
                {3, 4, 5, 6, 7},
                {4, 5, 6, 7, 8},
                {11, 2, 13, 24, 15, 100}
            }
        )

        -- Optimize one 'secondary' index update.

        -- Change only index 'third'.
        t.assert_equals(
            box.space.test:update(
                3, {{'+', 1, 10}, {'-', 5, 2}, {'!', 6, 100}}
            ),
            {12, 3, 4, 5, 4, 100}
        )
        box.snapshot()

        old_stmt_count = new_stmt_count
        new_stmt_count = _G.dump_stmt_count({'primary', 'secondary', 'third'})
        t.assert_equals(new_stmt_count - old_stmt_count, 3)

        -- Optimize one 'third' index update.

        -- Change only index 'secondary'.
        t.assert_equals(
            box.space.test:update(
                3, {{'=', 1, 20}, {'+', 3, 5}, {'=', 4, 30}, {'!', 6, 110}}
            ),
            {20, 3, 9, 30, 4, 110, 100}
        )
        box.snapshot()

        old_stmt_count = new_stmt_count
        new_stmt_count = _G.dump_stmt_count({'primary', 'secondary', 'third'})
        t.assert_equals(new_stmt_count - old_stmt_count, 3)

        -- Optimize both indexes.

        -- Not change any indexed fields.
        t.assert_equals(
            box.space.test:update(3, {{'+', 1, 10}, {'#', 6, 1}}),
            {30, 3, 9, 30, 4, 100}
        )
        box.snapshot()

        old_stmt_count = new_stmt_count
        new_stmt_count = _G.dump_stmt_count({'primary', 'secondary', 'third'})
        t.assert_equals(new_stmt_count - old_stmt_count, 1)

        t.assert_equals(
            box.space.test.index.primary:select {},
            {
                {11, 2, 13, 24, 15, 100},
                {30, 3, 9, 30, 4, 100},
                {3, 4, 5, 6, 7},
                {4, 5, 6, 7, 8}
            }
        )
        t.assert_equals(
            box.space.test.index.secondary:select {},
            {
                {3, 4, 5, 6, 7},
                {4, 5, 6, 7, 8},
                {11, 2, 13, 24, 15, 100},
                {30, 3, 9, 30, 4, 100}
            }
        )
        t.assert_equals(
            box.space.test.index.third:select {},
            {
                {30, 3, 9, 30, 4, 100},
                {3, 4, 5, 6, 7},
                {4, 5, 6, 7, 8},
                {11, 2, 13, 24, 15, 100}
            }
        )
    end)
end

-- gh-1716: optimize UPDATE with field num > 64.
g.test_optimize_UPDATE_with_field_num_more_than_64 = function()
    g.server:exec(function()
        local t = require('luatest')

        box.space.test:create_index('primary',
            {parts = {2, 'unsigned'}, run_count_per_level = 20})
        box.space.test:create_index('secondary',
            {parts = {4, 'unsigned', 3, 'unsigned'}, run_count_per_level = 20})
        box.space.test:create_index('third',
            {parts = {5, 'unsigned'}, run_count_per_level = 20})

        -- Create a big tuple.
        local long_tuple = {}
        for i = 1, 70 do long_tuple[i] = i end

        box.space.test:replace(long_tuple)
        box.snapshot()

        -- Make update of not indexed field with pos > 64.
        local old_stmt_count = _G.dump_stmt_count(
            {'primary', 'secondary', 'third'})
        long_tuple[65] = 1000
        t.assert_equals(box.space.test:update(2, {{'=', 65, 1000}}), long_tuple)
        box.snapshot()

        -- Check only primary index to be changed.
        local new_stmt_count = _G.dump_stmt_count(
            {'primary', 'secondary', 'third'})
        t.assert_equals(new_stmt_count - old_stmt_count, 1)
        t.assert_equals(box.space.test:get {2}[65], 1000)

        -- Try to optimize update with negative field numbers.

        t.assert_equals(
            box.space.test:update(2, {{'#', -65, 65}}), {1, 2, 3, 4, 5}
        )
        box.snapshot()

        old_stmt_count = new_stmt_count
        new_stmt_count = _G.dump_stmt_count({'primary', 'secondary', 'third'})
        t.assert_equals(new_stmt_count - old_stmt_count, 1)

        t.assert_equals(
            box.space.test.index.primary:select {}, {{1, 2, 3, 4, 5}}
        )
        t.assert_equals(
            box.space.test.index.secondary:select {}, {{1, 2, 3, 4, 5}}
        )
        t.assert_equals(box.space.test.index.third:select {}, {{1, 2, 3, 4, 5}})

        box.space.test:replace({10, 20, 30, 40, 50})
        box.snapshot()

        old_stmt_count = _G.dump_stmt_count({'primary', 'secondary', 'third'})

        t.assert_equals(
            box.space.test:update(20, {{'=', -1, 500}}), {10, 20, 30, 40, 500}
        )
        box.snapshot()

        new_stmt_count = _G.dump_stmt_count({'primary', 'secondary', 'third'})
        -- 3 = REPLACE in 1 index and DELETE + REPLACE in 3 index.
        t.assert_equals(new_stmt_count - old_stmt_count, 3)

        t.assert_equals(
            box.space.test.index.primary:select {},
            {
                {1, 2, 3, 4, 5},
                {10, 20, 30, 40, 500}
            }
        )
        t.assert_equals(
            box.space.test.index.secondary:select {},
            {
                {1, 2, 3, 4, 5},
                {10, 20, 30, 40, 500}
            }
        )
        t.assert_equals(
            box.space.test.index.third:select {},
            {
                {1, 2, 3, 4, 5},
                {10, 20, 30, 40, 500}
            }
        )
    end)
end

g.test_optimize_update_does_not_skip_entire_key_during_dump = function()
    g.server:exec(function()
        local t = require('luatest')

        box.space.test:create_index('primary',
            {parts = {2, 'unsigned'}, run_count_per_level = 20})
        box.space.test:create_index('secondary',
            {parts = {4, 'unsigned', 3, 'unsigned'}, run_count_per_level = 20})
        box.space.test:create_index('third',
            {parts = {5, 'unsigned'}, run_count_per_level = 20})

        box.space.test:replace({10, 100, 1000, 10000, 100000, 1000000})
        t.assert_equals(
            box.space.test:update(100, {{'=', 6, 1}}),
            {10, 100, 1000, 10000, 100000, 1}
        )

        box.begin()

        box.space.test:replace({20, 200, 2000, 20000, 200000, 2000000})
        t.assert_equals(
            box.space.test:update(200, {{'=', 6, 2}}),
            {20, 200, 2000, 20000, 200000, 2}
        )

        box.commit()

        box.snapshot()

        t.assert_equals(
            box.space.test.index.primary:select {},
            {
                {10, 100, 1000, 10000, 100000, 1},
                {20, 200, 2000, 20000, 200000, 2}
            }
        )
        t.assert_equals(
            box.space.test.index.secondary:select {},
            {
                {10, 100, 1000, 10000, 100000, 1},
                {20, 200, 2000, 20000, 200000, 2}
            }
        )
        t.assert_equals(
            box.space.test.index.third:select {},
            {
                {10, 100, 1000, 10000, 100000, 1},
                {20, 200, 2000, 20000, 200000, 2}
            }
        )
    end)
end

-- gh-2980: key uniqueness is not checked if indexed fields are not updated.
g.test_key_uniqueness_not_checked_if_indexed_fields_not_updated = function()
    g.server:exec(function()
        local t = require('luatest')

        box.space.test:create_index('primary',
            {parts = {2, 'unsigned'}, run_count_per_level = 20})
        box.space.test:create_index('secondary',
            {parts = {4, 'unsigned', 3, 'unsigned'}, run_count_per_level = 20})
        box.space.test:create_index('third',
            {parts = {5, 'unsigned'}, run_count_per_level = 20})

        box.space.test:replace({1, 1, 1, 1, 1})

        local function get_lookups(lb)
            local ret = {}
            for i = 1, #lb do
                local info = box.space.test.index[i - 1]:stat()
                table.insert(ret, info.lookup - lb[i])
            end
            return ret
        end

        local lookups = get_lookups({0, 0, 0})

        -- Update field that is not indexed.
        t.assert_equals(
            box.space.test:update(1, {{'+', 1, 1}}), {2, 1, 1, 1, 1}
        )
        t.assert_equals(get_lookups(lookups), {1, 0, 0})

        -- Update field indexed by space.index[1].
        t.assert_equals(
            box.space.test:update(1, {{'+', 3, 1}}), {2, 1, 2, 1, 1}
        )
        t.assert_equals(get_lookups(lookups), {2, 1, 0})

        -- Update field indexed by space.index[2].
        t.assert_equals(
            box.space.test:update(1, {{'+', 5, 1}}), {2, 1, 2, 1, 2}
        )
        t.assert_equals(get_lookups(lookups), {3, 1, 1})
    end)
end

-- gh-3607: phantom tuples in secondary index if UPDATE does not change key
-- fields.
g.test_no_phantom_tuples_in_secondary_index = function()
    g.server:exec(function()
        local t = require('luatest')

        box.space.test:create_index('primary')
        box.space.test:create_index('secondary',
            {parts = {2, 'unsigned'}, run_count_per_level = 10})

        box.space.test:insert({1, 10})
        -- Some padding to prevent last-level compaction (gh-3657).
        for i = 1001, 1010 do box.space.test:replace {i, i} end
        box.snapshot()

        t.assert_equals(box.space.test:update(1, {{'=', 2, 10}}), {1, 10})
        box.space.test:delete(1)
        box.snapshot()

        -- Should be 12: INSERT{10, 1} and INSERT[1001..1010] in the first run
        -- plus DELETE{10, 1} in the second one.
        t.assert_equals(box.space.test.index.secondary:stat().rows, 12)

        box.space.test:insert({1, 20})
        t.assert_equals(
            box.space.test.index.secondary:select {},
            {
                {1, 20},
                {1001, 1001},
                {1002, 1002},
                {1003, 1003},
                {1004, 1004},
                {1005, 1005},
                {1006, 1006},
                {1007, 1007},
                {1008, 1008},
                {1009, 1009},
                {1010, 1010}
            }
        )
    end)
end
