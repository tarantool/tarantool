local replica_set = require('luatest.replica_set')
local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_each(function(cg)
    cg.replica_set = replica_set:new({})
    local replication = {
        server.build_listen_uri('master', cg.replica_set.id),
        server.build_listen_uri('replica', cg.replica_set.id),
    }
    for _, name in ipairs({'master', 'replica'}) do
        cg[name] = cg.replica_set:build_and_add_server({
            alias = name,
            box_cfg = {
                instance_name = name,
                read_only = name == 'replica',
                replication = replication,
            },
        })
    end
    cg.replica_set:start()
    cg.replica_set:wait_for_fullmesh()
end)

g.after_each(function(cg)
    cg.replica_set:drop()
end)

g.test_uuid_update_without_applier_rollback = function(cg)
    cg.master:exec(function()
        local uuid = require('uuid')
        local original = uuid.str()
        box.space._cluster:insert{3, original, 'unconnected'}
        box.begin()
        box.space._cluster:update(3, {{'=', 2, uuid.str()}})
        box.rollback()
        t.assert_equals(box.space._cluster:get{3}[2], original)
        t.assert_equals(box.info.replication[3].uuid, original)
    end)
end
