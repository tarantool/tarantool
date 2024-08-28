local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_each(function(cg)
    cg.server = server:new{box_cfg = {memtx_use_mvcc_engine = true}}
    cg.server:start()
end)

g.after_each(function(cg)
    cg.server:stop()
end)

-- The test checks that all delete statements are handled correctly
-- on space drop
g.test_drop_space_many_delete_statements = function(cg)
    cg.server:exec(function()
        local txn_proxy = require("test.box.lua.txn_proxy")

        -- Create space with tuples
        local s = box.schema.space.create('test')
        s:create_index('pk')
        for i = 1, 100 do
            s:replace{i}
        end

        -- Delete the tuples concurrently
        local tx1 = txn_proxy:new()
        local tx2 = txn_proxy:new()
        tx1:begin()
        tx2:begin()
        for i = 1, 100 do
            local stmt = "box.space.test:delete{" .. i .. "}"
            tx1(stmt)
            tx2(stmt)
        end
        s:drop()
        tx1:rollback()
        tx2:rollback()

        -- Collect garbage
        box.internal.memtx_tx_gc(1000)
    end)
end

-- The test checks if background build of index does not crash when
-- MVCC is enabled
-- gh-10147
g.test_background_build = function(cg)
    cg.server:exec(function()
        local txn_proxy = require("test.box.lua.txn_proxy")
        local fiber = require('fiber')

        -- Create space with tuples
        local s = box.schema.space.create('test')
        s:create_index('pk')
        for i = 1, 2000 do
            s:replace{i}
        end

        local index_built = false
        local f = fiber.create(function()
            s:create_index('sk')
            index_built = true
        end)
        f:set_joinable(true)

        -- Delete the tuples concurrently
        local tx1 = txn_proxy:new()
        tx1:begin()
        for i = 1, 2000 do
            local stmt = "box.space.test:delete{" .. i .. "}"
            tx1(stmt)
        end

        assert(not index_built)
        local ok = f:join()
        t.assert(ok)
        local res = tx1:commit()
        -- Must be aborted by DDL
        t.assert_equals(res,
            {{error = "Transaction has been aborted by conflict"}})

        -- Collect garbage
        box.internal.memtx_tx_gc(1000)
    end)
end

-- The test covers a crash when transaction that is being deleted removes
-- itself from reader list of a deleted story that leads to use-after-free
g.test_reader_list_use_after_free = function(cg)
    cg.server:exec(function()
        local txn_proxy = require("test.box.lua.txn_proxy")

        -- Create space with tuples
        local s = box.schema.space.create('test')
        s:create_index('pk')

        box.begin()
        for i = 1, 10000 do
            s:replace{i}
        end
        box.commit()

        -- Create a transaction that reads every tuple so it's
        -- inserted to reader list of every story
        local tx = txn_proxy.new()
        tx:begin()
        for i = 1, 10000 do
            tx('box.space.test:get{' .. i .. '}')
        end

        -- Create a new index
        -- Firstly, we need it so that all the stories will be deleted
        -- due to DDL
        -- Secondly, we need to create a new index so that layout of stories
        -- will be changed and use-after-free on rlist link will trash another
        -- field (for example, pointer to tuple) and that's will definitely lead
        -- to crash
        box.space.test:create_index('sk')

        -- Open a read-view so that stories for all tuples from the space
        -- are created
        local rv = txn_proxy.new()
        rv:begin()
        rv('box.space.test:select{}')

        -- Rollback the first reader so that it will delete itself from reader
        -- lists of all stories and that will lead to use-after-free
        tx:rollback()

        -- Read all the tuples, Tarantool is most likely to crash here if
        -- use-after-free broke something
        for i = 1, 10000 do
            s:get{i}
        end
    end)
end
