local server = require('luatest.server')
local t = require('luatest')

local g = t.group(nil, t.helpers.matrix{iter = {'LT', 'LE'}})

g.before_all(function(cg)
    cg.server = server:new {
        alias   = 'dflt',
        box_cfg = {memtx_use_mvcc_engine = true}
    }
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.before_each(function(cg)
    cg.server:exec(function()
        local s = box.schema.create_space('s')
        s:create_index('pk')
        box.internal.memtx_tx_gc(1)
    end)
end)

g.after_each(function(cg)
    cg.server:exec(function()
        box.space.s:drop()
    end)
end)

--[[
Checks that repeatable read violation with reverse iterators is not possible.
]]
g.test_repeatable_read_violation_with_rev_iter = function(cg)
    cg.server:exec(function(iter)
        local txn_proxy = require('test.box.lua.txn_proxy')

        local tx1 = txn_proxy:new()
        local tx2 = txn_proxy:new()
        local tx3 = txn_proxy:new()

        tx1('box.begin()')
        tx2('box.begin()')
        tx3('box.begin()')

        tx1('box.space.s:insert{3}')
        tx1('box.rollback()')

        tx2('box.space.s:insert{0}')
        local read_operation = 'box.space.s:select({2}, {iterator = "%s"})'
        read_operation = read_operation:format(iter)
        tx3(read_operation)
        box.space.s:insert{1}

        t.assert_equals(tx3(read_operation), {{}})
        t.assert_equals(tx3('box.space.s:insert{3}'),
                        {{error = 'Transaction has been aborted by conflict'}})
    end, {cg.params.iter})
end
