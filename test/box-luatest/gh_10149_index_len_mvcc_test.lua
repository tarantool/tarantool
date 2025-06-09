local t = require('luatest')
local server = require('luatest.server')

local g = t.group('gh-10149-index-len-mvcc')

g.before_all(function(cg)
    cg.server = server:new{box_cfg = {memtx_use_mvcc_engine = true}}
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.after_each(function(cg)
    cg.server:exec(function()
        if box.space.test then
            box.space.test:drop()
        end
    end)
end)

-- Test if the tree index's len() method is recorded in MVCC.
g.test_index_len_mvcc_tree = function(cg)
    cg.server:exec(function()
        -- Create a space with data.
        local s = box.schema.space.create('test')
        s:create_index('pk')
        s:insert({1})

        -- Prepare the proxies.
        local txn_proxy = require('test.box.lua.txn_proxy')
        local tx1 = txn_proxy.new()
        local tx2 = txn_proxy.new()

        -- TX1 open a transaction and reads the index size.
        tx1:begin()
        tx1('box.space.test:len()')

        -- TX2 changes the index size (now TX1 must be conflicted).
        tx2('box.space.test:insert({2})')

        -- TX1 does a writing operation (must fail).
        t.assert_equals(tx1('box.space.test:delete({1})'),
                        {{error = "Transaction has been aborted by conflict"}})
    end)
end

-- Test if the hash index's len() method is recorded in MVCC.
g.test_index_len_mvcc_hash = function(cg)
    cg.server:exec(function()
        -- Create a space with data.
        local s = box.schema.space.create('test')
        s:create_index('pk', {type = 'hash'})
        s:insert({1})

        -- Prepare the proxies.
        local txn_proxy = require('test.box.lua.txn_proxy')
        local tx1 = txn_proxy.new()
        local tx2 = txn_proxy.new()

        -- TX1 open a transaction and reads the index size.
        tx1:begin()
        tx1('box.space.test:len()')

        -- TX2 changes the index size (now TX1 must be conflicted).
        tx2('box.space.test:insert({2})')

        -- TX1 does a writing operation (must fail).
        t.assert_equals(tx1('box.space.test:delete({1})'),
                        {{error = "Transaction has been aborted by conflict"}})
    end)
end

-- Test if the bitset index's len() method is recorded in MVCC.
g.test_index_len_mvcc_bitset = function(cg)
    cg.server:exec(function()
        -- Create a space with data.
        local s = box.schema.space.create('test')
        s:create_index('pk')
        s:create_index('sk', {type = 'bitset'})
        s:insert({1, 1})

        -- Prepare the proxies.
        local txn_proxy = require('test.box.lua.txn_proxy')
        local tx1 = txn_proxy.new()
        local tx2 = txn_proxy.new()

        -- TX1 open a transaction and reads the index size.
        tx1:begin()
        tx1('box.space.test.index.sk:len()')

        -- TX2 changes the index size (now TX1 must be conflicted).
        tx2('box.space.test:insert({2, 2})')

        -- TX1 does a writing operation (must fail).
        t.assert_equals(tx1('box.space.test.index.sk:delete({1})'),
                        {{error = "Transaction has been aborted by conflict"}})
    end)
end

-- Test if the rtree index's len() method is recorded in MVCC.
g.test_index_len_mvcc_rtree = function(cg)
    cg.server:exec(function()
        -- Create a space with data.
        local s = box.schema.space.create('test')
        s:create_index('pk')
        s:create_index('sk', {type = 'rtree'})
        s:insert({1, {1, 1}})

        -- Prepare the proxies.
        local txn_proxy = require('test.box.lua.txn_proxy')
        local tx1 = txn_proxy.new()
        local tx2 = txn_proxy.new()

        -- TX1 open a transaction and reads the index size.
        tx1:begin()
        tx1('box.space.test.index.sk:len()')

        -- TX2 changes the index size (now TX1 must be conflicted).
        tx2('box.space.test:insert({2, {2, 2}})')

        -- TX1 does a writing operation (must fail).
        t.assert_equals(tx1('box.space.test.index.sk:delete({{1, 1}})'),
                        {{error = "Transaction has been aborted by conflict"}})
    end)
end
