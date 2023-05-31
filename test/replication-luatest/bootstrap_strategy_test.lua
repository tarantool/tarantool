local t = require('luatest')
local server = require('luatest.server')
local replica_set = require('luatest.replica_set')
local fio = require('fio')
local fiber = require('fiber')
local socket = require('socket')

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
            server.build_listen_uri('server1', cg.replica_set.id),
            server.build_listen_uri('server2', cg.replica_set.id),
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
    cg.box_cfg.replication[3] = server.build_listen_uri('server3',
        cg.replica_set.id)
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
            server.build_listen_uri('server1', cg.replica_set.id),
            server.build_listen_uri('server2', cg.replica_set.id),
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
        cg.master.net_box_uri,
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
            bootstrap_leader = server.build_listen_uri('server1',
                cg.replica_set.id),
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
        t.assert_equals(box.info.status, 'running', 'The server is running')
    end)
end

g_config.before_test('test_name', function(cg)
    cg.replica_set = replica_set:new{}
    cg.server1 = cg.replica_set:build_and_add_server{
        alias = 'server1',
        box_cfg = {
            bootstrap_strategy = 'config',
            bootstrap_leader = 'server1name',
            instance_name = 'server1name',
            replication = nil,
        },
    }
end)

g_config.test_name = function(cg)
    cg.replica_set:start()
    t.helpers.retrying({}, cg.server1.exec, cg.server1, function()
        t.assert_equals(box.info.status, 'running', 'The server is running')
    end)
end

