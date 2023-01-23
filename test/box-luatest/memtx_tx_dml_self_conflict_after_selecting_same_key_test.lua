local server = require('luatest.server')
local t = require('luatest')

local pg = t.group(nil, {{idx = 'TREE'}, {idx = 'HASH'}})

pg.before_each(function(cg)
    cg.server = server:new{
        alias   = 'dflt',
        box_cfg = {memtx_use_mvcc_engine = true}
    }
    cg.server:start()
    cg.server:exec(function()
        box.schema.create_space('s')
    end)
    local fmt = 'box.space.s:create_index("pk", {type = "%s"})'
    cg.server:eval(fmt:format(cg.params.idx))
end)

pg.after_each(function(cg)
    cg.server:drop()
end)

pg.test_replace_self_conflict_after_selecting_same_key = function(cg)
    cg.server:exec(function()
        local txn_proxy = require('test.box.lua.txn_proxy')

        local tx = txn_proxy:new()

        tx('box.begin()')
        tx('box.space.s:select{0}')
        tx('box.space.s:replace{0}')
        t.assert_equals(tx:commit(), "")
    end)
end

pg.test_insert_self_conflict_after_selecting_same_key = function(cg)
    cg.server:exec(function()
        local txn_proxy = require('test.box.lua.txn_proxy')

        local tx = txn_proxy:new()

        tx('box.begin()')
        tx('box.space.s:select{0}')
        tx('box.space.s:insert{0}')
        t.assert_equals(tx:commit(), "")
    end)
end

pg.test_upsert_self_conflict_after_selecting_same_key = function(cg)
    cg.server:exec(function()
        local txn_proxy = require('test.box.lua.txn_proxy')

        local tx = txn_proxy:new()

        tx('box.begin()')
        tx('box.space.s:select{0}')
        tx('box.space.s:upsert({0}, {{"=", 1, 0}})')
        t.assert_equals(tx:commit(), "")
    end)
end
