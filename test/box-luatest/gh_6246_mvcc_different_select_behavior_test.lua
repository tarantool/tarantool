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
        s:create_index('primary')
    end)
end)

g.after_each(function()
    g.server:exec(function() box.space.test:drop() end)
end)

-- Select in RO transaction is similar to autocommit.
g.test_mvcc_different_select_behavior_simple = function()
    g.server:exec(function()
        local t = require('luatest')
        local fiber = require('fiber')
        local s = box.space.test

        local f = fiber.create(function()
            fiber.self():set_joinable(true)
            s:insert{1}
        end)

        t.assert_equals(s:select(), {})
        t.assert_equals(s:select(1), {})
        t.assert_equals(s:count(), 0)
        box.begin()
        local res1 = s:select()
        local res2 = s:select(1)
        local res3 = s:count()
        box.commit()
        t.assert_equals(res1, {})
        t.assert_equals(res2, {})
        t.assert_equals(res3, 0)

        f:join()

        t.assert_equals(s:select(), {{1}})
        t.assert_equals(s:select(1), {{1}})
        t.assert_equals(s:count(), 1)
        box.begin()
        local res1 = s:select()
        local res2 = s:select(1)
        local res3 = s:count()
        box.commit()
        t.assert_equals(res1, {{1}})
        t.assert_equals(res2, {{1}})
        t.assert_equals(res3, 1)
    end)
end

-- for RW transactions prepared statements are visible.
g.test_mvcc_different_select_behavior_rw = function()
    g.server:exec(function()
        local t = require('luatest')
        local fiber = require('fiber')
        local s = box.space.test

        local f = fiber.create(function()
            fiber.self():set_joinable(true)
            s:insert{1}
        end)

        box.begin()
        s:replace{2}
        local res1 = s:select()
        local res2 = s:select(1)
        local res3 = s:count()
        box.commit()
        t.assert_equals(res1, {{1}, {2}})
        t.assert_equals(res2, {{1}})
        t.assert_equals(res3, 2)

        f:join()

        t.assert_equals(s:select(), {{1}, {2}})
        t.assert_equals(s:select(1), {{1}})
        t.assert_equals(s:count(), 2)
    end)
end

-- If RO transaction becomes RW - it can be aborted.
g.test_mvcc_different_select_behavior_ro_rw = function()
    g.server:exec(function()
        local t = require('luatest')
        local fiber = require('fiber')
        local s = box.space.test

        local f = fiber.create(function()
            fiber.self():set_joinable(true)
            s:insert{1}
        end)

        box.begin()
        local res1 = s:select()
        local res2 = s:select(1)
        local res3 = s:count()
        t.assert_error_msg_content_equals(
            "Transaction has been aborted by conflict",
            function() s:replace{2} end)
        t.assert_error_msg_content_equals(
            "Transaction has been aborted by conflict",
            function() box.commit() end)
        t.assert_equals(res1, {})
        t.assert_equals(res2, {})
        t.assert_equals(res3, 0)

        f:join()

        t.assert_equals(s:select(), {{1}})
        t.assert_equals(s:select(1), {{1}})
        t.assert_equals(s:count(), 1)
    end)
end

        -- Similar conflict but with skipped delete.
g.test_mvcc_different_select_behavior_ro_rw_delete = function()
    g.server:exec(function()
        local t = require('luatest')
        local fiber = require('fiber')
        local s = box.space.test

        s:replace{1}

        local f = fiber.create(function()
            fiber.self():set_joinable(true)
            s:delete{1}
        end)

        box.begin()
        local res1 = s:select()
        local res2 = s:select(1)
        local res3 = s:count()
        t.assert_error_msg_content_equals(
            "Transaction has been aborted by conflict",
            function() s:replace{2} end)
        t.assert_error_msg_content_equals(
            "Transaction has been aborted by conflict",
            function() box.commit() end)

        t.assert_equals(res1, {{1}})
        t.assert_equals(res2, {{1}})
        t.assert_equals(res3, 1)

        f:join()

        t.assert_equals(s:select(), {})
        t.assert_equals(s:select(1), {})
        t.assert_equals(s:count(), 0)
    end)
end