g_config.before_test('test_replication_without_bootstrap_leader', function(cg)
    cg.replica_set = replica_set:new{}
    cg.server1 = cg.replica_set:build_and_add_server{
        alias = 'server1',
        box_cfg = {
            replication_timeout = 0.1,
            bootstrap_strategy = 'config',
            bootstrap_leader = server.build_listen_uri('server1',
                cg.replica_set.id),
            replication = {
                server.build_listen_uri('server2', cg.replica_set.id),
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
            replication = server.build_listen_uri('server1', cg.replica_set.id),
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
            bootstrap_leader = server.build_listen_uri('server1',
                cg.replica_set.id),
            replication = server.build_listen_uri('server1', cg.replica_set.id),
        },
    }
end)

g_config.test_single_leader = function(cg)
    cg.replica_set:start()
    cg.server1:exec(function()
        t.assert_equals(box.info.status, 'running', 'server is working')
    end)
end

g_config.after_test('test_single_leader', function(cg)
    cg.server1:stop()
end)

local g_config_success = t.group('gh-7999-bootstrap-strategy-config-success', {
     {leader = 'server3'},
     {leader = uuid3},
     {leader = 'server3name'},
})

g_config_success.after_each(function(cg)
    cg.replica_set:drop()
end)

g_config_success.before_test('test_correct_bootstrap_leader', function(cg)
    cg.replica_set = replica_set:new{}
    cg.replica_set_a = replica_set:new{}
    cg.replica_set_b = replica_set:new{}
    local bootstrap_leader = cg.params.leader == 'server3' and
        server.build_listen_uri('server3', cg.replica_set_b.id) or
        cg.params.leader
    cg.server1 = cg.replica_set:build_and_add_server{
        alias = 'server1',
        box_cfg = {
            bootstrap_strategy = 'config',
            bootstrap_leader = bootstrap_leader,
            instance_uuid = uuid1,
            instance_name = 'server1name',
            replication = {
                server.build_listen_uri('server1', cg.replica_set.id),
                server.build_listen_uri('server2', cg.replica_set_a.id),
                server.build_listen_uri('server3', cg.replica_set_b.id),
            },
            replication_timeout = 0.1,
        },
    }
    cg.server2 = cg.replica_set_a:build_and_add_server{
        alias = 'server2',
        box_cfg = {
            replicaset_uuid = uuida,
            instance_uuid = uuid2,
            instance_name = 'server2name',
        }
    }
    cg.server3 = cg.replica_set_b:build_and_add_server{
        alias = 'server3',
        box_cfg = {
            replicaset_uuid = uuidb,
            instance_uuid = uuid3,
            instance_name = 'server3name',
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
        t.assert_equals(box.info.replicaset.uuid, uuid,
                        'Server bootstrapped from correct leader')
    end, {uuidb})
end

g_config_success.before_test('test_wait_only_for_leader', function(cg)
    cg.replica_set = replica_set:new{}
    local bootstrap_leader = cg.params.leader == 'server3' and
        server.build_listen_uri('server3', cg.replica_set.id) or
        cg.params.leader
    cg.server1 = cg.replica_set:build_and_add_server{
        alias = 'server1',
        box_cfg = {
            bootstrap_strategy = 'config',
            bootstrap_leader = bootstrap_leader,
            replication = {
                server.build_listen_uri('server1', cg.replica_set.id),
                server.build_listen_uri('unreachable_2'),
                server.build_listen_uri('server3', cg.replica_set.id),
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
            instance_name = 'server3name',
        },
    }
end)

g_config_success.test_wait_only_for_leader = function(cg)
    cg.replica_set:start{}
    t.helpers.retrying({}, cg.server1.exec, cg.server1, function(uuid)
        t.assert_equals(box.info.replicaset.uuid, uuid,
                        'Server boots as soon as sees the leader')
    end, {uuidb})
end

local g_config_fail = t.group('gh-7999-bootstrap-strategy-config-fail')

g_config_fail.before_each(function(cg)
    cg.replica_set = replica_set:new{}
end)

g_config_fail.after_each(function(cg)
    cg.replica_set:drop()
end)

g_config_fail.test_bad_uri_or_uuid = function(cg)
    local cfg_failure = cg.replica_set:build_and_add_server{
        alias = 'cfg_failure',
        box_cfg = {bootstrap_strategy = 'config'},
        env = {['TARANTOOL_RUN_BEFORE_BOX_CFG'] = set_log_before_cfg},
    }
    local bad_config = {
        {}, -- empty table.
        {'a'}, -- non-empty table.
        {3301},
        '1z345678-1234-1234-1234-12345678', -- not a UUID or a name.
    }
    local errmsg = "Incorrect value for option 'bootstrap_leader':"
    local logfile = fio.pathjoin(cfg_failure.workdir, 'cfg_failure.log')
    for _, leader in pairs(bad_config) do
        cfg_failure.box_cfg.bootstrap_leader = leader
        -- The server will be dropped by after_all.
        cfg_failure:start{wait_until_ready = false}
        t.helpers.retrying({}, function()
            t.assert(cfg_failure:grep_log(errmsg, nil, {filename = logfile}),
                     'Incorrect option')
            t.assert(cfg_failure:grep_log('fatal error, exiting the event loop',
                                          nil, {filename = logfile}),
                     'Fatal error')
        end)
    end
end

local g_supervised = t.group('gh-8509-bootstrap-strategy-supervised')

local server2_admin
local SOCKET_TIMEOUT = 5

local function make_bootstrap_leader_initial(sockname)
    local sock, err = socket.tcp_connect('unix/', sockname)
    t.assert_equals(err, nil, 'Connection successful')
    local greeting = sock:read(128, SOCKET_TIMEOUT)
    t.assert_str_contains(greeting, 'Tarantool', 'Connected to console')
    t.assert_str_contains(greeting, 'Lua console', 'Connected to console')
    sock:write('box.ctl.make_bootstrap_leader()\n', SOCKET_TIMEOUT)
    local response = sock:read(8, SOCKET_TIMEOUT)
    t.assert_equals(response, '---\n...\n', 'The call succeeded')
    sock:close()
end

g_supervised.after_each(function(cg)
    cg.replica_set:drop()
end)

g_supervised.before_test('test_bootstrap', function(cg)
    cg.replica_set = replica_set:new{}
    cg.box_cfg = {
        bootstrap_strategy = 'supervised',
        replication = {
            server.build_listen_uri('server1', cg.replica_set.id),
            server.build_listen_uri('server2', cg.replica_set.id),
        },
        replication_timeout = 0.1,
    }
    for i = 1, 2 do
        local alias = 'server' .. i
        cg[alias] = cg.replica_set:build_and_add_server{
            alias = alias,
            box_cfg = cg.box_cfg,
        }
    end
    cg.server1.box_cfg.instance_uuid = uuid1
    cg.server2.box_cfg.instance_uuid = uuid2
    server2_admin = fio.pathjoin(cg.server2.workdir, 'server2.admin')
    local run_before_cfg = string.format([[
        local console = require('console')
        console.listen('unix/:%s')
    ]], server2_admin)

    cg.server2.env.TARANTOOL_RUN_BEFORE_BOX_CFG = run_before_cfg
end)

g_supervised.test_bootstrap = function(cg)
    cg.replica_set:start{wait_until_ready = false}
    t.helpers.retrying({}, make_bootstrap_leader_initial, server2_admin)
    cg.server1:wait_until_ready()
    cg.server2:wait_until_ready()
    t.assert_equals(cg.server2:get_instance_id(), 1,
                    'Server 2 is the bootstrap leader');
    cg.server2:exec(function()
        local tup = box.space._schema:get{'bootstrap_leader_uuid'}
        t.assert(tup ~= nil, 'Bootstrap leader uuid is persisted')
        t.assert_equals(tup[2], box.info.uuid,
                        'Bootstrap leader uuid is correct')
    end)
    t.helpers.retrying({}, cg.server1.assert_follows_upstream, cg.server1, 1)

    cg.server3 = cg.replica_set:build_and_add_server{
        alias = 'server3',
        box_cfg = cg.box_cfg,
    }
    cg.server3:start()
    local query = string.format('bootstrapping replica from %s',
                                uuid2:gsub('%-', '%%-'))
    t.assert(cg.server3:grep_log(query), 'Server3 bootstrapped from server2')

    cg.server1:exec(function()
        box.ctl.make_bootstrap_leader()
        local tup = box.space._schema:get{'bootstrap_leader_uuid'}
        t.assert_equals(tup[2], box.info.uuid,
                        'Bootstrap leader is updated')
    end)
    cg.server4 = cg.replica_set:build_and_add_server{
        alias = 'server4',
        box_cfg = cg.box_cfg,
    }
    cg.server4:start()
    query = string.format('bootstrapping replica from %s',
                          uuid1:gsub('%-', '%%-'))
    t.assert(cg.server4:grep_log(query), 'Server4 bootstrapped from server1')

end

g_supervised.before_test('test_schema_triggers', function(cg)
    cg.replica_set = replica_set:new{}
    cg.server1 = cg.replica_set:build_and_add_server{alias = 'server1'}
end)

g_supervised.test_schema_triggers = function(cg)
    cg.replica_set:start{}
    cg.server1:exec(function()
        local uuid2 = '22222222-2222-2222-2222-222222222222'
        local uuid3 = '33333333-3333-3333-3333-333333333333'
        box.cfg{bootstrap_strategy = 'supervised'}
        box.ctl.make_bootstrap_leader()
        local old_tuple = box.space._schema:get{'bootstrap_leader_uuid'}
        local new_tuple = old_tuple:update{{'=', 2, 'not a uuid'}}
        t.assert_error_msg_contains('Invalid UUID', function()
            box.space._schema:replace(new_tuple)
        end)
        new_tuple = old_tuple:update{{'=', 3, 'not a timestamp'}}
        t.assert_error_msg_contains('expected unsigned, got string', function()
            box.space._schema:replace(new_tuple)
        end)
        new_tuple = old_tuple:update{{'=', 4, 'not a replica id'}}
        t.assert_error_msg_contains('expected unsigned, got string', function()
            box.space._schema:replace(new_tuple)
        end)
        new_tuple = old_tuple:update{{'-', 3, 1}}
        box.space._schema:replace(new_tuple)
        t.assert_equals(box.space._schema:get{'bootstrap_leader_uuid'},
                        old_tuple, 'Last write wins by timestamp - old tuple')
        new_tuple = old_tuple:update{{'-', 4, 1}}
        box.space._schema:replace(new_tuple)
        t.assert_equals(box.space._schema:get{'bootstrap_leader_uuid'},
                        old_tuple, 'Last write wins by replica id - old tuple')
        new_tuple = old_tuple:update{{'+', 3, 1}}
        box.space._schema:replace(new_tuple)
        t.assert_equals(box.space._schema:get{'bootstrap_leader_uuid'},
                        new_tuple, 'Last write wins by timestamp - new tuple')
        old_tuple = new_tuple
        new_tuple = old_tuple:update{{'+', 4, 1}}
        box.space._schema:replace(new_tuple)
        t.assert_equals(box.space._schema:get{'bootstrap_leader_uuid'},
                        new_tuple, 'Last write wins by replica id - new tuple')

        local ballot_uuid
        local watcher = box.watch('internal.ballot', function(_, ballot)
                local ballot_key = box.iproto.key.BALLOT
                local uuid_key = box.iproto.ballot_key.BOOTSTRAP_LEADER_UUID
                ballot_uuid = ballot[ballot_key][uuid_key]
        end)
        t.helpers.retrying({}, function()
            t.assert_equals(ballot_uuid, new_tuple[2],
                            'Ballot stores correct uuid')
        end)
        old_tuple = new_tuple
        new_tuple = old_tuple:update{{'=', 2, uuid2}, {'-', 3, 1}}
        box.space._schema:replace(new_tuple)
        t.assert_equals(ballot_uuid, old_tuple[2],
                        "Ballot isn't updated if the tuple is rejected")
        new_tuple = new_tuple:update{{'+', 3, 1}}
        box.space._schema:replace(new_tuple)
        t.helpers.retrying({}, function()
            t.assert_equals(ballot_uuid, new_tuple[2],
                            'Ballot updates the uuid')
        end)
        old_tuple = new_tuple
        new_tuple = new_tuple:update{{'=', 2, uuid3}}
        box.begin()
        box.space._schema:replace(new_tuple)
        local new_uuid = ballot_uuid
        box.commit()
        t.assert_equals(new_uuid, old_tuple[2],
                        "Ballot isn't updated before commit")
        t.helpers.retrying({}, function()
            t.assert_equals(ballot_uuid, new_tuple[2],
                            "Ballot is updated on commit")
        end)
        watcher:unregister()
    end)
end

local function assert_not_booted(server)
    local logfile = fio.pathjoin(server.workdir, server.alias .. '.log')
    t.helpers.retrying({}, function()
        t.assert_equals(server:grep_log('ready to accept requests', nil,
                                        {filename = logfile}), nil,
                        server.alias .. 'does not bootstrap')
    end)
end

g_supervised.before_test('test_wait_for_bootstrap_leader', function(cg)
    cg.replica_set = replica_set:new{}
    cg.box_cfg = {
        bootstrap_strategy = 'supervised',
        replication_timeout = 0.1,
        replication = {
            server.build_listen_uri('server1', cg.replica_set.id),
            server.build_listen_uri('server2', cg.replica_set.id),
        },
        replication_connect_timeout = 1000,
    }
    for i = 1, 2 do
        local alias = 'server' .. i
        cg[alias] = cg.replica_set:build_and_add_server{
            alias = alias,
            box_cfg = cg.box_cfg,
        }
    end
end)

local function wait_master_is_seen(replica, master, rs_id)
    local addr = server.build_listen_uri(master.alias, rs_id)
    local logfile = fio.pathjoin(replica.workdir, replica.alias .. '.log')
    local query = string.format('remote master .* at unix/:%s',
                                addr:gsub('%-', '%%-'))
    t.helpers.retrying({}, function()
        t.assert(replica:grep_log(query, nil, {filename = logfile}),
                 replica.alias .. ' sees ' .. addr)
    end)
end

g_supervised.test_wait_for_bootstrap_leader = function(cg)
    cg.replica_set:start{wait_until_ready = false}

    wait_master_is_seen(cg.server1, cg.server1, cg.replica_set.id)
    wait_master_is_seen(cg.server1, cg.server2, cg.replica_set.id)
    wait_master_is_seen(cg.server2, cg.server1, cg.replica_set.id)
    wait_master_is_seen(cg.server2, cg.server2, cg.replica_set.id)

    fiber.sleep(cg.box_cfg.replication_timeout)

    assert_not_booted(cg.server1)
    assert_not_booted(cg.server2)
end

g_supervised.before_test('test_no_bootstrap_without_replication', function(cg)
    cg.replica_set = replica_set:new{}
    cg.server1 = cg.replica_set:build_and_add_server{
        alias = 'server1',
        box_cfg = {
            bootstrap_strategy = 'supervised',
        },
    }
end)

g_supervised.test_no_bootstrap_without_replication = function(cg)
    cg.server1:start{wait_until_ready = false}
    local logfile = fio.pathjoin(cg.server1.workdir, 'server1.log')
    local query = "can't initialize storage"
    t.helpers.retrying({}, function()
        t.assert(cg.server1:grep_log(query, nil, {filename = logfile}),
                 'Server fails to boot')
    end)
    query = 'failed to connect to the bootstrap leader'
    t.assert(cg.server1:grep_log(query, nil, {filename = logfile}),
             'Bootstrap leader not found')
end
