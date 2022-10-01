local server = require('test.luatest_helpers.server')
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
Checks that server does not crash on series of transactions from gh-7756.
]]
g.test_server_crash_on_series_of_txs = function(cg)
    local stream1 = cg.server.net_box:new_stream()
    local stream2 = cg.server.net_box:new_stream()
    local stream3 = cg.server.net_box:new_stream()

    stream1:begin()
    stream2:begin()
    stream3:begin()

    stream1.space.s:insert{3}
    stream1:rollback()

    stream2.space.s:insert{0}

    stream3.space.s:select({2}, {iterator = cg.params.iter})

    cg.server:exec(function()
        box.space.s:insert{1}
    end)
end
