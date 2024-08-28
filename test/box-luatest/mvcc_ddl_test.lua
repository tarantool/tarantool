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

-- The test checks if stories associated with old schema cannot
-- be access on index creation
-- gh-10096
g.test_dont_access_old_stories_on_index_create = function(cg)
    cg.server:exec(function()
        local fiber = require('fiber')

        local s = box.schema.space.create('test')
        s:create_index('pk', {parts = {{1}}})
        s:create_index('sk1', {parts = {{2}}, unique = true})

        s:insert{1, 1, 1, 0}
        -- Collect stories to be independent from GC steps
        box.internal.memtx_tx_gc(100)

        -- Create index in a separate transaction
        -- Transaction creating index will create story for our tuple
        -- when it reads it to insert into the new index
        -- The problem is the story is created with old index_count and
        -- does not have links for created index
        local created_index = false
        local f = fiber.create(function()
            s:create_index('sk2', {parts = {{3}}, unique = true})
            created_index = true
        end)
        f:set_joinable(true)

        -- Make sure txn is still in progress
        assert(not created_index)
        s:replace{1, 1, 1, 1}
        local ok = f:join()
        t.assert(ok)
    end)
end

-- The test checks if stories associated with old schema cannot
-- be access when index is being altered
-- gh-10097
g.test_dont_access_old_stories_on_index_alter = function(cg)
    cg.server:exec(function()
        local fiber = require('fiber')

        local s = box.schema.space.create('test')
        s:create_index('pk', {parts = {{1}}})
        s:create_index('sk1', {parts = {{2}}, unique = true})

        s:insert{1, 1, 1, 0}
        -- Collect stories to be independent from GC steps
        box.internal.memtx_tx_gc(100)

        -- Alter index in a separate transaction
        -- Transaction creating index will create story for our tuple
        -- when it reads it to insert into the new index
        -- The problem is the story link points to the old index, but
        -- it was replaced with the new one (new index object, hence,
        -- new address)
        local altered_index = false
        local f = fiber.create(function()
            s.index.sk1:alter({parts = {{3}}, unique = true})
            altered_index = true
        end)
        f:set_joinable(true)

        -- Make sure txn is still in progress
        assert(not altered_index)
        s:replace{1, 1, 1, 1}
        local ok = f:join()
        t.assert(ok)
    end)
end

-- The test covers a crash when using count after DDL because
-- DDL has violated some MVCC invariants that are checked in count
-- gh-10474, case 3
g.test_count_crashes_after_ddl = function(cg)
    cg.server:exec(function()
        local fiber = require('fiber')

        local s = box.schema.create_space('test')
        s:create_index('pk')
        s:create_index('sk', {parts={{2}}})

        -- Do a sequence of operations under WAL delay:
        -- Replace
        -- DDL
        -- Replace
        box.error.injection.set('ERRINJ_WAL_DELAY', true)
        fiber.create(function() box.space.test:replace{1, 1, 1} end)
        local f = fiber.create(function()
            box.space.test:format{{name = 'field1', type = 'scalar'}}
        end)
        fiber.create(function() box.space.test:replace{1, 1, 1} end)
        f:set_joinable(true)
        box.error.injection.set('ERRINJ_WAL_DELAY', false)

        local ok = f:join()
        t.assert(ok)
        t.assert_equals(box.space.test:count(), 1)
    end)
end

-- The test checks if isolation of transactions is not broken when DDL
-- is committed. It happened because we removed all memtx stories on commit
-- eariler.
g.test_txn_isolation_on_ddl_commit = function(cg)
    cg.server:exec(function()
        local fiber = require('fiber')

        local s = box.schema.space.create('test')
        s:create_index('pk', {parts = {{1}}})

        -- Few tuples to do DDL without yields
        for i = 1, 5 do
            s:replace{i}
        end

        -- Collect stories to be independent from GC steps
        box.internal.memtx_tx_gc(100)

        -- Alter space in a separate transaction
        local altered_space = false
        local f = fiber.create(function()
            s:format({{name = 'field1', type = 'unsigned'}})
            altered_space = true
        end)
        f:set_joinable(true)
        -- Make sure txn is still in progress
        t.assert(not altered_space)

        box.begin()
        -- Drop all the tuples in the new transaction
        for i = 1, 5 do
            s:delete(i)
        end
        -- Check if tuples were deleted
        t.assert_equals(s:select{}, {})

        -- Wait for DDL to be committed and make sure it was successful
        local ok = f:join()
        t.assert(ok)

        -- Check contents after yield
        t.assert_equals(s:select{}, {})

        box.commit()

        -- We committed a transaction deleting all tuples just now, result
        -- still must be empty
        t.assert_equals(s:select{}, {})
    end)
