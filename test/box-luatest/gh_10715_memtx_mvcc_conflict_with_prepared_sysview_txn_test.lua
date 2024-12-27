local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function()
    g.server = server:new{
        box_cfg = {memtx_use_mvcc_engine = true}
    }
    g.server:start()
end)

g.after_all(function()
    g.server:drop()
end)

g.before_each(function()
    g.server:exec(function()
        -- Create vinyl space to test non-memtx transactions
        local s = box.schema.space.create('s', {engine = 'vinyl'})
        s:create_index('pk')
    end)
end)

g.after_each(function()
    g.server:exec(function()
        box.space.s:drop()
    end)
end)

g.test_conflict_with_prepared_sysview_transaction = function()
    g.server:exec(function()
        local fiber = require('fiber')

        local ch = fiber.channel(1)
        local f = fiber.create(function()
            box.begin()
            -- Read from sysview over _space to conflict this
            -- non-memtx transaction with future DDL.
            box.space._vspace:select{}
            box.space.s:replace{1}
            ch:put(true)
            box.commit()
        end)
        f:set_joinable(true)

        -- The crash happened because we conflicted a prepared non-memtx
        -- transaction. Here we check that we don't conflict such
        -- transactions anymore.
        t.assert_equals({ch:get()}, {true})
        box.space.s:format({{'f1', 'unsigned'}})
        t.assert_equals({f:join()}, {true})
    end)
end
