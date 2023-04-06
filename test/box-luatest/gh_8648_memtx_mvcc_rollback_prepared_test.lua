local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function(cg)
    cg.server = server:new{
        alias   = 'default',
        box_cfg = {memtx_use_mvcc_engine = true}
    }
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.before_each(function(cg)
    cg.server:exec(function()
        box.schema.create_space('test'):create_index('pk')
    end)
end)

g.after_each(function(cg)
    cg.server:exec(function()
        box.space.test:drop()
    end)
end)

-- Test that ensures that rollback of prepared works correctly.
-- Case when there was a tuple before transactions.
g.test_rollback_prepared_simple_with_tuple = function(cg)
    t.tarantool.skip_if_not_debug()
    cg.server:exec(function()
        box.space.test:create_index('sk', {parts={{2}}})
        local txn_proxy = require("test.box.lua.txn_proxy")

        box.space.test:replace{1, 0, 0}

        local tx1 = txn_proxy.new()
        tx1:begin()
        tx1('box.space.test:replace{1, 1, 1}')
        -- {1, 0, 0} is invisible since it is replaced by {1, 1, 1}
        t.assert_equals(tx1('box.space.test.index.sk:select{0}'), {{}})

        local tx2 = txn_proxy.new()
        tx2:begin()
        tx2('box.space.test:replace{1, 2, 2}')

        -- Prepare and rollback tx2.
        box.error.injection.set('ERRINJ_WAL_WRITE', true)
        tx2:commit()
        box.error.injection.set('ERRINJ_WAL_WRITE', false)

        -- {1, 0, 0} must remain invisible since it is replaced by {1, 1, 1}
        t.assert_equals(tx1('box.space.test.index.sk:select{0}'), {{}})
        -- Must be successful
        t.assert_equals(tx1:commit(), "")
    end)
end

-- Test that ensures that rollback of prepared works correctly.
-- Case when there were no tuple before transactions.
g.test_rollback_prepared_simple_without_tuple = function(cg)
    t.tarantool.skip_if_not_debug()
    cg.server:exec(function()
        box.space.test:create_index('sk', {parts={{2}}})
        local txn_proxy = require("test.box.lua.txn_proxy")

        local tx1 = txn_proxy.new()
        tx1:begin()
        tx1('box.space.test:replace{11, 21}')

        local tx2 = txn_proxy.new()
        tx2:begin()
        tx2('box.space.test:replace{11, 22}')

        -- Prepare and rollback tx2.
        box.error.injection.set('ERRINJ_WAL_WRITE', true)
        tx2:commit()
        box.error.injection.set('ERRINJ_WAL_WRITE', false)

        tx1:rollback()

        -- Nothing of the above must be visible since everything is rollbacked.
        t.assert_equals(box.space.test.index.pk:select{11}, {})
        t.assert_equals(box.space.test.index.sk:select{21}, {})
        t.assert_equals(box.space.test.index.sk:select{22}, {})
    end)
end

-- Test that ensures that rollback of prepared works correctly.
-- Case with rollback of prepared DELETE statement.
g.test_rollback_prepared_simple_delete = function(cg)
    t.tarantool.skip_if_not_debug()
    cg.server:exec(function()
        box.space.test:create_index('sk', {parts={{2}}})
        local txn_proxy = require("test.box.lua.txn_proxy")

        box.space.test:replace{21, 30}
        local tx1 = txn_proxy.new()
        tx1:begin()
        tx1('box.space.test:replace{21, 31}')
        -- tx1 must overwrite {21, 30}.
        t.assert_equals(tx1('box.space.test.index.sk:select{30}'), {{}})

        local tx2 = txn_proxy.new()
        tx2:begin()
        tx2('box.space.test:delete{21}')

        -- Prepare and rollback tx2.
        box.error.injection.set('ERRINJ_WAL_WRITE', true)
        tx2:commit()
        box.error.injection.set('ERRINJ_WAL_WRITE', false)

        -- tx1 must continue to overwrite {21, 30}.
        t.assert_equals(tx1('box.space.test.index.sk:select{30}'), {{}})

        tx1:rollback()
    end)
end

-- The first test from issue.
g.test_rollback_prepared_complicated = function(cg)
    t.tarantool.skip_if_not_debug()
    cg.server:exec(function()
        local txn_proxy = require("test.box.lua.txn_proxy")

        box.space.test:replace{1, 1, 1}

        -- tx1 is made just for preventing further GC of {1, 1, 1} story.
        local tx1 = txn_proxy.new()
        tx1:begin()
        tx1('box.space.test:select{1}') -- {1, 1, 1}

        box.space.test:delete{1} -- tx1 goes to read view and owns {1, 1, 1}

        local tx2 = txn_proxy.new()
        tx2:begin()
        tx2('box.space.test:replace{1, 2, 2}')

        local tx3 = txn_proxy.new()
        tx3:begin()
        tx3('box.space.test:replace{1, 3, 3}')

        -- Prepare and rollback tx2.
        box.error.injection.set('ERRINJ_WAL_WRITE', true)
        t.assert_equals(tx2:commit(), {{error = "Failed to write to disk"}})
        box.error.injection.set('ERRINJ_WAL_WRITE', false)

        -- Must be OK
        t.assert_equals(tx3:rollback(), "")

        -- Must return {}
        t.assert_equals(box.space.test:select{1}, {})
    end)
end
