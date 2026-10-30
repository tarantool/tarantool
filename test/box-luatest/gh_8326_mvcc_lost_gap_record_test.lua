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
        local s = box.schema.space.create('s')
        s:create_index('pk', {parts = {{1, 'uint'}, {2, 'uint'}}})
        s:create_index('sk', {type = 'hash', parts = {{2, 'uint'}}})
    end)
end)

g.after_each(function()
    g.server:exec(function()
        box.space.s:drop()
    end)
end)

g.test_lost_gap_record = function()
    g.server:exec(function()
        local txn_proxy = require("test.box.lua.txn_proxy")

        local tx1 = txn_proxy.new()
        local tx2 = txn_proxy.new()
        tx1:begin()
        tx2:begin()

        tx1('box.space.s:select{1}') -- select by partial key {1}, empty result
        tx1('box.space.s:replace{1, 1, 1}') -- write right to selected

        tx2('box.space.s:replace{1, 1, 2}') -- overwrite by the second TX
        tx2:commit() -- ok, now tx1 must become conflicted because of select{1}

        t.assert_equals(tx1:commit(),
                        {{error = "Transaction has been aborted by conflict"}})
        t.assert_equals(box.space.s:select{1}, {{1, 1, 2}})
    end)
end

g.test_lost_full_scan_record = function()
    g.server:exec(function()
        local txn_proxy = require("test.box.lua.txn_proxy")

        local tx1 = txn_proxy.new()
        local tx2 = txn_proxy.new()
        tx1:begin()
        tx2:begin()

        tx1('box.space.s.index.sk:select{}') -- secondary fullscan, empty result
        tx1('box.space.s:replace{1, 1, 1}') -- write to selected

        tx2('box.space.s:replace{1, 1, 2}') -- overwrite by the second TX
        tx2:commit() -- ok, now tx1 must become conflicted because of select{1}

        t.assert_equals(tx1:commit(),
                        {{error = "Transaction has been aborted by conflict"}})
        t.assert_equals(box.space.s:select{1}, {{1, 1, 2}})
    end)
end
