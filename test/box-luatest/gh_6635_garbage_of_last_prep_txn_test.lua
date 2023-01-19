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

g.test_garbage_of_last_prepared_txn_cannot_be_deleted = function()
    g.server:exec(function()
        local t = require('luatest')

        local s = box.schema.create_space('test')
        s:create_index('pk')

        -- Collect garbage before replacing tuples
        box.internal.memtx_tx_gc(10)
        collectgarbage('collect')

        s:replace{0, string.rep('a', 1100)}
        box.internal.memtx_tx_gc(10)
        collectgarbage('collect')
        local items_used_with_huge_tuple = box.slab.info()['items_used']

        s:replace{0, 1}
        box.internal.memtx_tx_gc(10)
        collectgarbage('collect')
        local items_used_without_huge_tuple = box.slab.info()['items_used']
        t.assert_lt(items_used_without_huge_tuple, items_used_with_huge_tuple)
        t.assert_ge(items_used_with_huge_tuple - items_used_without_huge_tuple, 1000)
    end)
end
