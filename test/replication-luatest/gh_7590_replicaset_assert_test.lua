local t = require('luatest')
local server = require('luatest.server')
local cluster = require('luatest.replica_set')

local g = t.group('gh-7590')

g.before_all(function(g)
    g.cluster = cluster:new({})
    g.cfg = {
        replication_timeout = 0.1,
        replication = {
            server.build_listen_uri('r1'),
            server.build_listen_uri('r2'),
            server.build_listen_uri('r3'),
        },
    }

    -- We need to specify uuid explicitly, so that rb tree traversal
    -- wouldn't take errored applier (applier from r2 to r3) first.
    g.cfg.instance_uuid = '00000000-0000-0000-0000-000000000001'
    g.r1 = g.cluster:build_and_add_server({alias = 'r1', box_cfg = g.cfg})

    g.cfg.instance_uuid = '00000000-0000-0000-0000-000000000002'
    g.r2 = g.cluster:build_and_add_server({alias = 'r2', box_cfg = g.cfg})

    g.cfg.instance_uuid = '00000000-0000-0000-0000-000000000003'
    g.r3 = g.cluster:build_and_add_server({alias = 'r3', box_cfg = g.cfg})

    g.cluster:start()
    g.r2:exec(function()
        box.schema.create_space('test'):create_index('pk')
    end)

    g.cluster:wait_for_fullmesh()
    g.r3:wait_for_vclock_of(g.r2)
    g.r1:wait_for_vclock_of(g.r2)
end)

g.after_all(function(g)
    g.cluster:stop()
end)

g.test_replicaset_state_machine_assert_fail = function(g)
    t.tarantool.skip_if_not_debug()
    g.r1:exec(function(replication_cfg)
        -- Let's drop replication from r2 instance so that it cannot
        -- trigger the destruction of applier from r1 to r3 at r3.
        -- Heartbeats must be send, as we don't want the applier to
        -- be destroyed on itself too.
        box.cfg({
            replication = {
                replication_cfg[1],
                replication_cfg[3],
            },
        })
    end, {g.cfg.replication})

    g.r3:exec(function()
        -- Throw ClientError as soon as write request from the r2 is received.
        box.error.injection.set('ERRINJ_WAL_IO', true)
        -- Don't allow applier_f fibers to be joined.
        box.error.injection.set('ERRINJ_APPLIER_DESTROY_DELAY', true);
    end)

    g.r2:exec(function()
        -- Send some data via relay in order to trigger
        -- destruction of applier from r2 to r3.
        box.space.test:insert({1})
    end)

    t.helpers.retrying({timeout = 5}, function()
        if not g.r3:grep_log('applier data destruction is delayed') then
            error("Applier destruction haven't started yet")
        end
    end)

    g.r3:exec(function()
        -- Errored applier can be waken up now. All others appliers will be
        -- allowed to do the same as soon as errored one is unyielded
        box.error.injection.set('ERRINJ_APPLIER_DESTROY_DELAY', false);
        box.error.injection.set('ERRINJ_APPLIER_STOP_DELAY', true);
        require('fiber').create(function()
            box.cfg({replication = {}})
        end)
    end)

    t.helpers.retrying({timeout = 5}, function()
        if not g.r3:grep_log('applier data destruction is continued') then
            error("Applier destruction haven't continued yet")
        end
    end)

    g.r3:exec(function()
        require('fiber').yield()
        box.error.injection.set('ERRINJ_APPLIER_STOP_DELAY', false);
        box.error.injection.set('ERRINJ_WAL_IO', false)
    end)
end
