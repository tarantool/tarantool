local server = require('test.luatest_helpers.server')
local t = require('luatest')

local g = t.group(nil, t.helpers.matrix({pk_type = {'TREE', 'HASH'},
                                        sk_type = {'TREE', 'HASH'}}))

g.before_all(function(cg)
    cg.server = server:new {
        alias   = 'dflt',
        box_cfg = {memtx_use_mvcc_engine = true}
    }
    cg.server:start()
    cg.server:exec(function(pk_type, sk_type)
        local s = box.schema.create_space('s')
        s:create_index('pk', {type = pk_type})
        s:create_index('sk', {type = sk_type, parts = {2}})
    end, {cg.params.pk_type, cg.params.sk_type})
end)

g.after_all(function(cg)
    cg.server:drop()
end)

--[[
Checks that memtx secondary index uniqueness is not violated.
]]
g.test_memtx_sk_index_unique_violation = function(cg)
    local stream1 = cg.server.net_box:new_stream()
    local stream2 = cg.server.net_box:new_stream()

    stream1:begin()
    stream2:begin()

    stream1.space.s:insert{0, 1}
    stream2.space.s:insert{1, 0}
    stream2.space.s:replace{1, 1}

    stream1:commit()
    local conflict_err_msg = 'Transaction has been aborted by conflict'
    t.assert_error_msg_content_equals(conflict_err_msg,
                                      function() stream2:commit() end)
end