-- Special check that order of read views is correct.
g.test_mvcc_different_select_behavior_reordering = function()
    g.server:exec(function()
        local t = require('luatest')
        local fiber = require('fiber')
        local s = box.space.test
        s:replace{0, 0}

        local fibers = {}
        local num_fibers = 10
        local half = math.modf(num_fibers / 2)
        local num_finished = 0
        for i = 1, num_fibers do
            local f = fiber.create(function()
                fiber.self():set_joinable(true)
                box.begin()
                s:replace{i}
                s:replace{0, i}
                box.commit()
                num_finished = num_finished + 1
            end)
            table.insert(fibers, f)
        end

        local num1,res1,num2,res2,res3
        local cond = fiber.cond()
        local reader = fiber.create(function()
            fiber.self():set_joinable(true)
            box.begin()
            num1 = num_finished
            res1 = s:select{half}
            cond:wait()
            num2 = num_finished
            res2 = s:select{half}
            res3 = s:select{0}
            box.commit()
        end)

        box.begin()
        t.assert_equals(s:select{num_fibers}, {}) -- highest level
        t.assert_equals(s:select{1}, {}) -- lowest level

        t.assert_equals(num_finished, 0)
        for _,f in pairs(fibers) do
           f:join()
        end
        t.assert_equals(num_finished, num_fibers)

        -- Create transaction noise to force TX GC to do the job.
        for i = num_fibers + 1, num_fibers * 3 do
            fiber.create(function()
                fiber.self():set_joinable(true)
                s:replace{i}
            end):join()
        end

        cond:signal()
        reader:join()

        t.assert_equals(s:select{num_fibers}, {}) -- highest level
        t.assert_equals(s:select{1}, {}) -- lowest level
        t.assert_equals(s:select{0}, {{0, 0}}) -- lowest level
        box.commit()

        -- Before RW transaction completed...
        t.assert_equals(num1, 0)
        -- ... we cannot see their changes.
        t.assert_equals(res1, {})
        -- After RW transaction completed...
        t.assert_equals(num2, num_fibers)
        -- ... we still cannot see their changes (repeatable read).
        t.assert_equals(res2, {})
        -- And now our read view show changes right tx number 'half'.
        t.assert_equals(res3, {{0, half - 1}})
    end)
end

-- Another check that order of read views is correct.
g.test_mvcc_different_select_behavior_another_reordering = function()
    g.server:exec(function()
        local t = require('luatest')
        local fiber = require('fiber')
        local s = box.space.test

        local fibers = {}
        local num_fibers = 10
        local num_finished = 0

        s:replace{1, 0}
        s:replace{num_fibers, 0}

        local res11,num11,res12,num12
        local cond1 = fiber.cond()
        local reader1 = fiber.create(function()
            fiber.self():set_joinable(true)
            box.begin()
            res11 = s:select{num_fibers}
            num11 = num_finished
            cond1:wait()
            res12 = s:select{num_fibers}
            num12 = num_finished
            box.commit()
        end)

        for i = 1, num_fibers do
            local f = fiber.create(function()
                fiber.self():set_joinable(true)
                box.begin()
                s:replace{i, 1} -- reader1 was sent to read view.
                box.commit()
                num_finished = num_finished + 1
            end)
            table.insert(fibers, f)
        end

        local res21,num21,res22,num22
        local cond2 = fiber.cond()
        local reader2 = fiber.create(function()
            fiber.self():set_joinable(true)
            box.begin()
            res21 = s:select{1} -- go to read view since we cannot see prepared.
            num21 = num_finished
            cond2:wait()
            res22 = s:select{1}
            num22 = num_finished
            box.commit()
        end)

        for _,f in pairs(fibers) do
           f:join()
        end
        t.assert_equals(num_finished, num_fibers)

        -- Create transaction noise to force TX GC to do the job.
        for i = num_fibers + 1, num_fibers * 3 do
            fiber.create(function()
                fiber.self():set_joinable(true)
                s:replace{i}
            end):join()
        end

        -- Finish TX in the first fiber.
        cond1:signal()
        reader1:join()

        -- Create transaction noise to force TX GC to do the job.
        for i = num_fibers + 1, num_fibers * 3 do
            fiber.create(function()
                fiber.self():set_joinable(true)
                s:replace{i}
            end):join()
        end

        -- Finish TX in the second fiber.
        cond2:signal()
        reader2:join()

        t.assert_equals(res11, {{num_fibers, 0}})
        t.assert_equals(num11, 0)
        t.assert_equals(res12, {{num_fibers, 0}})
        t.assert_equals(num12, num_finished)
        t.assert_equals(res21, {{1, 0}})
        t.assert_equals(num21, 0)
        t.assert_equals(res22, {{1, 0}})
        t.assert_equals(num22, num_finished)

    end)
end
