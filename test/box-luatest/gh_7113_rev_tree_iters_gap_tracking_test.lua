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

g.before_each(function()
    g.server:exec(function()
        local s = box.schema.space.create('s')
        s:create_index('pk', {parts = {{1, 'unsigned'},
                                       {2, 'unsigned'}}})
        s:insert{0, 0}
        s:insert{1, 0}
    end)
end)

g.after_each(function()
    g.server:eval('box.space.s:drop()')
end)

g['test_reverse_iter_gap_tracking'] = function()
    g.server:exec(function()
        local txn_proxy = require('test.box.lua.txn_proxy')

        local tx = txn_proxy:new()

        local conflict_err = 'Transaction has been aborted by conflict'

        tx:begin()
        tx('box.space.s:select({1, 0}, {iterator = "LT"})')
        box.space.s:insert{0, 1}
        tx('box.space.s:insert{2, 0}')
        t.assert_equals(tx:commit(), {{error = conflict_err}})
        box.space.s:delete{0, 1}

        tx:begin()
        tx('box.space.s:select({1, 0}, {iterator = "LE"})')
        box.space.s:insert{0, 1}
        tx('box.space.s:insert{2, 0}')
        t.assert_equals(tx:commit(), {{error = conflict_err}})
        box.space.s:delete{0, 1}

        tx:begin()
        tx('box.space.s:select({1}, {iterator = "LT"})')
        box.space.s:insert{0, 1}
        tx('box.space.s:insert{2, 0}')
        t.assert_equals(tx:commit(), {{error = conflict_err}})
        box.space.s:delete{0, 1}

        tx:begin()
        tx('box.space.s:select({1}, {iterator = "LE"})')
        box.space.s:insert{0, 1}
        tx('box.space.s:insert{2, 0}')
        t.assert_equals(tx:commit(), {{error = conflict_err}})
        box.space.s:delete{0, 1}

        tx:begin()
        tx('box.space.s:select({0}, {iterator = "REQ"})')
        box.space.s:insert{0, 1}
        tx('box.space.s:insert{2, 0}')
        t.assert_equals(tx:commit(), {{error = conflict_err}})
        box.space.s:delete{0, 1}

        tx:begin()
        tx('box.space.s:select({1}, {iterator = "REQ"})')
        box.space.s:insert{1, 1}
        tx('box.space.s:insert{2, 0}')
        t.assert_equals(tx:commit(), {{error = conflict_err}})
        box.space.s:delete{1, 1}

        tx:begin()
    end)
end

g['test_reverse_iter_clarify_before_gap_tracking'] = function()
    g.server:exec(function()
        local txn_proxy = require('test.box.lua.txn_proxy')

        local tx = txn_proxy:new()

        --[[
            The following tests are a safety net for catching the buggy case
            when tuple clarification could be done after gap tracking
            (gh-7073).
        --]]
        box.internal.memtx_tx_gc(128)

        tx:begin()
        box.space.s:delete{0, 0}
        t.assert_equals(tx("box.space.s:select({1, 0}, {iterator = 'LT'})"),
                        {{}})
        tx:commit()
        box.space.s:insert{0, 0}

        tx:begin()
        box.space.s:delete{0, 0}
        t.assert_equals(tx("box.space.s:select({0, 0}, {iterator = 'LE'})"),
                        {{}})
        tx:commit()
        box.space.s:insert{0, 0}

        tx:begin()
        box.space.s:delete{0, 0}
        t.assert_equals(tx("box.space.s:select({1}, {iterator = 'LT'})"),
                        {{}})
        tx:commit()
        box.space.s:insert{0, 0}

        tx:begin()
        box.space.s:delete{0, 0}
        t.assert_equals(tx("box.space.s:select({0}, {iterator = 'LE'})"),
                        {{}})
        tx:commit()
        box.space.s:insert{0, 0}

        tx:begin()
        box.space.s:delete{0, 0}
        t.assert_equals(tx("box.space.s:select({0}, {iterator = 'REQ'})"),
                        {{}})
        tx:commit()
        box.space.s:insert{0, 0}
    end)
end
