local server = require('test.luatest_helpers.server')
local t = require('luatest')
local g = t.group()

g.before_all = function()
    g.server = server:new{
        alias   = 'default',
        box_cfg = {memtx_use_mvcc_engine = true}
    }
    g.server:start()
end

g.after_all = function()
    g.server:drop()
end

g.test_mvcc_crash_in_prepare_case1 = function()
    g.server:exec(function()
        local t = require('luatest')
        local txn_proxy = require("test.box.lua.txn_proxy")

        box.cfg{memtx_use_mvcc_engine = true}
        local s = box.schema.space.create("s", {engine="memtx"})
        s:create_index("pk", {type="tree"})
        s:create_index("sk", {parts={2}, type="hash"})

        local tx1 = txn_proxy.new()
        local tx2 = txn_proxy.new()
        local tx3 = txn_proxy.new()
        local tx4 = txn_proxy.new()

        tx1:begin()
        tx2:begin()
        tx3:begin()
        tx4:begin()

        tx1("box.space.s:insert{1, 'A'}")
        tx1:commit()
        tx2("box.space.s:replace{2, 'B'}")
        tx3("box.space.s:replace{1, 'B'}")
        tx4("box.space.s:replace{3, 'B'}")

        tx2:rollback()
        t.assert_equals(tx4:commit(), "")
        t.assert_equals(tx3:commit(),
                        {{error = "Transaction has been aborted by conflict"}})

        s:drop()

    end)
end

g.test_mvcc_crash_in_prepare_case2 = function()
    g.server:exec(function()
        local t = require('luatest')
        local txn_proxy = require("test.box.lua.txn_proxy")

        box.cfg{memtx_use_mvcc_engine = true}
        local s = box.schema.space.create("s", {engine="memtx"})
        s:create_index("pk", {type="tree"})
        s:create_index("sk", {parts={2}, type="hash"})

        local tx1 = txn_proxy.new()
        local tx2 = txn_proxy.new()
        local tx3 = txn_proxy.new()

        tx1:begin()
        tx2:begin()
        tx3:begin()

        tx1('box.space.s:replace{1, "A"}')
        tx2('box.space.s:select{1}')
        tx2('box.space.s:insert{2, "B"}')
        tx3('box.space.s:replace{3, "A"}')
        tx3('box.space.s:replace{1, "C"}')
        t.assert_equals(tx3:commit(), "")
        tx1:rollback()
        t.assert_equals(tx2:commit(),
                        {{error = "Transaction has been aborted by conflict"}})

        s:drop()

    end)
end
