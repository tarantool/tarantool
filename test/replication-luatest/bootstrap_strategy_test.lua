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

local function grep_log(server, query)
    local logfile = fio.pathjoin(server.workdir, server.alias .. '.log')
    local opts = {filename = logfile}
    return server:grep_log(query, nil, opts)
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

g_config.before_test('test_dynamic_leader_cfg', function(cg)
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

g_config.test_dynamic_leader_cfg = function(cg)
    cg.replica_set:start()
    cg.server1:exec(function()
        t.assert_equals(box.info.status, 'running', 'server is working')
        local other_name = 'other'
        t.assert_not_equals(box.cfg.bootstrap_leader, other_name)
        box.cfg{
            bootstrap_leader = other_name,
        }
        t.assert_equals(box.cfg.bootstrap_leader, other_name,
                        'bootstrap leader is dynamic')
        box.cfg{
            bootstrap_strategy = 'auto',
        }
        local errmsg = "the option takes no effect when bootstrap " ..
                       "strategy is not 'config'"
        t.assert_error_msg_contains(errmsg, box.cfg,
                                    {bootstrap_leader = 'smth'})
        box.cfg{
            bootstrap_leader = '',
        }
        t.assert_equals(box.cfg.bootstrap_leader, nil, 'can change to nil')
    end)
end

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

local function make_bootstrap_leader_initial(sockname, opts)
    opts = opts or {}
    local command
    if opts.graceful then
        command = 'box.ctl.make_bootstrap_leader({graceful = true})'
    else
        command = 'box.ctl.make_bootstrap_leader()'
    end

    local sock, err = socket.tcp_connect('unix/', sockname)
    t.assert_equals(err, nil, 'Connection successful')
    local greeting = sock:read(128, SOCKET_TIMEOUT)
    t.assert_str_contains(greeting, 'Tarantool', 'Connected to console')
    t.assert_str_contains(greeting, 'Lua console', 'Connected to console')
    sock:write(command .. '\n', SOCKET_TIMEOUT)
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

-- If the given instance is not chosen as the bootstrap leader
-- before the first box.cfg() call and there is no replication
-- upstreams, there are no ways to load the database.
--
-- A startup failure is expected in this scenario.
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

g_supervised.before_test('test_early_leader_singleton', function(cg)
    cg.replica_set = replica_set:new{}
    cg.server1 = cg.replica_set:build_and_add_server{
        alias = 'server1',
        box_cfg = {
            bootstrap_strategy = 'supervised',
            -- NB: The test case also verifies that a instance
            -- successfully bootstraps the database if no
            -- upstreams are configured.
        },
        env = {
            ['TARANTOOL_RUN_BEFORE_BOX_CFG'] =
                'box.ctl.make_bootstrap_leader()',
        },
    }
end)

-- If the given instance is chosen as a bootstrap leader before
-- the first box.cfg() call, it is expected to successfully
-- bootstrap the database even if there are no upstreams
-- configured.
--
-- This logic is added in gh-10858.
g_supervised.test_early_leader_singleton = function(cg)
    cg.server1:start()

    t.assert_equals(cg.server1:get_instance_id(), 1,
                    'Server 1 is the bootstrap leader')

    cg.server1:exec(function()
        local tup = box.space._schema:get{'bootstrap_leader_uuid'}
        t.assert(tup ~= nil, 'Bootstrap leader uuid is persisted')
        t.assert_equals(tup[2], box.info.uuid,
                        'Bootstrap leader uuid is correct')
    end)
end

g_supervised.before_test('test_early_leader_with_replica', function(cg)
    cg.replica_set = replica_set:new{}
    cg.box_cfg = {
        bootstrap_strategy = 'supervised',
        replication = {
            server.build_listen_uri('server1', cg.replica_set.id),
            server.build_listen_uri('server2', cg.replica_set.id),
            -- Don't start this third server to verify that once
            -- a bootstrap leader is found, a replica doesn't wait
            -- anything else to register in the replicaset.
            server.build_listen_uri('server3', cg.replica_set.id),
        },
        replication_timeout = 0.1,
        -- This way we verify that when the replica connects to
        -- the bootstrap leader, it correctly determines that it
        -- is the bootstrap leader and doesn't wait anything else.
        replication_connect_timeout = 1000,
    }
    for i = 1, 2 do
        local alias = 'server' .. i
        cg[alias] = cg.replica_set:build_and_add_server{
            alias = alias,
            box_cfg = cg.box_cfg,
        }
    end

    cg.server2.env.TARANTOOL_RUN_BEFORE_BOX_CFG =
        'box.ctl.make_bootstrap_leader()'
end)

-- If the given instance is chosen as a bootstrap leader before
-- the first box.cfg() call, it is expected that it is
-- successfully bootstrapped and bootstraps a replica.
--
-- This logic is added in gh-10858.
g_supervised.test_early_leader_with_replica = function(cg)
    cg.replica_set:start()
    t.assert_equals(cg.server2:get_instance_id(), 1,
                    'Server 2 is the bootstrap leader')
    local leader_uuid = cg.server2:exec(function()
        local tup = box.space._schema:get{'bootstrap_leader_uuid'}
        t.assert(tup ~= nil, 'Bootstrap leader uuid is persisted')
        t.assert_equals(tup[2], box.info.uuid,
                        'Bootstrap leader uuid is correct')
        return box.info.uuid
    end)
    t.helpers.retrying({}, cg.server1.assert_follows_upstream, cg.server1, 1)

    -- Verify a log message on a bootstrap leader.
    local exp_msg = 'instance [0-9a-f-]+ is assigned as a bootstrap leader'
    local found = grep_log(cg.server2, exp_msg)
    t.assert(found)
    t.assert_str_contains(found, leader_uuid)

    -- Verify a log message on a replica.
    local exp_msg = 'instance [0-9a-f-]+ is assigned as a bootstrap leader'
    local found = grep_log(cg.server1, exp_msg)
    t.assert(found)
    t.assert_str_contains(found, leader_uuid)
end

g_supervised.before_test('test_early_leader_several_leaders', function(cg)
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
            -- Both instances advertise itself as bootstrap
            -- leaders.
            env = {
                ['TARANTOOL_RUN_BEFORE_BOX_CFG'] =
                    'box.ctl.make_bootstrap_leader()',
            },
        }
    end
end)

