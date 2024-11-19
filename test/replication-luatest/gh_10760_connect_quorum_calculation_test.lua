local t = require('luatest')
local server = require('luatest.server')
local replica_set = require('luatest.replica_set')

local g = t.group()

g.before_each(function(cg)
    t.tarantool.skip_if_not_debug()
    cg.replica_set = replica_set:new{}
    local box_cfg = {
        replication_timeout = 0.1,
        replication = {
            server.build_listen_uri('master', cg.replica_set.id),
        },
    }
    cg.master = cg.replica_set:build_and_add_server{
        alias = 'master',
        box_cfg = box_cfg,
    }
    cg.replica = cg.replica_set:build_and_add_server{
        alias = 'replica',
        box_cfg = box_cfg,
    }

    cg.master:start()
end)

-- Test that not yet joined or booting replicas are not accounted as a quorum to
-- sync with during master reconfiguration. Otherwise adding new replicas to a
-- cluster with centralized configuration wouldn't work: master would try to
-- sync with the not yet joined replicas and enter read-only state, and replicas
-- wouldn't be able to join due to master being read-only.
g.test_booting_replica_not_accounted_in_sync_quorum = function(cg)
    -- Make sure that master is connected to the booting replica and starts
    -- syncing before replica tries to join.
    cg.master:exec(function()
        t.assert(not box.info.ro)
        box.error.injection.set('ERRINJ_IPROTO_PROCESS_REPLICATION_DELAY', true)
    end)
    local cfg = {
        replication = {
            server.build_listen_uri('master', cg.replica_set.id),
            server.build_listen_uri('replica', cg.replica_set.id),
        },
        replication_connect_timeout = 1000,
    }
    cg.master:exec(function(cfg)
        require('fiber').new(function()
            box.cfg(cfg)
        end)
    end, {cfg})
    cg.replica:start{wait_until_ready = false}
    t.helpers.retrying({}, function()
        t.assert(cg.master:grep_log('connected to 2 replicas'))
    end)
    cg.master:exec(function()
        box.error.injection.set('ERRINJ_IPROTO_PROCESS_REPLICATION_DELAY',
                                false)
    end)
    cg.replica:wait_until_ready()
    t.helpers.retrying({}, function()
        cg.replica:assert_follows_upstream(cg.master:get_instance_id())
    end)
end

g.after_each(function(cg)
    cg.replica_set:drop()
end)
