local t = require('luatest')
local log = require('log')
local Cluster =  require('test.luatest_helpers.cluster')
local server = require('test.luatest_helpers.server')
local json = require('json')

local pg = t.group('quorum_master', {{engine = 'memtx'}, {engine = 'vinyl'}})

pg.before_each(function(cg)
    local engine = cg.params.engine
    cg.cluster = Cluster:new({})

    cg.box_cfg = {
        replication = {
            server.build_instance_uri('master_quorum1');
            server.build_instance_uri('master_quorum2');
        };
        replication_connect_quorum = 0;
        replication_timeout = 0.1;
    }

    cg.master_quorum1 = cg.cluster:build_server(
        {alias = 'master_quorum1', engine = engine, box_cfg = cg.box_cfg})

    cg.master_quorum2 = cg.cluster:build_server(
        {alias = 'master_quorum2', engine = engine, box_cfg = cg.box_cfg})

    pcall(log.cfg, {level = 6})

end)

pg.after_each(function(cg)
    cg.cluster.servers = nil
    cg.cluster:drop()
end)

pg.before_test('test_master_master_works', function(cg)
    cg.cluster:add_server(cg.master_quorum1)
    cg.cluster:add_server(cg.master_quorum2)
    cg.cluster:start()
    local bootstrap_function = function()
        box.schema.space.create('test', {engine = os.getenv('TARANTOOL_ENGINE')})
        box.space.test:create_index('primary')
    end
    cg.cluster:exec_on_leader(bootstrap_function)

end)

pg.after_test('test_master_master_works', function(cg)
    cg.cluster:drop({cg.master_quorum1, cg.master_quorum2})
end)

pg.test_master_master_works = function(cg)
    local repl = json.encode({replication = cg.box_cfg.replication})
    cg.master_quorum1:eval('box.cfg{replication = ""}')
    t.assert_equals(cg.master_quorum1:eval('return box.space.test:insert{1}'), {1})
    cg.master_quorum1:eval(('box.cfg{replication = %s}'):format(repl.replication))
    local vclock = cg.master_quorum1:eval('return box.info.vclock')
    cg.master_quorum2:wait_vclock(vclock)
    t.assert_equals(cg.master_quorum2:eval('return box.space.test:select()'), {{1}})
end
