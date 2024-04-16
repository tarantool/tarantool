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
        local space = box.schema.space.create('test', {if_not_exists = true})
        space:format({
             {name = 'id';    type = 'unsigned';},
             {name = 'data';  type = 'string';},
             {name = 'cnt';   type = 'unsigned';   is_nullable=true},
         })

        space:create_index('pk', {
            parts = {'id'},
            if_not_exists = true,
        })

        space:create_index('second', {
            parts = {{'data'}, {'cnt', exclude_null=true}},
            unique = false,
            if_not_exists = true,
        })
    end)
end)

g.after_each(function()
    g.server:exec(function()
        if box.space.test then
            box.space.test:drop()
        end
    end)
end)

-- Test right from the issue.
g.test_mvcc_with_exclude_null_base = function()
    g.server:exec(function()
        require('console').eval("box.space.test:insert({1, 'fdf'})")
    end)
end

-- Test that excluded tuple has no side effects.
g.test_mvcc_with_exclude_null_side_effects = function()
    g.server:exec(function()
        local txn_proxy = require("test.box.lua.txn_proxy")
        local tx1 = txn_proxy.new()
        local tx2 = txn_proxy.new()
        tx1:begin()
        tx2:begin()
        -- Perform a full scan.
        tx1("box.space.test.index.second:select{}")
        -- Perform a range scan.
        tx2("box.space.test.index.second:select({'a'}, {iterator = 'ge'})")

        box.space.test:insert{1, 'fdf'}

        -- Perform independent RW stmt.
        tx2("box.space.test:replace{2, 'a'}")
        t.assert_equals(tx2:commit(), '')
        -- Perform independent RW stmt.
        tx1("box.space.test:replace{3, 'b'}")
        t.assert_equals(tx1:commit(), '')
    end)
end

-- Test the rollback story.
g.test_mvcc_with_exclude_null_rollback = function()
    g.server:exec(function()
        box.begin()
        box.space.test:insert{1, 'fdf'}
        box.rollback()
    end)
end

-- Test the case with GC.
g.test_mvcc_with_exclude_null_collect_garbage = function()
    g.server:exec(function()
        for i = 1, 100 do
            box.space.test:insert{i, 'fdf'}
        end
        box.internal.memtx_tx_gc(10)
    end)
end

-- Test space drop case.
g.test_mvcc_with_exclude_null_space_drop = function()
    g.server:exec(function()
        local txn_proxy = require("test.box.lua.txn_proxy")
        for i = 1, 100 do
            local tx = txn_proxy.new()
            tx:begin()
            tx("box.space.test:insert{" .. i .. ", 'fdf'}")
        end
        box.space.test:drop()
    end)
end
