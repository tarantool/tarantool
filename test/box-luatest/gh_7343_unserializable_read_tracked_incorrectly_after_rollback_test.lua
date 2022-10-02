local server = require('test.luatest_helpers.server')
local t = require('luatest')

local pg = t.group(nil, t.helpers.matrix({op = {'get', 'delete', 'update'}}))

pg.before_all(function(cg)
    cg.server = server:new{
        alias   = 'dflt',
        box_cfg = {memtx_use_mvcc_engine = true}
    }
    cg.server:start()
    cg.server:exec(function()
        box.schema.create_space('s')
        box.space.s:create_index('pk')
    end)
end)

pg.after_all(function(cg)
    cg.server:drop()
end)

--[[
Checks that unserializable reads are tracked correctly after transaction
rollback.
]]
pg.test_unserializable_read_after_rollback = function(cg)
    local stream1 = cg.server.net_box:new_stream()
    local stream2 = cg.server.net_box:new_stream()
    local stream3 = cg.server.net_box:new_stream()

    stream1:begin()
    stream2:begin()
    stream3:begin()

    stream1.space.s:replace{0, 0}

    local args = {{0}}
    if cg.params.op == 'update' then
        table.insert(args, {{'=', 2, 0}})
    end
    stream2.space.s[cg.params.op](stream2.space.s, unpack(args))
    stream2.space.s:replace{1, 0}

    stream1:rollback()

    stream3.space.s:insert{0, 1}
    stream3:commit()

    local conflict_err_msg = 'Transaction has been aborted by conflict'
    t.assert_error_msg_content_equals(conflict_err_msg, function()
        stream2:commit()
    end)
end

local g = t.group()

g.before_all(function(cg)
    cg.server = server:new{
        alias   = 'dflt',
        box_cfg = {memtx_use_mvcc_engine = true}
    }
    cg.server:start()
    cg.server:exec(function()
        local s = box.schema.create_space('s')
        s:create_index('pk')
    end)
end)

g.after_all(function(cg)
    cg.server:drop()
end)

--[[
Checks that gap are tracked correctly after transaction rollback.
]]
g.test_gap_tracked_correctly_rollback = function(cg)
    local stream1 = cg.server.net_box:new_stream()
    local stream2 = cg.server.net_box:new_stream()

    stream1:begin()
    stream2:begin()

    stream1.space.s:insert{1}
    -- Gap from {0} to {1} is tracked.
    stream2.space.s:select({0}, {iterator = 'LE'})
    -- {1} containing gap tracker is rolled back.
    stream1:rollback()

    cg.server:exec(function()
        -- Gap write into [0; 1].
        box.space.s:insert{0}
    end)
    t.assert_equals(stream2.space.s:select({0}, {iterator = 'LE'}), {})
    t.assert_error_msg_content_equals('Transaction has been aborted by conflict',
                                      function() stream2.space.s:insert{1} end)
end
