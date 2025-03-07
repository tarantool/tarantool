local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function(cg)
    cg.server = server:new()
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.after_each(function(cg)
    cg.server:exec(function()
        if box.space.test ~= nil then
            box.space.test:drop()
        end
    end)
end)

g.test_index_quantile = function(cg)
    cg.server:exec(function()
        local margin = 200
        local key_count = 10000
        local page_size = 128
        local range_size = 32768

        local s = box.schema.space.create('test', {engine = 'vinyl'})
        local i1 = s:create_index('i1', {
            page_size = page_size,
            range_size = range_size,
        })
        local i2 = s:create_index('i2', {
            unique = false,
            parts = {{3, 'unsigned'}},
            page_size = page_size,
            range_size = range_size,
        })

        box.begin()
        for k = 1, key_count do
            s:insert({k, 'x', k})
            if k % 100 == 0 then
                box.commit()
                box.begin()
            end
        end
        box.commit()

        -- Check that a quantile isn't found if there are no disk layers.
        local function check(i)
            t.assert_equals(i:quantile(0.5), nil)
            t.assert_equals(i:quantile(0.5, {}, {100}), nil)
            t.assert_equals(i:quantile(0.5, {100}, {}), nil)
            t.assert_equals(i:quantile(0.5, {100}, {200}), nil)
        end
        check(i1)
        check(i2)

        box.snapshot()

        -- Check various corner cases.
        local function check(i)
            t.assert_equals(i:quantile(0.5, {}, {1}), nil)
            t.assert_equals(i:quantile(0.5, {2}, {3}), nil)
            t.assert_equals(i:quantile(0.5, {15000}, {}), nil)
            t.assert_equals(i:quantile(1e-10, {1000}, {2000}), nil)
        end
        check(i1)
        check(i2)

        -- Check quantile values.
        local function check_one(i, expected, level, begin_key, end_key)
            local actual = i:quantile(level, begin_key, end_key)
            t.assert_type(actual, 'table')
            t.assert(#actual, 1)
            actual = actual[1]
            t.assert_almost_equals(actual, expected, margin)
        end
        local function check(i)
            check_one(i, 1000, 0.1)
            check_one(i, 5000, 0.5)
            check_one(i, 9000, 0.9)
            check_one(i, 2500, 0.5, {}, {5000})
            check_one(i, 3500, 0.5, {}, {7000})
            check_one(i, 7500, 0.5, {5000}, {})
            check_one(i, 8500, 0.5, {7000}, {})
            check_one(i, 4500, 0.5, {2000}, {7000})
            check_one(i, 4500, 0.5, {3000}, {6000})
        end
        check(i1)
        check(i2)

        -- Overwrite the data a few times to force range splitting.
        for n = 1, 4 do
            box.begin()
            for k = 1, key_count, n * 10 do
                s:update({k}, {{'=', 2, 'y'}, {'+', 3, 1}})
            end
            box.commit()
            box.snapshot()
            i1:compact()
            i2:compact()
            t.helpers.retrying({}, function()
                t.assert_covers(box.stat.vinyl(), {
                    scheduler = {
                        tasks_inprogress = 0,
                        compaction_queue = 0,
                    },
                })
            end)
        end
        t.assert_covers(i1:stat(), {range_count = 8})
        t.assert_covers(i2:stat(), {range_count = 8})

        -- Check quantile values again.
        check(i1)
        check(i2)
    end)
end
