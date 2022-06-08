local server = require('test.luatest_helpers.server')
local t = require('luatest')

local pg = t.group(nil, t.helpers.matrix({engine = {'memtx', 'vinyl'},
                                          op = {'select', 'pairs', 'get'}}))

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
    cg.server:exec(function(engine)
        box.schema.create_space('s', {engine = engine})
        box.space.s:create_index('pk')
    end, {cg.params.engine})
end)

pg.after_each(function(cg)
    cg.server:exec(function()
        box.space.s:drop()
    end)
end)

--[[
Checks that transactions in "conflicted" state unconditionally throw
`Transaction has been aborted by conflict` error on read-only operations.
]]
pg.test_conflicted_txs_ro_operations = function(cg)
    local stream1 = cg.server.net_box:new_stream()
    local stream2 = cg.server.net_box:new_stream()

    stream1:begin()
    stream2:begin()

    stream1.space.s:select{}
    stream2.space.s:replace{0}
    stream1.space.s:delete{0}
    stream2:commit()

    local conflict_err_msg = 'Transaction has been aborted by conflict'
    local call = ('box.space.s:%s{0}'):format(cg.params.op)
     t.assert_error_msg_content_equals(conflict_err_msg, function()
         stream1:eval(call)
     end)
end
