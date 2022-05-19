local server = require('test.luatest_helpers.server')
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
        local function dump(o, depth)
            if not depth then
                depth = 1
            end
            if type(o) == 'table' then
                local s = '{ \n'
                for k, v in pairs(o) do
                    if type(k) ~= 'number' then k = '"'..k..'"' end
                    s = s .. string.rep('\t', depth) .. '['..k..'] = ' .. dump(v, depth + 1) .. ',\n'
                end
                return s .. string.rep('\t', depth - 1) .. ' } '
            else
                return tostring(o)
            end
        end
        local t = require('luatest')
        local txn_proxy = require('test.box.lua.txn_proxy')

        local tx = txn_proxy:new()

        local conflict_err = 'Transaction has been aborted by conflict'

        for i=1,100000 do
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
        end
    end)
end
