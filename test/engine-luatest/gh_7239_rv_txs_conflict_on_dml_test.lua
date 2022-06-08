local server = require('test.luatest_helpers.server')
local t = require('luatest')

local pg = t.group(nil, t.helpers.matrix({engine = {'memtx', 'vinyl'},
                                          op = {'replace', 'insert', 'delete',
                                                'upsert'}}))

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
Checks that transactions in "read-view" state are conflicted on attempt to
perform DML operations.
]]
pg.test_gh_7239_rv_txs_conflict_on_dml = function(cg)
    local stream1 = cg.server.net_box:new_stream()
    local stream2 = cg.server.net_box:new_stream()

    stream1:begin()
    stream1.space.s:select{}

    stream2:begin()
    stream2.space.s:replace{0}
    stream2:commit()

    local conflict_err_msg = 'Transaction has been aborted by conflict'
    local args = {{0}}
    if cg.params.op == 'upsert' then
        table.insert(args, {{'=', 2, 0}})
    end
    t.assert_error_msg_content_equals(conflict_err_msg, function()
        stream1.space.s[cg.params.op](stream1.space.s, args[1], args[2])
    end)
end

--[[
Checks that transactions in "read-view" state which do not perform DML
operations get committed successfully.
]]
pg.test_rv_txs_ro_committed_successfully = function(cg)
    local stream1 = cg.server.net_box:new_stream()
    local stream2 = cg.server.net_box:new_stream()

    stream1:begin()
    stream1.space.s:select{}

    stream2:begin()
    stream2.space.s:replace{0}
    stream2:commit()

    t.assert_is(stream1:commit(), nil)
end