-- For the sake of completeness verify behavior in the incorrect
-- situation: two instances within the same replicaset are
-- assigned as bootstrap leaders.
g_supervised.test_early_leader_several_leaders = function(cg)
    cg.replica_set:start({wait_until_ready = false})
    local query = 'ER_REPLICASET_UUID_MISMATCH'
    t.helpers.retrying({}, function()
        local found = grep_log(cg.server1, query) or
                      grep_log(cg.server2, query)
        t.assert(found, ('Found %s'):format(query))
    end)
end

local g_graceful_supervised = t.group('graceful-bootstrap-strategy-supervised')

g_graceful_supervised.before_test('test_singleton', function(cg)
    cg.replica_set = replica_set:new{}
    cg.server1 = cg.replica_set:build_and_add_server{
        alias = 'server1',
        box_cfg = {
            bootstrap_strategy = 'supervised',
        },
        env = {
            ['TARANTOOL_RUN_BEFORE_BOX_CFG'] =
                'box.ctl.make_bootstrap_leader({graceful = true})',
        },
    }
end)

-- Bootstraps immediately if there are no upstreams to wait.
g_graceful_supervised.test_singleton = function(cg)
    cg.server1:start()
    t.assert_equals(cg.server1:get_instance_id(), 1,
                    'Server 1 is the bootstrap leader')
end

g_graceful_supervised.before_test('test_no_peer', function(cg)
    cg.replica_set = replica_set:new{}
    cg.box_cfg = {
        bootstrap_strategy = 'supervised',
        replication = {
            server.build_listen_uri('server1', cg.replica_set.id),
            -- Don't start server2.
            server.build_listen_uri('server2', cg.replica_set.id),
        },
        replication_timeout = 0.1,
        -- Wait for server2 for a long time.
        replication_connect_timeout = 1000,
    }
    for i = 1, 2 do
        local alias = 'server' .. i
        cg[alias] = cg.replica_set:build_and_add_server{
            alias = alias,
            box_cfg = cg.box_cfg,
        }
    end

    cg.server1.env.TARANTOOL_RUN_BEFORE_BOX_CFG =
        'box.ctl.make_bootstrap_leader({graceful = true})'
end)

-- Waits for other peers.
g_graceful_supervised.test_no_peer = function(cg)
    cg.server1:start({wait_until_ready = false})
    fiber.sleep(1)
    t.assert_error(cg.server1.connect_net_box, cg.server1)
end

g_graceful_supervised.before_test('test_no_leader', function(cg)
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

    cg.server1.env.TARANTOOL_RUN_BEFORE_BOX_CFG =
        'box.ctl.make_bootstrap_leader({graceful = true})'
end)

