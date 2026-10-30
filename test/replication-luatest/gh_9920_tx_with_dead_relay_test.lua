local t = require('luatest')
local replica_set = require('luatest.replica_set')
local server = require('luatest.server')

local g = t.group()

g.before_all(function(cg)
    cg.replica_set = replica_set:new{}
    local box_cfg = {
        replication = {
            server.build_listen_uri('server1', cg.replica_set.id),
            server.build_listen_uri('server2', cg.replica_set.id),
        },
        replication_timeout = 0.1,
    }
    cg.server1 = cg.replica_set:build_and_add_server{
        alias = 'server1',
        box_cfg = box_cfg,
    }
    cg.server2 = cg.replica_set:build_and_add_server{
        alias = 'server2',
        box_cfg = box_cfg,
    }
    cg.replica_set:start()
    cg.replica_set:wait_for_fullmesh()
    cg.server1:exec(function()
        box.schema.space.create('test'):create_index('pk')
    end)
    cg.server2:wait_for_vclock_of(cg.server1)
end)

g.after_all(function(cg)
    cg.replica_set:drop()
end)

g.test_tx_send_msg_to_dead_relay = function(cg)
    cg.server1:exec(function()
        box.error.injection.set('ERRINJ_RELAY_TX_STATUS_UPDATE_DELAY', true)
        box.error.injection.set('ERRINJ_RELAY_REPORT_INTERVAL', 1e-9)
        box.space.test:insert({1})
    end)

    cg.server2:wait_for_vclock_of(cg.server1)
    cg.server2:stop()

    t.helpers.retrying({}, function()
        t.assert(cg.server1:grep_log('exiting the relay loop'))
    end)

    cg.server1:exec(function()
        box.error.injection.set('ERRINJ_RELAY_TX_STATUS_UPDATE_DELAY', false)
        box.error.injection.set('ERRINJ_RELAY_REPORT_INTERVAL', 0)
    end)

    t.assert(cg.server1.process:is_alive())
    cg.server2:start()
end
