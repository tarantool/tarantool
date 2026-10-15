local replica_set = require('luatest.replica_set')
local t = require('luatest')

local g = t.group(nil, t.helpers.matrix({
    memtx_use_mvcc_engine = {false, true},
}))

g.before_each(function(cg)
    cg.replica_set = replica_set:new({})
    cg.master = cg.replica_set:build_and_add_server({
        alias = 'master',
        box_cfg = {
            instance_uuid = 'cccccccc-cccc-cccc-cccc-cccccccccccc',
            memtx_use_mvcc_engine = cg.params.memtx_use_mvcc_engine,
        },
    })
    cg.replica_set:start()
end)

g.after_each(function(cg)
    cg.replica_set:drop()
end)

g.test_uuid_lookup_ignores_case = function(cg)
    cg.master:exec(function()
        local id = box.info.id
        local uuid = box.info.uuid
        box.space._cluster:update(id, {{'=', 2, uuid:upper()}})
        t.assert_equals(box.info.id, id)
        t.assert_equals(box.info.uuid, uuid)
        box.cfg{instance_name = 'master'}
        t.assert_equals(box.info.id, id)
        t.assert_equals(box.info.name, 'master')
        t.assert_equals(box.space._cluster:count(), 1)
        t.assert_equals(
            box.space._cluster:get{id}, {id, uuid:upper(), 'master'})
    end)
end
