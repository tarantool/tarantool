local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function()
    g.server = server:new{
        alias   = 'dflt',
        box_cfg = {memtx_use_mvcc_engine = true}
    }
    g.server:start()
end)

g.after_all(function()
    g.server:drop()
end)

g.after_each(function()
    g.server:exec(function()
        box.space.s:drop()
    end)
end)

g.test_repeatable_insert_primary_idx = function()
    g.server:exec(function()
        local txn_proxy = require('test.box.lua.txn_proxy')

        local s = box.schema.create_space('s')
        s:create_index('pk')

        local tx1 = txn_proxy:new()
        local tx2 = txn_proxy:new()

        tx1('box.begin()')
        tx2('box.begin()')

        tx1('box.space.s:replace{0, 0}')

        tx2('box.space.s:replace{0, 1}')
        tx2('box.space.s:delete{0}')
        tx2('box.space.s:insert{0, 2}')

        tx1('box.commit()')
        t.assert_equals(tx2:commit(), "")
    end)
end

g.test_repeatable_insert_secondary_idx = function()
    g.server:exec(function()
        local txn_proxy = require('test.box.lua.txn_proxy')

        local s = box.schema.create_space('s')
        s:create_index('pk')
        s:create_index('sk', {parts = {2, 'unsigned'}})

        local tx1 = txn_proxy:new()
        local tx2 = txn_proxy:new()

        tx1('box.begin()')
        tx2('box.begin()')

        tx1('box.space.s:replace{0, 0, 0}')

        tx2('box.space.s:replace{0, 0, 1}')
        tx2('box.space.s:delete{0}')
        tx2('box.space.s:insert{0, 0, 2}')

        tx1('box.commit()')
        t.assert_equals(tx2:commit(), "")
    end)
end

g.test_repeatable_upsert_primary_idx = function()
   g.server:exec(function()
       local txn_proxy = require('test.box.lua.txn_proxy')

       local s = box.schema.create_space('s')
       s:create_index('pk')

       local tx1 = txn_proxy:new()
       local tx2 = txn_proxy:new()

       tx1('box.begin()')
       tx2('box.begin()')

       tx1('box.space.s:replace{0, 0}')

       tx2('box.space.s:replace{0, 1}')
       tx2('box.space.s:delete{0}')
       tx2('box.space.s:upsert({0, 2}, {{"=", 2, 2}})')

       tx1('box.commit()')
        t.assert_equals(tx2:commit(), "")
    end)
end

g.test_repeatable_upsert_primary_idx = function()
   g.server:exec(function()
       local txn_proxy = require('test.box.lua.txn_proxy')

       local s = box.schema.create_space('s')
       s:create_index('pk')
       s:create_index('sk', {parts = {2, 'unsigned'}})

       local tx1 = txn_proxy:new()
       local tx2 = txn_proxy:new()

       tx1('box.begin()')
       tx2('box.begin()')

       tx1('box.space.s:replace{0, 0, 0}')

       tx2('box.space.s:replace{0, 0, 1}')
       tx2('box.space.s:delete{0}')
       tx2('box.space.s:upsert({0, 0, 2}, {{"=", 3, 2}})')

       tx1('box.commit()')
        t.assert_equals(tx2:commit(), "")
    end)
end
