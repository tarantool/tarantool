local t = require('luatest')
local treegen = require('test.treegen')
local server = require('test.luatest_helpers.server')
local replica_set = require('luatest.replica_set')
local yaml = require('yaml')
local uuid = require('uuid')
local fun = require('fun')
local os = require('os')

local g = t.group('set-names-automatically-on-reload')

--
-- Test case:
--  1. User forgets to set UUID for one replica, starts
--     the cluster on 3.0.0 using config module and manages
--     to get half of the nodes working (using connect_quorum, e.g.)
--  2. He notices, that one of the replica didn't start and require UUID.
--  3. He sets UUID, starts this replica and reloads config on all other
--     instances.
--  4. Test, that master notices newly added replica and set name to it.

local function initialize_xlogs(g, uuids)
    g.replica_set = replica_set:new({})
    local box_cfg = {
        replicaset_uuid = uuids['replicaset-001'],
        replication = {
            server.build_listen_uri('instance-001', g.replica_set.id),
            server.build_listen_uri('instance-002', g.replica_set.id),
        },
    }

    box_cfg.instance_uuid = uuids['instance-001']
    local instance_1 = g.replica_set:build_and_add_server(
        {alias = 'instance-001', box_cfg = box_cfg})

    box_cfg.instance_uuid = uuids['instance-002']
    local instance_2 = g.replica_set:build_and_add_server(
        {alias = 'instance-002', box_cfg = box_cfg})

    g.replica_set:start()
    g.replica_set:wait_for_fullmesh()
    for _, replica in ipairs(g.replica_set.servers) do
        replica:exec(function()
            box.snapshot()
        end)
    end

    g.replica_set:stop()
    return instance_1.workdir, instance_2.workdir
end

g.before_all(function(g)
    treegen.init(g)
    g.uuids = {
        ['replicaset-001'] = uuid.str(),
        ['instance-001']   = uuid.str(),
        ['instance-002']   = uuid.str(),
    }

    local workdir_1, workdir_2 = initialize_xlogs(g, g.uuids)
    g.config = {
        credentials = {
            users = {
                guest = {
                    roles = {'super'},
                },
            },
        },

        iproto = {
            listen = 'unix/:./{{ instance_name }}.iproto',
        },

        replication = {
            bootstrap_strategy = 'legacy',
        },

        log = {
            to = 'file',
        },

        groups = {
            ['group-001'] = {
                replicasets = {
                    ['replicaset-001'] = {
                        database = {
                            replicaset_uuid = g.uuids['replicaset-001']
                        },
                        instances = {
                            ['instance-001'] = {
                                snapshot = { dir = workdir_1, },
                                wal = { dir = workdir_1, },
                                database = {
                                    instance_uuid = g.uuids['instance-001'],
                                    mode = 'rw',
                                },
                            },
                            ['instance-002'] = {
                                snapshot = { dir = workdir_2, },
                                wal = { dir = workdir_2, },
                                -- No UUID is set
                            },
                        },
                    },
                },
            },
        },
    }

    os.setenv('TT_REPLICATION_CONNECT_QUORUM', '1')
    g.dir = treegen.prepare_directory(g, {}, {})
end)

g.after_all(function(g)
    g.replica_set:drop()
    g.instance_1:drop()
    g.instance_2:drop()
    treegen.clean(g)
end)

local function check_names(rs_name, name, names)
    t.helpers.retrying({timeout = 20}, function()
        local info = box.info
        t.assert_equals(info.replicaset.name, rs_name)
        t.assert_equals(info.name, name)

        t.assert_equals(box.cfg.replicaset_name, rs_name)
        t.assert_equals(box.cfg.instance_name, name)

        local alerts = require('config')._alerts
        for name, _ in pairs(names) do
            t.assert_equals(alerts[name], nil)
        end
    end)
end

g.test_names_are_set_after_reload = function(g)
    -- Start instance-001 with config, from which UUID is missing.
    local cfg = yaml.encode(g.config)
    local config_file = treegen.write_script(g.dir, 'cfg.yaml', cfg)
    local opts = {config_file = config_file, chdir = g.dir}
    g.instance_1 = server:new(fun.chain(opts, {alias = 'instance-001'}):tomap())

    -- Instance-001 successfully starts and sets name.
    -- Pretend if instance-002 failed to start.
    g.instance_1:start({wait_until_ready = false})
    g.instance_1:wait_until_ready()
    local rs_name = 'replicaset-001'
    g.instance_1:exec(check_names, {rs_name, 'instance-001', {
        ['instance-001'] = true,
        ['replicaset-001'] = true,
        -- instance-002 still has the alert.
    }})

    -- Instance-001 notices, that instance-002 doesn't have UUID set in config,
    -- but it succeeds to start.
    local log_postfix = '/var/log/instance-001/tarantool.log'
    local msg = 'box_cfg.apply: name %s for %s uuid is missing'
    g.instance_1:grep_log(msg:format('instance-001', g.uuids['instance-001']),
                          1024, {filename = g.dir .. log_postfix})
    local unknown = 'box_cfg.apply: instance %s is unknown'
    g.instance_1:grep_log(unknown:format('instance-002'),
                          1024, {filename = g.dir .. log_postfix})
    g.instance_1:grep_log(msg:format(rs_name, g.uuids[rs_name]),
                          1024, {filename = g.dir .. log_postfix})

    -- Set UUID, reload config on instance-001, start instance-002.
    local rs = g.config.groups['group-001'].replicasets['replicaset-001']
    rs.instances['instance-002'].database = {
        instance_uuid = g.uuids['instance-002']
    }

    -- Reload config on instance-001 and wait for the name to be assigned
    local cfg = yaml.encode(g.config)
    opts.config_file = treegen.write_script(g.dir, 'cfg.yaml', cfg)
    g.instance_1:exec(function(uuid)
        require('config'):reload()
        t.helpers.retrying({timeout = 20}, function()
            t.assert_equals(box.space._cluster.index.uuid:select(uuid)[1][3],
                            'instance-002')
        end)
    end, {g.uuids['instance-002']})

    g.instance_1:grep_log(msg:format('instance-002', g.uuids['instance-002']),
                          1024, {filename = g.dir .. log_postfix})

    -- Start instance-002
    g.instance_2 = server:new(fun.chain(opts, {alias = 'instance-002'}):tomap())
    g.instance_2:start({wait_until_ready = false})

    g.instance_2:wait_until_ready()
    g.instance_2:wait_for_vclock_of(g.instance_1)
    g.instance_2:exec(check_names, {rs_name, 'instance-002', g.uuids})
end