-- All connected, no leader => let's bootstrap.
--
-- Plus verify several other cases.
g_graceful_supervised.test_no_leader = function(cg)
    cg.replica_set:start()
    t.assert_equals(cg.server1:get_instance_id(), 1,
                    'Server 1 is the bootstrap leader')
    local uuid_1 = cg.server1:call('box.info').uuid

    -- Verify that a message about the reason why this instance
    -- is a bootstrap leader is issued.
    local exp_msg = 'Graceful bootstrap request succeeded: no bootstrap ' ..
        'leader is found in connected peers, so the current instance ' ..
        'proceeds as a bootstrap leader'
    local found = grep_log(cg.server1, exp_msg)
    t.assert(found)

    -- Also, verify that when the database is bootstrapped the
    -- graceful bootstrap request is discarded.
    --
    -- Try it on a replica (server2).
    cg.server2:exec(function()
        box.ctl.make_bootstrap_leader({graceful = true})
    end)
    local exp_msg = 'Graceful bootstrap request is discarded: the ' ..
        'instance is already bootstrapped'
    t.helpers.retrying({}, function()
        local found = grep_log(cg.server2, exp_msg)
        t.assert(found)
    end)
    cg.server2:exec(function(uuid_1)
        local tup = box.space._schema:get{'bootstrap_leader_uuid'}
        t.assert(tup ~= nil)
        t.assert_equals(tup[2], uuid_1)
    end, {uuid_1})

    -- And verify that if {graceful = false} is passed, it
    -- actually means the non-graceful bootstrap leader
    -- assignment.
    cg.server2:exec(function()
        box.ctl.make_bootstrap_leader({graceful = false})

        -- Bootstrap leader is updated. This wouldn't occur with
        -- the graceful bootstrap request.
        local tup = box.space._schema:get{'bootstrap_leader_uuid'}
        t.assert(tup ~= nil)
        t.assert_equals(tup[2], box.info.uuid)
    end)

    -- Verify incorrect argument.
    cg.server2:exec(function()
        local exp_err_msg = 'box.ctl.make_bootstrap_leader() expects a ' ..
            'table as the first argument, got boolean'
        t.assert_error_msg_equals(exp_err_msg, function()
            box.ctl.make_bootstrap_leader(true)
        end)
    end)
end

g_graceful_supervised.before_test('test_other_leader', function(cg)
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

    cg.server1.env.TARANTOOL_RUN_BEFORE_BOX_CFG =
        'box.ctl.make_bootstrap_leader({graceful = true})'
    cg.server2.env.TARANTOOL_RUN_BEFORE_BOX_CFG =
        'box.ctl.make_bootstrap_leader({graceful = false})'
end)

-- If some of the peers is already a leader, act as a regular
-- replica.
g_graceful_supervised.test_other_leader = function(cg)
    cg.server2:start()
    t.assert_equals(cg.server2:get_instance_id(), 1)
    cg.server1:start()
    t.assert_equals(cg.server1:get_instance_id(), 2)

    -- Verify that the message about the graceful bootstrap
    -- request failure is issued.
    local exp_msg = 'Graceful bootstrap request failed: other bootstrap ' ..
        'leader is found within connected peers, proceed as a regular replica'
    t.helpers.retrying({}, function()
        local found = grep_log(cg.server1, exp_msg)
        t.assert(found)
    end)
end

g_graceful_supervised.before_test('test_after_box_cfg', function(cg)
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
        local box_cfg = table.copy(cg.box_cfg)
        box_cfg.instance_uuid = ({uuid1, uuid2})[i]
        cg[alias] = cg.replica_set:build_and_add_server{
            alias = alias,
            box_cfg = box_cfg,
        }
    end

    cg.server1_admin = fio.pathjoin(cg.server1.workdir, 'server1.admin')
    local run_before_cfg = string.format([[
        local console = require('console')
        console.listen('unix/:%s')
    ]], cg.server1_admin)

    cg.server1.env.TARANTOOL_RUN_BEFORE_BOX_CFG = run_before_cfg
end)

-- Run box.cfg() first, wait until server1 connects to all the
-- peers and then issue the graceful bootstrap request.
--
-- Similar to test_no_leader, but the graceful bootstrap request
-- is issued after box.cfg() (and after all the peers are
-- connected).
--
-- This way we verify that the graceful bootstrap request wakes up
-- replicaset_connect(), where we wait for new connected peers, a
-- new bootstrap leader, a change in the local supervised
-- bootstrap flags (set by box.cfg.make_bootstrap_leader(<...>)).
g_graceful_supervised.test_after_box_cfg = function(cg)
    -- Start both instances, run box.cfg() on them.
    cg.replica_set:start({wait_until_ready = false})

    -- Wait until server1 connects to itself and server2.
    t.helpers.retrying({}, function()
        local found_1 = grep_log(cg.server1, 'remote master 11111111')
        local found_2 = grep_log(cg.server1, 'remote master 22222222')
        t.assert(found_1 ~= nil and found_2 ~= nil)
    end)

    -- Run box.cfg.make_bootstrap_leader({graceful = true}).
    t.helpers.retrying({}, function()
        make_bootstrap_leader_initial(cg.server1_admin, {graceful = true})
    end)

    -- Now the database should be bootstrapped.
    cg.server1:wait_until_ready()
    cg.server2:wait_until_ready()

    -- Verify that server1 was the bootstrap leader.
    t.assert_equals(cg.server1:get_instance_id(), 1)

    -- Verify that a message about the reason why this instance
    -- is a bootstrap leader is issued.
    local exp_msg = 'Graceful bootstrap request succeeded: no bootstrap ' ..
        'leader is found in connected peers, so the current instance ' ..
        'proceeds as a bootstrap leader'
    local found = grep_log(cg.server1, exp_msg)
    t.assert(found)
end
