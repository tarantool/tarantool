local t = require('luatest')
local log = require('log')
local Cluster =  require('test.luatest_helpers.cluster')
local server = require('test.luatest_helpers.server')

local pg = t.group('quorum_orphan', {{engine = 'memtx'}, {engine = 'vinyl'}})

pg.before_each(function(cg)
    local engine = cg.params.engine
    cg.cluster = Cluster:new({})

    local box_cfg = {
        replication_timeout = 0.1;
        replication_connect_timeout = 10;
        replication_sync_lag = 0.01;
        replication_connect_quorum = 3;
        replication = {
            server.build_instance_uri('quorum1');
            server.build_instance_uri('quorum2');
            server.build_instance_uri('quorum3');
        };
    }

    cg.quorum1 = cg.cluster:build_server({alias = 'quorum1', engine = engine, box_cfg = box_cfg})
    cg.quorum2 = cg.cluster:build_server({alias = 'quorum2', engine = engine, box_cfg = box_cfg})
    cg.quorum3 = cg.cluster:build_server({alias = 'quorum3', engine = engine, box_cfg = box_cfg})

    pcall(log.cfg, {level = 6})

end)

pg.after_each(function(cg)
    cg.cluster.servers = nil
    cg.cluster:drop()
end)

pg.before_test('test_replica_is_orphan_after_restart', function(cg)
    cg.cluster:add_server(cg.quorum1)
    cg.cluster:add_server(cg.quorum2)
    cg.cluster:add_server(cg.quorum3)
    cg.cluster:start()
    local bootstrap_function = function()
        box.schema.space.create('test', {engine = os.getenv('TARANTOOL_ENGINE')})
        box.space.test:create_index('primary')
    end
   cg.cluster:exec_on_leader(bootstrap_function)

end)

pg.test_replica_is_orphan_after_restart = function(cg)
    -- Stop one replica and try to restart another one.
    -- It should successfully restart, but stay in the
    -- 'orphan' mode, which disables write accesses.
    -- There are three ways for the replica to leave the
    -- 'orphan' mode:
    -- * reconfigure replication
    -- * reset box.cfg.replication_connect_quorum
    -- * wait until a quorum is formed asynchronously
    cg.cluster:wait_fullmesh()
    cg.quorum1:stop()
    cg.quorum2:restart()
    t.assert_equals(cg.quorum2.net_box.state, 'active')
    t.assert_str_matches(cg.quorum2:eval('return box.info.status'), 'orphan')
    t.assert_error_msg_content_equals('timed out', function()
        cg.quorum2:eval('return box.ctl.wait_rw(0.001)')
    end)
    t.assert(cg.quorum2:eval('return box.info.ro'))
    t.helpers.retrying({timeout = 30}, function()
        t.assert(cg.quorum2:eval('return box.space.test ~= nil'))
    end)
    t.assert_error_msg_content_equals(
        "Can't modify data because this instance is in read-only mode.",
        function()
            cg.quorum2:eval('return box.space.test:replace{100}')
        end
    )
    cg.quorum2:eval('box.cfg{replication={}}')
    t.assert_str_matches(
        cg.quorum2:eval('return box.info.status'), 'running')
    cg.quorum2:restart()
    t.assert_equals(cg.quorum2.net_box.state, 'active')
    t.assert_str_matches(
        cg.quorum2:eval('return box.info.status'), 'orphan')
    t.assert_error_msg_content_equals('timed out', function()
        cg.quorum2:eval('return box.ctl.wait_rw(0.001)')
    end)
    t.assert(cg.quorum2:eval('return box.info.ro'))
    t.assert(cg.quorum2:eval('return box.space.test ~= nil'))
    t.assert_error_msg_content_equals(
        "Can't modify data because this instance is in read-only mode.",
        function()
            cg.quorum2:eval('return box.space.test:replace{100}')
        end
    )

    cg.quorum2:eval('box.cfg{replication_connect_quorum = 2}')
    cg.quorum2:eval('return box.ctl.wait_rw()')
    t.assert_not(cg.quorum2:eval('return box.info.ro'))
    t.assert_str_matches(cg.quorum2:eval('return box.info.status'), 'running')
    cg.quorum2:restart()
    t.assert_equals(cg.quorum2.net_box.state, 'active')
    t.assert_str_matches(cg.quorum2:eval('return box.info.status'), 'orphan')
    t.assert_error_msg_content_equals('timed out', function()
            cg.quorum2:eval('return box.ctl.wait_rw(0.001)')
    end)
    t.assert(cg.quorum2:eval('return box.info.ro'))
    t.assert(cg.quorum2:eval('return box.space.test ~= nil'))
    t.assert_error_msg_content_equals(
        "Can't modify data because this instance is in read-only mode.",
        function()
            cg.quorum2:eval('return box.space.test:replace{100}')
        end
    )
    cg.quorum1:start()
    cg.quorum2:eval('return box.ctl.wait_rw()')
    t.assert_not(cg.quorum2:eval('return box.info.ro'))
    t.assert_str_matches(cg.quorum2:eval('return box.info.status'), 'running')

end
