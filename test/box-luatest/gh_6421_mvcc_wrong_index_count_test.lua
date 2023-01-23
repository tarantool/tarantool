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

g.test_mvcc_wrong_space_count_simple = function()
    g.server:exec(function()
        local fiber = require('fiber')
        local s = box.space.test

        local f = fiber.create(function()
            fiber.self():set_joinable(true)
            s:insert{1}
        end)

        -- Unconfirmed TX is not visible.
        t.assert_equals(s:select(), {})
        t.assert_equals(s:count(), 0)

        f:join()

        -- Confirmed TX is visible.
        t.assert_equals(s:select(), {{1}})
        t.assert_equals(s:count(), 1)
    end)
end

g.test_mvcc_wrong_space_count_rw_tx = function()
    g.server:exec(function()
        local fiber = require('fiber')
        local s = box.space.test

        local f1 = fiber.create(function()
            fiber.self():set_joinable(true)
            s:insert{1}
        end)

        local fiber_select = nil
        local fiber_count = nil
        local f2 = fiber.create(function()
            fiber.self():set_joinable(true)
            box.begin()
            s:insert{2}
            fiber_select = s:select()
            fiber_count = s:count()
            box.commit()
        end)

        -- RW transaction must see unconfirmed statements
        t.assert_equals(fiber_select, {{1}, {2}})
        t.assert_equals(fiber_count, 2)

        f1:join()
        f2:join()
    end)
end
