local t = require('luatest')
local log = require('log')
local fio = require('fio')
local Cluster =  require('test.luatest_helpers.cluster')
local server = require('test.luatest_helpers.server')
local COUNT = 100

local pg = t.group('quorum_misc', {{engine = 'memtx'}, {engine = 'vinyl'}})

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

    local box_cfg = {
        replication_timeout = 0.05,
        replication_connect_timeout = 10,
        replication_connect_quorum = 1,
        replication = {server.build_instance_uri('replica_quorum'),
                       server.build_instance_uri('replica_quorum1'),
                       server.build_instance_uri('replica_quorum2')}
    }
    cg.replica_quorum = cg.cluster:build_server({alias = 'replica_quorum', engine = engine, box_cfg = box_cfg})

    pcall(log.cfg, {level = 6})

end)

pg.after_each(function(cg)
    cg.cluster.servers = nil
    cg.cluster:drop()
end)

pg.before_test('test_quorum_during_reconfiguration', function(cg)
    cg.cluster:add_server(cg.replica_quorum)
end)

pg.after_test('test_quorum_during_reconfiguration', function(cg)
    cg.cluster:drop({cg.replica_quorum})
end)

pg.test_quorum_during_reconfiguration = function(cg)
    -- Test that quorum is not ignored neither during bootstrap, nor
    -- during reconfiguration.
    cg.replica_quorum:start()
    local function cmd(args)
        return (box.cfg{
                    replication = {
                        os.getenv("TARANTOOL_LISTEN"),
                        args.nonexistent_uri
                    }
                })
    end
    local nonexistent_uri = server.build_instance_uri('replica_quorum1')
    t.helpers.retrying({timeout = 10},
        function()
            -- If replication_connect_quorum was ignored here, the instance
            -- would exit with an error.
            -- XXX: box.cfg() returns nothing in either way, I don't see a
            -- point to check it. box.cfg() may raise an error: will it be
            -- re-raised here?
            t.assert_equals(cg.replica_quorum:exec(cmd, {nonexistent_uri}), nil)
        end
    )

    t.assert_equals(cg.replica_quorum:eval('return box.info.id'), 1)
end

pg.before_test('test_id_for_rebootstrapped_replica_with_removed_xlog',
function(cg)
    cg.cluster:add_server(cg.quorum1)
    cg.cluster:add_server(cg.quorum2)
    cg.cluster:add_server(cg.quorum3)
    cg.cluster:start()
    local bootstrap_function = function()
        box.schema.space.create('test', {engine = os.getenv('TARANTOOL_ENGINE')})
        box.space.test:create_index('primary')
    end
    cg.cluster:exec_on_leader(bootstrap_function)
    cg.cluster:wait_fullmesh()
    t.helpers.retrying({timeout = 10},
        function()
            cg.quorum2:eval(
                ('for i = 1, %d do box.space.test:insert{i} end'):format(COUNT))
        end
    )
end)

pg.test_id_for_rebootstrapped_replica_with_removed_xlog = function(cg)
    cg.quorum1:stop()
    fio.rmtree(cg.quorum1.workdir)
    fio.mktree(cg.quorum1.workdir)
    -- The rebootstrapped replica will be assigned id = 4,
    -- because ids 1..3 are busy.
    cg.quorum1:start()
    t.assert_equals(cg.quorum1.net_box.state, 'active',
        'wrong state for server="%s"', cg.quorum1.alias)
    t.helpers.retrying({timeout = 10}, function()
        t.assert_equals(
            cg.quorum1:eval('return box.space.test:count()'), COUNT)
        t.assert_equals(
            cg.quorum2:eval(
                'return box.info.replication[4].upstream.status'), 'follow')
        t.assert(cg.quorum3:eval('return box.info.replication ~= nil'))
        t.assert_equals(
            cg.quorum3:eval(
                'return box.info.replication[4].upstream.status'), 'follow')
    end)

    t.assert_equals(
        cg.quorum2:eval('return box.info.replication[4].upstream.status'),
        'follow')
    t.assert_equals(
        cg.quorum3:eval('return box.info.replication[4].upstream.status'),
        'follow')
end
