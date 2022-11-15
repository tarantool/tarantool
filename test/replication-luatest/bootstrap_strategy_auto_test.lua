local t = require('luatest')
local server = require('luatest.server')
local replica_set = require('luatest.replica_set')

local fio = require('fio')

local g = t.group('gh-5272-bootstrap-strategy-auto')

local uuid1 = '11111111-1111-1111-1111-111111111111'
local uuid2 = '22222222-2222-2222-2222-222222222222'
local uuid3 = '33333333-3333-3333-3333-333333333333'

local function server_is_ready(server)
    local ok, _ = pcall(function()
        server:connect_net_box()
        server:exec(function()
            assert(_G.ready)
        end)
    end)
    return ok
end

local function assert_master_waits_for_replica(server, uuid)
    local logfile = fio.pathjoin(server.workdir, server.alias .. '.log')
    -- Escape '-' in uuid.
    local uuid_pattern = uuid:gsub('%-', '%%-')
    local pattern = 'Checking if ' .. uuid_pattern ..
                    ' at .* chose this instance as bootstrap leader'
    t.assert(server:grep_log(pattern, nil, {filename = logfile}),
             'Server ' .. server.alias .. ' waits for replica ' .. uuid)
end

g.after_each(function(cg)
    cg.replica_set:drop()
end)

g.before_test('test_auto_bootstrap_waits_for_confirmations', function(cg)
    cg.replica_set = replica_set:new{}
    cg.box_cfg = {
        replication = {
            server.build_listen_uri('server1'),
            server.build_listen_uri('server2'),
        },
        replication_connect_timeout = 1000,
        replication_timeout = 0.1,
    }
    -- Make server1 the bootstrap leader.
    cg.box_cfg.instance_uuid = uuid1
    cg.server1 = cg.replica_set:build_and_add_server{
        alias = 'server1',
        box_cfg = cg.box_cfg,
    }
    cg.box_cfg.replication[3] = server.build_listen_uri('server3')
    cg.box_cfg.instance_uuid = uuid2
    cg.server2 = cg.replica_set:build_and_add_server{
        alias = 'server2',
        box_cfg = cg.box_cfg,
    }
    cg.box_cfg.instance_uuid = uuid3
    cg.server3 = cg.replica_set:build_and_add_server{
        alias = 'server3',
        box_cfg = cg.box_cfg,
    }
end)

g.test_auto_bootstrap_waits_for_confirmations = function(cg)
    cg.server1:start{wait_until_ready = false}
    cg.server2:start{wait_until_ready = false}
    t.helpers.retrying({}, assert_master_waits_for_replica, cg.server1, uuid2)
    t.assert(not server_is_ready(cg.server1) and
             not server_is_ready(cg.server2), 'Servers wait for fullmesh')
    -- Start the third server so that server2 finishes waiting for connections
    -- and unblocks server1.
    cg.server3:start{wait_until_ready = true}
    t.assert(server_is_ready(cg.server1), 'Server1 finished bootstrap')
    t.assert_equals(cg.server1:get_instance_id(), 1,
                    'Server1 is the bootstrap leader')
end

g.before_test('test_join_checks_fullmesh', function(cg)
    cg.replica_set = replica_set:new{}
    cg.box_cfg = {
        replication = {
            server.build_listen_uri('server1'),
            server.build_listen_uri('server2'),
        },
        replication_timeout = 0.1,
    }
    cg.box_cfg.instance_uuid = uuid1
    cg.server1 = cg.replica_set:build_and_add_server{
        alias = 'server1',
        box_cfg = cg.box_cfg,
    }
    cg.box_cfg.instance_uuid = uuid2
    cg.server2 = cg.replica_set:build_and_add_server{
        alias = 'server2',
        box_cfg = cg.box_cfg,
    }
    cg.replica_set:start()
end)

g.test_join_checks_fullmesh = function(cg)
    cg.box_cfg.replication[2] = nil
    cg.server3 = cg.replica_set:build_server{
        alias = 'server3',
        box_cfg = cg.box_cfg,
    }
    cg.server3:start{wait_until_ready = false}
    local logfile = fio.pathjoin(cg.server3.workdir, 'server3.log')
    local uuid_pattern = uuid2:gsub('%-', '%%-')
    local pattern = 'No connection to ' .. uuid_pattern
    t.helpers.retrying({}, function()
           t.assert(cg.server3:grep_log(pattern, nil, {filename = logfile}),
                    'Server3 detected a missing connection to ' .. uuid2)
    end)
    t.assert(not server_is_ready(cg.server3), 'Server3 is dead')
    -- Server is dead, stopping it will fail.
    cg.server3:clean()
end

g.before_test('test_sync_waits_for_all_connected', function(cg)
    cg.replica_set = replica_set:new{}
    cg.box_cfg = {
        replication_timeout = 0.1,
    }
    cg.master = cg.replica_set:build_and_add_server{
        alias = 'master',
        box_cfg = cg.box_cfg,
    }
    cg.box_cfg.replication = {
        server.build_listen_uri('master'),
    }
    cg.replica = cg.replica_set:build_and_add_server{
        alias = 'replica',
        box_cfg = cg.box_cfg,
    }
    cg.replica_set:start()
end)

g.test_sync_waits_for_all_connected = function(cg)
    cg.master:stop()
    cg.replica:exec(function(replication)
        local t = require('luatest')
        box.cfg{
            replication_connect_timeout = 0.01,
            replication = {},
        }
        box.cfg{
            replication = replication,
        }
        t.assert_equals(box.info.status, 'running', 'Replica is not an ' ..
                        'orphan. No connections mean no one to sync with')
    end, {cg.box_cfg.replication})

    t.tarantool.skip_if_not_debug()

    cg.replica:exec(function()
        box.cfg{
            replication = {},
        }
    end)
    cg.master:start()
    cg.master:exec(function()
        box.schema.space.create('test')
        box.space.test:create_index('pk')
        box.space.test:insert{1}
    end)
    cg.replica:exec(function(replication)
        local t = require('luatest')
        box.error.injection.set('ERRINJ_WAL_DELAY', true)
        box.cfg{
            replication_connect_timeout = 1000,
            replication = replication,
        }
        t.assert_equals(box.info.status, 'orphan', 'Replica is orphan until ' ..
                        'it syncs with the master')
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
        t.helpers.retrying({}, function()
            t.assert_equals(box.info.status, 'running', 'Replica is synced')
        end)
    end, {cg.box_cfg.replication})
    cg.replica:assert_follows_upstream(1)
end
