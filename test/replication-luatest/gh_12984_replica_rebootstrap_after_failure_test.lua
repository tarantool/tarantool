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
                replication_timeout = 0.1,
            },
        })
    end
    cg.replica_set:start()
    cg.replica_set:wait_for_fullmesh()
end)

g.after_each(function(cg)
    cg.replica_set:drop()
end)

g.test_delete_releases_name = function(cg)
    local replica_id = cg.replica:get_instance_id()
    cg.replica:exec(function()
        box.cfg{replication = {}}
    end)
    cg.master:exec(function(replica_id)
        t.helpers.retrying({}, function()
            t.assert_equals(box.info.replication[replica_id].downstream.status,
                            'stopped')
        end)
        t.assert_equals(
            box.info.replication[replica_id].upstream.status, 'follow')
        local tuple = box.space._cluster:delete{replica_id}
        t.assert_equals(box.space._cluster:get{replica_id}, nil)
        t.assert_equals(box.info.replication[replica_id], nil)
        t.assert_equals(box.space._cluster:insert(tuple), tuple)
        local info = box.info.replication[replica_id]
        t.assert_equals(info.uuid, tuple[2])
        t.assert_equals(info.name, tuple[3])
        t.assert_equals(info.upstream.status, 'follow')
    end, {replica_id})
end
