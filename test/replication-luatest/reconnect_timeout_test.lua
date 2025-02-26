local t = require('luatest')
local server = require('luatest.server')
local replica_set = require('luatest.replica_set')

local g = t.group()

g.before_all(function(cg)
    cg.replica_set = replica_set:new{}
    local box_cfg = {
        replication_reconnect_timeout = 0.1,
        replication_timeout = 0.1,
        replication = {
            server.build_listen_uri('master', cg.replica_set.id),
            server.build_listen_uri('replica', cg.replica_set.id),
        },
    }

    for _, name in ipairs({'master', 'replica'}) do
        cg[name] = cg.replica_set:build_and_add_server{
            alias = name,
            box_cfg = box_cfg,
        }
    end

    cg.replica_set:start()
    cg.master:exec(function()
        box.schema.space.create('test')
        box.space.test:create_index('pk')
    end)
    cg.replica:wait_for_vclock_of(cg.master)
end)

g.after_all(function(cg)
    cg.replica_set:drop()
end)


g.test_replication_reconnect_timeout = function(cg)
    local replica_id = cg.replica:get_instance_id()
    local master_id = cg.master:get_instance_id()

    local function replica_wait_for_disconnected_upstream()
        cg.replica:exec(function(id)
            t.helpers.retrying({}, function()
                t.assert_equals(box.info.replication[id].upstream.status,
                                'disconnected')
            end)
        end, {master_id})
    end

    local function replicaset_wait_for_alive_replication()
        t.helpers.retrying({}, function()
            cg.master:assert_follows_upstream(replica_id)
            cg.replica:assert_follows_upstream(master_id)
        end)
    end

    -- 1. Replication is fully alive.
    replicaset_wait_for_alive_replication()

    --
    -- 2. Replication is broken, since master's relay stops sending heartbeats
    -- and applier goes into loop of reconnections and TimeOut errors.
    --
    cg.master:update_box_cfg({replication_timeout = 10000})
    replica_wait_for_disconnected_upstream()

    --
    -- 3. Replication restores, replication_timeout is the same on all
    -- instances. Note, that without replication_reconnect_timeout it
    -- is impossible to restore replication at that point, since
    -- applier will sleep before reconnection for `replication_timeout`.
    --
    cg.replica:update_box_cfg({replication_timeout = 10000})
    replicaset_wait_for_alive_replication()

    -- Restore replication_timeout.
    cg.replica:update_box_cfg({replication_timeout = 0.1})
    cg.master:update_box_cfg({replication_timeout = 0.1})
    replicaset_wait_for_alive_replication()
end
