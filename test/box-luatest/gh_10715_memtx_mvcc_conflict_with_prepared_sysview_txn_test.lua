local server = require('luatest.server')
local t = require('luatest')

-- The original issue is about non-memtx transactions, but let's
-- test memtx along the way.
local g = t.group(nil, {{engine = 'memtx'}, {engine = 'vinyl'}})

g.before_all(function(g)
    g.server = server:new{
        box_cfg = {memtx_use_mvcc_engine = true}
    }
    g.server:start()
end)

g.after_all(function(g)
    g.server:drop()
end)

g.before_each(function(g)
    g.server:exec(function(engine)
        local s = box.schema.space.create('s', {engine = engine})
        s:create_index('pk')
    end, {g.params.engine})
end)

g.after_each(function(g)
    g.server:exec(function()
        box.space.s:drop()
    end)
end)

g.test_conflict_with_prepared_sysview_transaction = function(g)
    g.server:exec(function()
        local fiber = require('fiber')

        local ch = fiber.channel(1)
        local f = fiber.create(function()
            box.begin()
            -- Read from sysview over _space to conflict this
            -- transaction with future DDL.
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

-- Along the way, make sure that conflict of in-progress transaction
-- caused by sysview read works well.
g.test_conflict_with_in_progress_sysview_transaction = function(g)
    g.server:exec(function()
        local fiber = require('fiber')

        box.begin()
        -- Read from sysview over _space to conflict this
        -- transaction with future DDL.
        box.space._vspace:select{}
        box.space.s:replace{1}

        local f = fiber.create(function()
            box.space.s:format({{'f1', 'unsigned'}})
        end)
        f:set_joinable(true)
        t.assert_equals({f:join()}, {true})

        t.assert_error_msg_equals(
            'Transaction has been aborted by conflict', box.commit)
    end)
end