end

-- The test checks if DDL aborts all conflicting transactions - not preparing
-- the DDL but the operation itself. We should check this behavior because we
-- delete all memtx stories on DDL so we cannot support transaction isolation
-- anymore. If we didn't abort active transactions, rollback of DDL could
-- violate their isolation.
g.test_abort_all_on_ddl = function(cg)
    cg.server:exec(function()
        local fiber = require('fiber')
        local s = box.schema.space.create('test')
        s:create_index('pk', {parts = {{1}}})
        local errmsg = 'Transaction has been aborted by conflict'

        box.begin()
        s:replace{1}
        local ddl = fiber.create(function()
            s:create_index('sk')
        end)
        ddl:set_joinable(true)
        t.assert_error_msg_content_equals(errmsg, box.commit)
        local ok = ddl:join()
        t.assert(ok)
    end)
end

-- The test checks if rollback of DDL aborts all conflicting transactions since
-- they rely on new schema which is going to be rolled back. Since we already
-- abort all transactions on DDL itself and transaction doing DDL cannot yield,
-- the only way a concurrent transaction can appear is after DDL is prepared but
-- before it is committed. If DDL wasn't prepared (manual rollback), there
-- can't be concurrent transactions.
g.test_abort_all_ddl_rollback = function(cg)
    t.tarantool.skip_if_not_debug()
    cg.server:exec(function()
        local fiber = require('fiber')
        local s = box.schema.space.create('test')
        s:create_index('pk', {parts = {{1}}})
        local errmsg = 'Transaction has been aborted by conflict'

        box.error.injection.set('ERRINJ_WAL_DELAY', true)

        local ddl = fiber.create(function()
            s:create_index('sk')
        end)
        ddl:set_joinable(true)

        box.begin()
        s:replace{1}
        box.error.injection.set('ERRINJ_WAL_WRITE', true)
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
        local ok = ddl:join()
        t.assert_not(ok, 'DDL must fail and rollback due to WAL error')
        box.error.injection.set('ERRINJ_WAL_WRITE', false)
        t.assert_error_msg_content_equals(errmsg, box.commit)
    end)
end

g.test_rollback_mixed_ddl_and_dml = function(cg)
    cg.server:exec(function()
        local s = box.schema.space.create('test')
        s:create_index('pk', {parts = {{1}}})
        for i = 1, 10 do
            s:replace{i}
        end
        local saved_select = s:select{}
        box.begin()
        s:replace{1, 1}
        s:delete{1}
        s:delete{2}
        s:insert{11}
        s:alter({is_sync = true})
        s:replace{3, 1}
        s:delete{4}
        s:delete{5}
        s:insert{12}
        s:alter({is_sync = false})
        box.rollback()
        t.assert_equals(s:select{}, saved_select)
    end)
end

g.test_rollback_prepared_dml_and_ddl = function(cg)
    t.tarantool.skip_if_not_debug()
    cg.server:exec(function()
        local fiber = require('fiber')
        local s = box.schema.space.create('test')
        s:create_index('pk', {parts = {{1}}})
        for i = 1, 10 do
            s:replace{i}
        end
        local saved_select = s:select{}

        box.error.injection.set('ERRINJ_WAL_DELAY', true)
        local dml1 = fiber.create(function()
            box.begin()
            s:replace{1, 1}
            s:replace{2, 2}
            s:delete{3}
            s:delete{4}
            s:insert{100}
            box.commit()
        end)
        dml1:set_joinable(true)
        local ddl = fiber.create(function()
            s:create_index('sk')
        end)
        ddl:set_joinable(true)
        local dml2 = fiber.create(function()
            box.begin()
            s:replace{4, 4}
            s:replace{5, 5}
            s:delete{8}
            s:insert{200}
            box.commit()
        end)
        dml2:set_joinable(true)
        box.error.injection.set('ERRINJ_WAL_WRITE', true)
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
        local ok = dml1:join()
        t.assert_not(ok)
        ok = dml2:join()
        t.assert_not(ok)
        ok = ddl:join()
        t.assert_not(ok)
        t.assert_equals(s:select{}, saved_select)
    end)
end
