local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function()
    g.server = server:new{
        alias   = 'dflt',
        box_cfg = {memtx_use_mvcc_engine = true}
    }
    g.server:start()
    g.server:exec(function()
        local s = box.schema.create_space('s')
        s:create_index('pk', {type = 'HASH'})
    end)
end)

g.after_all(function()
    g.server:drop()
end)

g.test_replace_self_conflict_after_fsc_in_hash_idx = function()
    g.server:exec(function()
        local txn_proxy = require('test.box.lua.txn_proxy')

        local tx = txn_proxy:new()

        tx('box.begin()')
        tx('box.space.s:select{}')
        tx('box.space.s:replace{0}')
        t.assert_equals(tx:commit(), "")
    end)
end

g.test_insert_self_conflict_after_fsc_in_hash_idx = function()
    g.server:exec(function()
        local txn_proxy = require('test.box.lua.txn_proxy')

        local tx = txn_proxy:new()

        tx('box.begin()')
        tx('box.space.s:select{}')
        tx('box.space.s:insert{0}')
        t.assert_equals(tx:commit(), "")
    end)
end

g.test_upsert_self_conflict_after_fsc_in_hash_idx = function()
    g.server:exec(function()
        local txn_proxy = require('test.box.lua.txn_proxy')

        local tx = txn_proxy:new()

        tx('box.begin()')
        tx('box.space.s:select{}')
        tx('box.space.s:upsert({0}, {{"=", 1, 0}})')
        t.assert_equals(tx:commit(), "")
    end)
end
