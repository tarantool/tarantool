local t = require('luatest')
local cluster = require('luatest.replica_set')
local fio = require('fio')
local server = require('luatest.server')

local g = t.group('replica-does-not-receive-its-own-rows')

local function wait_pair_sync(server1, server2)
    -- Without retrying it fails sometimes when vclocks are empty and both
    -- instances are in 'connect' state instead of 'follow'.
    t.helpers.retrying({timeout = 10}, function()
        server1:wait_for_vclock_of(server2)
        server2:wait_for_vclock_of(server1)
        server1:assert_follows_upstream(server2:get_instance_id())
        server2:assert_follows_upstream(server1:get_instance_id())
    end)
end

g.before_each(function(cg)
    cg.cluster = cluster:new({})

    cg.master = cg.cluster:build_and_add_server({
        alias = 'master',
        box_cfg = {
            replication = {
                server.build_listen_uri('master', cg.cluster.id),
            },
            replication_timeout = 0.1,
        }
    })
    cg.cluster:start()
    cg.master:exec(function()
        box.schema.space.create('test')
        box.space.test:create_index('pk')
    end)
end)

g.after_each(function(cg)
    cg.cluster:drop()
end)

g.test_main = function(cg)
    local box_cfg = {
        replication = {
            server.build_listen_uri('master', cg.cluster.id),
            server.build_listen_uri('replica', cg.cluster.id),
        },
        replication_timeout = 0.1,
    }
    cg.replica = cg.cluster:build_and_add_server({
        alias = 'replica',
        box_cfg = box_cfg,
    })
    cg.replica:start()
    --wait_pair_sync(cg.replica, cg.master)

    t.helpers.retrying({timeout = 10}, function()
        cg.replica:wait_for_vclock_of(cg.master)
        cg.replica:assert_follows_upstream(cg.master:get_instance_id())
    end)
    box_cfg.replication = { server.build_listen_uri('replica', cg.cluster.id), }
    cg.master:exec(function(box_cfg) box.cfg(box_cfg) end, { box_cfg })
    cg.replica:exec(function()
        box.cfg{ replication = '', replication_timeout = 0.1, }
    end)
    t.helpers.retrying({timeout = 10}, cg.master.exec, cg.master, function()
    	t.assert_equals(box.info.status, 'running', 'The server is running')
    end)
    cg.master:exec(function()
        t.assert(box.space.test:insert{1})
    end)
    cg.replica:exec(function()
        t.assert(box.space.test:insert{2})
    end)
end
