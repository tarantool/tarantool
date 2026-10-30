local server = require('luatest.server')
local t = require('luatest')

local pg = t.group(nil, {{idx = 'TREE'}, {idx = 'HASH'}})

pg.before_all(function(cg)
    cg.server = server:new{
        alias   = 'dflt',
        box_cfg = {memtx_use_mvcc_engine = true}
    }
    cg.server:start()
end)

pg.after_all(function(cg)
    cg.server:drop()
end)

pg.before_each(function(cg)
    cg.server:exec(function(idx)
        box.schema.create_space('s')
        box.space.s:create_index('pk', {type = idx})
    end, {cg.params.idx})
end)

pg.after_each(function(cg)
    cg.server:exec(function()
        box.space.s:drop()
    end)
end)

--[[
Checks that `index:random` from empty space is tracked correctly.
]]
pg.test_idx_random_from_empty_space_tracking = function(cg)
    cg.server:exec(function()
        local txn_proxy = require('test.box.lua.txn_proxy')

        local tx = txn_proxy:new()
        tx:begin()

        tx('box.space.s.index[0]:random()')
        tx('box.space.s:replace{1}')
        box.space.s:replace{0}
        local err_msg = "Transaction has been aborted by conflict"
        t.assert_equals(tx:commit(), {{error = err_msg}})
    end)
end

--[[
Checks that `index:random` result is tracked correctly.
]]
pg.test_idx_random_res_tracking = function(cg)
    cg.server:exec(function()
        local txn_proxy = require('test.box.lua.txn_proxy')

        local tx = txn_proxy:new()
        tx:begin()

        box.space.s:insert{0, 0}
        tx('box.space.s.index[0]:random()')
        tx('box.space.s:replace{1}')
        box.space.s:replace{0, 1}
        local err_msg = "Transaction has been aborted by conflict"
        t.assert_equals(tx:commit(), {{error = err_msg}})
    end)
end

--[[
Checks that `index:random` result is clarified correctly.
]]
pg.test_idx_random_res_clarification = function(cg)
    cg.server:exec(function()
        local txn_proxy = require('test.box.lua.txn_proxy')

        local tx1 = txn_proxy:new()
        local tx2 = txn_proxy:new()
        tx1:begin()
        tx2:begin()

        tx1('box.space.s:insert{0}')
        t.assert_equals(tx2('box.space.s.index[0]:random()'), "")
    end)
end

--[[
Checks that `index:random` works correctly in case the space is empty
from the transaction's perspective.
]]
pg.test_idx_random_empty_space_from_tx_perspective = function(cg)
    cg.server:exec(function()
        local txn_proxy = require('test.box.lua.txn_proxy')

        local tx = txn_proxy:new()
        tx:begin()

        tx('box.space.s:insert{0}')
        tx('box.space.s:insert{1}')
        tx('box.space.s:insert{2}')

        t.assert_equals(box.space.s.index[0]:random(), nil)
    end)
end

--[[
Checks that `index:random` works correctly in case the space has dirty tuples.
]]
pg.test_idx_random_with_dirty_tuples = function(cg)
    cg.server:exec(function()
        local txn_proxy = require('test.box.lua.txn_proxy')

        local tx = txn_proxy:new()
        tx:begin()

        box.space.s:insert{0}
        for i = 1, 100000 do
            tx(('box.space.s:insert{%d}'):format(i))
        end
        t.assert_equals(box.space.s.index[0]:random(), {0})
    end)
end
