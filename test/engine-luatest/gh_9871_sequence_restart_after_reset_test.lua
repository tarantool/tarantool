local server = require('luatest.server')
local t = require('luatest')

local group_config = {
    {engine = 'memtx', use_txn = true},
    {engine = 'memtx', use_txn = false},
    {engine = 'vinyl', use_txn = false}
}
local g = t.group('storage', group_config)

g.before_all(function(cg)
    cg.server = server:new()
    cg.server:start()
end)

g.before_each(function(cg)
    cg.server:exec(function(engine, use_txn)
        box.schema.sequence.create('id_seq')
        local s = box.schema.create_space('test', {
            format = {
                {'id', type='unsigned', is_nullable=false},
                {'name', type='string', is_nullable=false},
            },
            engine = engine,
            if_not_exists = true,
        })
        s:create_index('primary', {
            sequence = 'id_seq',
            if_not_exists = true
        })
        if use_txn then
            box.begin()
        end
        s:insert{nil, 'aaa'}
        s:insert{nil, 'bbb'}
        s:insert{nil, 'ccc'}
        local seq = box.sequence.id_seq
        seq:reset()
        s:delete(1)
        if use_txn then
            box.commit()
        end
    end, {cg.params.engine, cg.params.use_txn})
end)

local function commit()
    local s = box.space.test
    s:insert{nil, 'ddd'}
    local ddd = s:get(1)
    t.assert_equals(ddd[2], 'ddd')
end

g.test_no_recovery = function(cg)
    cg.server:exec(commit)
end

g.test_snapshot_recovery = function(cg)
    cg.server:exec(function()
        box.snapshot()
    end)
    cg.server:restart()
    cg.server:exec(commit)
end

g.test_wal_recovery = function(cg)
    cg.server:restart()
    cg.server:exec(commit)
end

g.after_each(function(cg)
    cg.server:exec(function()
        box.space.test:drop()
        box.sequence.id_seq:drop()
    end)
end)

g.after_all(function(cg)
    cg.server:drop()
end)
