local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function()
    g.server = server:new{
        alias   = 'default',
        box_cfg = {memtx_use_mvcc_engine = true}
    }
    g.server:start()
end)

g.after_all(function()
    g.server:drop()
end)

g.before_each(function()
    g.server:exec(function()
        local s = box.schema.space.create('test')
        s:create_index('pk', {parts = {1, 2, 3}})
    end)
end)

g.after_each(function()
    g.server:exec(function()
        box.space.test:drop()
    end)
end)

g.test_10581_insert_max_performance_degradation = function()
    g.server:exec(function()
        local SIZE_OF_NEARBY_GAP_TRACKER = 104
        local SIZE_OF_INPLACE_GAP_TRACKER = 64

        local fiber = require('fiber')
        fiber.set_max_slice(10)

        local N = 100000
        box.begin()
        for i = 1, N do
            box.space.test:insert({i, i, i})
            box.space.test.index.pk:max()
        end
        local expected_size = N *
            (SIZE_OF_INPLACE_GAP_TRACKER + SIZE_OF_NEARBY_GAP_TRACKER)
        t.assert_equals(box.stat.memtx.tx().mvcc.trackers.total, expected_size)
        box.commit()
    end)
end

g.test_gap_inplace_range_merge = function()
    g.server:exec(function()
        local SIZE_OF_NEARBY_GAP_TRACKER = 104

        local fiber = require('fiber')
        fiber.set_max_slice(10)

        -- TODO: add {box.NULL} key
        local cases = {
            function(key)
                box.space.test:select(key, {iterator = 'LE'})
                box.space.test:select(key, {iterator = 'LT'})
                box.space.test:select(key, {iterator = 'GE'})
                box.space.test:select(key, {iterator = 'GT'})
            end,
            function(key)
                box.space.test:select(key, {iterator = 'LT'})
                box.space.test:select(key, {iterator = 'LE'})
                box.space.test:select(key, {iterator = 'GT'})
                box.space.test:select(key, {iterator = 'GE'})
            end,
            function(key)
                box.space.test:select(nil, {iterator = 'LE'})
                box.space.test:select(key, {iterator = 'LE'})
                box.space.test:select(nil, {iterator = 'GE'})
                box.space.test:select(key, {iterator = 'GE'})
            end,
            function(key)
                box.space.test:select(key, {iterator = 'LE'})
                box.space.test:select(nil, {iterator = 'LE'})
                box.space.test:select(key, {iterator = 'GE'})
                box.space.test:select(nil, {iterator = 'GE'})
            end,
            function(key)
                box.space.test:select(key[1], {iterator = 'LE'})
                box.space.test:select(key, {iterator = 'LE'})
                box.space.test:select(key[1], {iterator = 'GE'})
                box.space.test:select(key, {iterator = 'GE'})
            end,
            function(key)
                box.space.test:select(key[1], {iterator = 'LE'})
                box.space.test:select(key, {iterator = 'LT'})
                box.space.test:select(key[1], {iterator = 'GE'})
                box.space.test:select(key, {iterator = 'GT'})
            end,
            function(key)
                box.space.test:select(nil, {iterator = 'LE'})
                box.space.test:select(key[1], {iterator = 'LE'})
                box.space.test:select(nil, {iterator = 'GE'})
                box.space.test:select(key[1], {iterator = 'GE'})
            end,
            function(key)
                box.space.test:select(key[1], {iterator = 'LE'})
                box.space.test:select(nil, {iterator = 'LE'})
                box.space.test:select(key[1], {iterator = 'GE'})
                box.space.test:select(nil, {iterator = 'GE'})
            end,
        }

        local N = 100
        for _, case in ipairs(cases) do
            box.begin()
            for i = 1, N do
                for _ = 1, 2 do
                    case({i, i, i})
                    t.assert_equals(box.stat.memtx.tx().mvcc.trackers.total,
                                    2 * SIZE_OF_NEARBY_GAP_TRACKER)
                end
            end
            box.commit()
        end
    end)
end

g.test_gap_inplace_eq = function()
    g.server:exec(function()
        local SIZE_OF_NEARBY_GAP_TRACKER = 104

        local fiber = require('fiber')
        fiber.set_max_slice(10)

        -- TODO: add {box.NULL} key
        local cases = {
            function(key)
                box.space.test:select({key, key}, {iterator = 'EQ'})
                box.space.test:select({key, key}, {iterator = 'REQ'})
                box.space.test:select({key, key}, {iterator = 'GE'})
            end,
            function(key)
                box.space.test:select({key, key}, {iterator = 'GE'})
                box.space.test:select({key, key}, {iterator = 'REQ'})
                box.space.test:select({key, key}, {iterator = 'EQ'})
            end,
            function(key)
                box.space.test:select({key, key}, {iterator = 'EQ'})
                box.space.test:select({key}, {iterator = 'EQ'})
            end,
            function(key)
                box.space.test:select({key}, {iterator = 'EQ'})
                box.space.test:select({key, key}, {iterator = 'EQ'})
            end,
            function(key)
                box.space.test:select(nil, {iterator = 'EQ'})
                box.space.test:select({key}, {iterator = 'EQ'})
            end,
        }

        local N = 100
        for case_no, case in ipairs(cases) do
            for i = 1, N do
                box.begin()
                for _ = 1, 2 do
                    case(i)
                    t.assert_equals(box.stat.memtx.tx().mvcc.trackers.total,
                                    2 * SIZE_OF_NEARBY_GAP_TRACKER, case_no)
                end
                box.commit()
            end
        end

        N = 10000
        box.begin()
        for i = 1, N do
            for _ = 1, 2 do
                box.space.test:select({i}, {iterator = 'EQ'})
                box.space.test:select({i, i}, {iterator = 'EQ'})
                t.assert_equals(box.stat.memtx.tx().mvcc.trackers.total,
                                2 * i * SIZE_OF_NEARBY_GAP_TRACKER)
            end
        end
        box.commit()
    end)
end

-- TODO: count
