local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function(cg)
    cg.server = server:new{
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
        local s = box.schema.space.create('s')
        s:create_index('pk')
        s:create_index('sk', {parts = {{2, 'uint', is_nullable = true}}})
    end)
end)

g.after_each(function(cg)
    cg.server:exec(function()
        box.space.s:drop()
    end)
end)

--[[
Checks that phantom reads with `get` from nullable TREE index are not allowed.
]]
g.test_memtx_tree_idx_get_with_nullable_field_phantom_read = function(cg)
    cg.server:exec(function()
        local txn_proxy = require('test.box.lua.txn_proxy')

        local tx = txn_proxy:new()
        tx:begin()

        tx('box.space.s.index[1]:get{0}')
        box.space.s:replace{0, 0}
        t.assert_equals(tx('box.space.s.index[1]:get{0}'), '')
    end)
end

--[[
Checks that reads with `get` from nullable TREE index are tracked correctly.
]]
g.test_memtx_tree_idx_get_with_nullable_field_read_tracked = function(cg)
    cg.server:exec(function()
        local txn_proxy = require('test.box.lua.txn_proxy')

        local tx = txn_proxy:new()
        tx:begin()

        tx('box.space.s.index[1]:get{0}')
        tx('box.space.s:replace{1, 1}')
        box.space.s:replace{0, 0}
        t.assert_equals(tx:commit(),
                        {{error = 'Transaction has been aborted by conflict'}})
    end)
end
