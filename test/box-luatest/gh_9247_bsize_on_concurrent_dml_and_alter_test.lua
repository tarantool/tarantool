local server = require('luatest.server')
local t = require('luatest')
local g = t.group('gh-9247')

g.before_all(function(cg)
    cg.server = server:new()
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.after_each(function(cg)
    cg.server:exec(function()
        box.space.test:drop()
    end)
end)

-- Based on `test/box/alter-primary-index-tuple-leak-long.test.lua'.
local function concurrent_insert_and_alter_helper(cg, rollback)
    cg.server:exec(function(rollback)
        local fiber = require('fiber')
        local s = box.schema.space.create('test')
        local idx = s:create_index('pk')
        local alter_started = false
        local data = string.rep('a', 66)

        -- Create a table with enough tuples to make primary index alter in
        -- background.
        for i = 0, 7000, 100 do
            box.begin()
            for j = i, i + 99 do
                s:insert{j, j, data}
            end
            box.commit()
        end

        -- Wait for fiber yield during altering and insert some tuples.
        local fib_insert = fiber.new(function()
            while not alter_started do
                fiber.sleep(0)
            end
            box.begin()
            for i = 8000, 9000 do
                s:insert{i, i, data}
            end
            box.commit()
        end)

        -- Alter primary index.
        -- If the index will not be altered in background, the test will fail
        -- after timeout (fib_insert:join() will wait forever).
        local fib_alter = fiber.new(function()
            alter_started = true
            box.begin()
            idx:alter{parts = {{field = 2}}}
            if rollback then
                box.rollback()
            else
                box.commit()
            end
            alter_started = false
        end)

        fib_insert:set_joinable(true)
        fib_alter:set_joinable(true)
        fib_insert:join()
        fib_alter:join()

        local bsize = 0
        for _, tuple in s:pairs() do
            bsize = bsize + tuple:bsize()
        end
        t.assert_equals(s:bsize(), bsize)
    end, {rollback})
end

-- Check that space:bsize() is correct for a built-in-background primary index
-- with concurrent inserts.
g.test_concurrent_insert_and_alter_commit = function(cg)
    concurrent_insert_and_alter_helper(cg, false)
end

-- Check that space:bsize() is correct for a built-in-background primary index
-- with concurrent inserts.
g.test_concurrent_insert_and_alter_rollback = function(cg)
    concurrent_insert_and_alter_helper(cg, true)
end

local function truncate_helper(cg, rollback)
    cg.server:exec(function(rollback)
        local s = box.schema.space.create('test')
        s:create_index('pk')
        s:insert{1, 2, 3}
        local bsize_before_truncate = s:bsize()

        box.begin()
        s:truncate()
        if rollback then
            box.rollback()
            t.assert_equals(s:bsize(), bsize_before_truncate)
        else
            box.commit()
            t.assert_equals(s:bsize(), 0)
        end
    end, {rollback})
end

-- Check that space:bsize() is correct after successful space:truncate().
g.test_truncate_commit = function(cg)
    truncate_helper(cg, false)
end

-- Check that space:bsize() is correct after rolled back space:truncate().
g.test_truncate_rollback = function(cg)
    truncate_helper(cg, true)
end
