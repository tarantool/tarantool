local t = require('luatest')
local server = require('luatest.server')
local replica_set = require('luatest.replica_set')

local g = t.group('gh_9748')

g.before_all(function(cg)
    cg.replica_set = replica_set:new({})
    cg.replication = {
        server.build_listen_uri('server1', cg.replica_set.id),
        server.build_listen_uri('server2', cg.replica_set.id),
    }
    local box_cfg = {
        replication = cg.replication,
        replication_timeout = 0.1,
    }
    for i = 1, 2 do
        cg['server'..i] = cg.replica_set:build_and_add_server{
            alias = 'server' .. i,
            box_cfg = box_cfg,
        }
    end
    cg.replica_set:start()
    cg.replica_set:wait_for_fullmesh()
    cg.server1:exec(function()
        box.schema.create_space('test'):create_index('pk')
    end)
end)

g.before_each(function(cg)
    for _, s in pairs(cg.replica_set.servers) do
        s:update_box_cfg{replication = cg.replication}
    end
    cg.server1:wait_for_downstream_to(cg.server2)
    cg.server2:wait_for_downstream_to(cg.server1)
end)

g.after_all(function(cg)
    cg.replica_set:drop()
end)

--
-- gh-9748: applier on reconnect must not send an old txn timestamp. Relay on
-- the other side might think it was an actual confirmation of an old txn, and
-- would ruin the downstream lag.
--
g.test_lag_on_master_restart = function(cg)
    cg.server1:exec(function()
        box.space.test:replace{1}
    end)
    cg.server2:wait_for_vclock_of(cg.server1)
    cg.server1:stop()
    -- To make vclock different. In case it would matter on the server1 side
    -- anyhow.
    cg.server2:exec(function()
        box.space.test:replace{2}
    end)
    cg.server1:start()
    t.helpers.retrying({}, function()
        cg.server2:assert_follows_upstream(cg.server1:get_instance_id())
    end)
    cg.server1:exec(function(id)
        t.assert_equals(box.info.replication[id].downstream.lag, 0)
    end, {cg.server2:get_instance_id()})
end
