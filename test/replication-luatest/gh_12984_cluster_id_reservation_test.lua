local replica_set = require('luatest.replica_set')
local t = require('luatest')

local g = t.group(nil, t.helpers.matrix({
    memtx_use_mvcc_engine = {false, true},
}))

g.before_each(function(cg)
    cg.replica_set = replica_set:new({})
    cg.master = cg.replica_set:build_and_add_server({
        alias = 'master',
        box_cfg = {memtx_use_mvcc_engine = cg.params.memtx_use_mvcc_engine},
    })
    cg.replica_set:start()
end)

g.after_each(function(cg)
    cg.replica_set:drop()
end)

g.test_savepoint_releases_reservation = function(cg)
    cg.master:exec(function()
        local uuid = require('uuid')
        local first = {2, uuid.str()}
        local second = {31, uuid.str()}
        box.begin()
        box.space._cluster:insert(first)
        local savepoint = box.savepoint()
        box.space._cluster:insert(second)
        box.rollback_to_savepoint(savepoint)
        t.assert_equals(box.space._cluster:insert(second), second)
        box.commit()
        t.assert_equals(box.info.replication[2].uuid, first[2])
        t.assert_equals(box.info.replication[31].uuid, second[2])
        box.space._cluster:delete{31}
        t.assert_equals(box.space._cluster:insert(second), second)
    end)
end
