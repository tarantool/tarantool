local t = require('luatest')
local server = require('luatest.server')
local replica_set = require('luatest.replica_set')

local g = t.group()

g.before_each(function(cg)
    cg.replica_set = replica_set:new{}
    cg.box_cfg = {
        replication = {
            server.build_listen_uri('server1', cg.replica_set.id),
            server.build_listen_uri('server2', cg.replica_set.id),
        },
        replication_timeout = 0.1,
    }
    cg.servers = {}
    for i = 1, 2 do
        cg.servers[i] = cg.replica_set:build_and_add_server{
            alias = 'server' .. i,
            box_cfg = cg.box_cfg,
        }
    end
    cg.replica_set:start()
    cg.replica_set:wait_for_fullmesh()
    cg.servers[1]:exec(function()
        box.schema.space.create('test')
        box.space.test:create_index('pk')
        box.schema.space.create('loc', {is_local = true})
        box.space.loc:create_index('pk')
    end)
end)

g.after_each(function(cg)
    cg.replica_set:drop()
end)

--
-- gh-9491: make sure that transactions ending with a local row are correctly
-- recovered and replicated by relay.
--
g.test_local_row_tx_boundary = function(cg)
    -- Stop replication, write some transactions to WAL, then restart
    -- replication.
    cg.servers[2]:update_box_cfg{replication = ""}
    cg.servers[1]:exec(function()
        box.begin()
        box.space.test:replace{1}
        box.space.loc:replace{1}
        box.commit()
        box.begin()
        box.space.test:replace{2}
        box.space.loc:replace{2}
        box.commit()
    end)
    cg.servers[2]:update_box_cfg{replication = cg.box_cfg.replication}
    t.helpers.retrying({}, function()
        cg.servers[2]:assert_follows_upstream(cg.servers[1]:get_instance_id())
        cg.servers[1]:wait_for_downstream_to(cg.servers[2])
    end)
    cg.servers[1]:exec(function(servers2_id)
        t.assert_equals(box.info.replication[servers2_id].downstream.status,
                        'follow')
    end, {cg.servers[2]:get_instance_id()})
    cg.servers[2]:exec(function()
        t.assert_equals(box.space.test:select{}, {{1}, {2}})
        t.assert_equals(box.space.loc:select{}, {})
    end)
end
