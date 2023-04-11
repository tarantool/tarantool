local t = require('luatest')
local server = require('luatest.server')
local replica_set = require('luatest.replica_set')
local fio = require('fio')

local g_auto = t.group('gh-5272-bootstrap-strategy-auto')

local uuid1 = '11111111-1111-1111-1111-111111111111'
local uuid2 = '22222222-2222-2222-2222-222222222222'
local uuid3 = '33333333-3333-3333-3333-333333333333'
local uuida = 'aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa'
local uuidb = 'bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb'

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

g_auto.after_each(function(cg)
    cg.replica_set:drop()
end)

g_auto.before_test('test_auto_bootstrap_waits_for_confirmations', function(cg)
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

g_auto.test_auto_bootstrap_waits_for_confirmations = function(cg)
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

g_auto.before_test('test_join_checks_fullmesh', function(cg)
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

g_auto.test_join_checks_fullmesh = function(cg)
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
    cg.server3:drop()
end

g_auto.before_test('test_sync_waits_for_all_connected', function(cg)
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

g_auto.test_sync_waits_for_all_connected = function(cg)
    cg.master:stop()
    cg.replica:exec(function(replication)
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
        box.error.injection.set('ERRINJ_WAL_DELAY', true)
        box.cfg{
            replication_connect_timeout = 1000,
            replication_sync_timeout = 0.01,
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

local g_config = t.group('gh-7999-bootstrap-strategy-config')

g_config.after_each(function(cg)
    cg.replica_set:drop()
end)

g_config.before_test('test_no_replication', function(cg)
    cg.replica_set = replica_set:new{}
    cg.server1 = cg.replica_set:build_and_add_server{
        alias = 'server1',
        box_cfg = {
            replication_timeout = 0.1,
            bootstrap_strategy = 'config',
            bootstrap_leader = server.build_listen_uri('server1'),
            replication = nil
        },
    }
end)

local no_leader_msg = 'failed to connect to the bootstrap leader'

g_config.test_no_replication = function(cg)
    cg.replica_set:start{wait_until_ready = false}
    local logfile = fio.pathjoin(cg.server1.workdir, 'server1.log')
    t.helpers.retrying({}, function()
        t.assert(server:grep_log(no_leader_msg, nil, {filename = logfile}))
    end)
end

g_config.before_test('test_uuid', function(cg)
    cg.replica_set = replica_set:new{}
    cg.server1 = cg.replica_set:build_and_add_server{
        alias = 'server1',
        box_cfg = {
            bootstrap_strategy = 'config',
            bootstrap_leader = uuid1,
            instance_uuid = uuid1,
            replication = nil,
        },
    }
end)

g_config.test_uuid = function(cg)
    cg.replica_set:start()
    t.helpers.retrying({}, cg.server1.exec, cg.server1, function()
        local t = require('luatest')
        t.assert_equals(box.info.status, 'running', 'The server is running')
    end)
end

g_config.after_test('test_uuid', function(cg)
    cg.server1:stop()
end)

g_config.before_test('test_replication_without_bootstrap_leader', function(cg)
    cg.replica_set = replica_set:new{}
    cg.server1 = cg.replica_set:build_and_add_server{
        alias = 'server1',
        box_cfg = {
            replication_timeout = 0.1,
            bootstrap_strategy = 'config',
            bootstrap_leader = server.build_listen_uri('server1'),
            replication = {
                server.build_listen_uri('server2'),
            },
        },
    }
    cg.server2 = cg.replica_set:build_and_add_server{
        alias = 'server2',
        box_cfg = {
            replication_timeout = 0.1,
        },
    }
end)

g_config.test_replication_without_bootstrap_leader = function(cg)
    cg.replica_set:start{wait_until_ready = false}
    local logfile = fio.pathjoin(cg.server1.workdir, 'server1.log')
    t.helpers.retrying({}, function()
        t.assert(server:grep_log(no_leader_msg, nil, {filename = logfile}))
    end)
end

g_config.after_test('test_replication_without_bootstrap_leader', function(cg)
    -- Server1 dies and doesn't need to be stopped.
    cg.server2:stop()
end)

local set_log_before_cfg = [[
    local logfile = require('fio').pathjoin(
        os.getenv('TARANTOOL_WORKDIR'),
        os.getenv('TARANTOOL_ALIAS') .. '.log'
    )
    require('log').cfg{log = logfile}
]]

g_config.before_test('test_no_leader', function(cg)
    cg.replica_set = replica_set:new{}
    cg.server1 = cg.replica_set:build_and_add_server{
        alias = 'server1',
        box_cfg = {
            replication_timeout = 0.1,
            bootstrap_strategy = 'config',
            bootstrap_leader = nil,
            replication = server.build_listen_uri('server1'),
        },
        env = {
            ['TARANTOOL_RUN_BEFORE_BOX_CFG'] = set_log_before_cfg,
        },
    }
end)

g_config.test_no_leader = function(cg)
    cg.replica_set:start{wait_until_ready = false}
    local logfile = fio.pathjoin(cg.server1.workdir, 'server1.log')
    local empty_leader_msg = "the option can't be empty when bootstrap " ..
                             "strategy is 'config'"
    t.helpers.retrying({}, function()
        t.assert(server:grep_log(empty_leader_msg, nil, {filename = logfile}))
    end)
end

g_config.before_test('test_single_leader', function(cg)
    cg.replica_set = replica_set:new{}
    cg.server1 = cg.replica_set:build_and_add_server{
        alias = 'server1',
        box_cfg = {
            replication_timeout = 0.1,
            bootstrap_strategy = 'config',
            bootstrap_leader = server.build_listen_uri('server1'),
            replication = server.build_listen_uri('server1'),
        },
    }
end)

g_config.test_single_leader = function(cg)
    cg.replica_set:start()
    cg.server1:exec(function()
        local t = require('luatest')
        t.assert_equals(box.info.status, 'running', 'server is working')
    end)
end

g_config.after_test('test_single_leader', function(cg)
    cg.server1:stop()
end)

local g_config_success = t.group('gh-7999-bootstrap-strategy-config-success', {
     {leader = 'server3'},
     {leader = uuid3},
})

g_config_success.before_each(function(cg)
    cg.leader = cg.params.leader
    -- cg.params can't have "/" for some reason, so recreate the path here.
    if string.match(cg.leader, 'server3') then
        cg.leader = server.build_listen_uri(cg.leader)
    end
end)

g_config_success.after_each(function(cg)
    cg.replica_set:drop()
end)

g_config_success.before_test('test_correct_bootstrap_leader', function(cg)
    cg.replica_set = replica_set:new{}
    cg.server1 = cg.replica_set:build_and_add_server{
        alias = 'server1',
        box_cfg = {
            bootstrap_strategy = 'config',
            bootstrap_leader = cg.leader,
            instance_uuid = uuid1,
            replication = {
                server.build_listen_uri('server1'),
                server.build_listen_uri('server2'),
                server.build_listen_uri('server3'),
            },
            replication_timeout = 0.1,
        },
    }
    cg.replica_set_a = replica_set:new{}
    cg.server2 = cg.replica_set_a:build_and_add_server{
        alias = 'server2',
        box_cfg = {
            replicaset_uuid = uuida,
            instance_uuid = uuid2,
        }
    }
    cg.replica_set_b = replica_set:new{}
    cg.server3 = cg.replica_set_b:build_and_add_server{
        alias = 'server3',
        box_cfg = {
            replicaset_uuid = uuidb,
            instance_uuid = uuid3,
            listen = {
                server.build_listen_uri('server3'),
            },
        },
    }
end)

g_config_success.after_test('test_correct_bootstrap_leader', function(cg)
    cg.replica_set_a:drop()
    cg.replica_set_b:drop()
end)

g_config_success.test_correct_bootstrap_leader = function(cg)
    cg.replica_set_a:start{}
    cg.replica_set_b:start{}
    cg.replica_set:start{}
    t.helpers.retrying({}, cg.server1.exec, cg.server1, function(uuid)
        local t = require('luatest')
        t.assert_equals(box.info.cluster.uuid, uuid,
                        'Server bootstrapped from correct leader')
    end, {uuidb})
end

g_config_success.before_test('test_wait_only_for_leader', function(cg)
    cg.replica_set = replica_set:new{}
    cg.server1 = cg.replica_set:build_and_add_server{
        alias = 'server1',
        box_cfg = {
            bootstrap_strategy = 'config',
            bootstrap_leader = cg.leader,
            replication = {
                server.build_listen_uri('server1'),
                server.build_listen_uri('unreachable_2'),
                server.build_listen_uri('server3'),
                server.build_listen_uri('unreachable_4'),
            },
            replication_connect_timeout = 1000,
            replication_timeout = 0.1,
        },
    }
    cg.server3 = cg.replica_set:build_and_add_server{
        alias = 'server3',
        box_cfg = {
            replicaset_uuid = uuidb,
            instance_uuid = uuid3,
            listen = {
                server.build_listen_uri('server3'),
            },
        },
    }
end)

g_config_success.test_wait_only_for_leader = function(cg)
    cg.replica_set:start{}
    t.helpers.retrying({}, cg.server1.exec, cg.server1, function(uuid)
        local t = require('luatest')
        t.assert_equals(box.info.cluster.uuid, uuid,
                        'Server boots as soon as sees the leader')
    end, {uuidb})
end

local g_config_fail = t.group('gh-7999-bootstrap-strategy-config-fail')

g_config_fail.test_bad_uri_or_uuid = function()
    local bad_config = {
        {}, -- empty table.
        {'a'}, -- non-empty table.
        {3301},
        'abracadabra', -- neither a URI or a UUID.
        'z2345678-1234-1234-1234-12345678', -- not a UUID.
    }
    local errmsg = "Incorrect value for option 'bootstrap_leader':"
    for _, leader in pairs(bad_config) do
        t.assert_error_msg_contains(errmsg, box.cfg, {
            bootstrap_strategy = 'config',
            bootstrap_leader = leader
        })
    end
end
