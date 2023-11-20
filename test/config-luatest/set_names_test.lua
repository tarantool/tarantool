local t = require('luatest')
local treegen = require('test.treegen')
local server = require('test.luatest_helpers.server')
local replica_set = require('luatest.replica_set')
local yaml = require('yaml')
local uuid = require('uuid')
local fun = require('fun')

local g = t.group('set-names-automatically')

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
    local config = {
        credentials = {
            users = {
                guest = {
                    roles = {'super'},
                },
            },
        },

        iproto = {
            listen = {{uri = 'unix/:./{{ instance_name }}.iproto'}},
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
                                database = {
                                    instance_uuid = g.uuids['instance-002']
                                },
                            },
                        },
                    },
                },
            },
        },
    }

    local cfg = yaml.encode(config)
    local dir = treegen.prepare_directory(g, {}, {})
    local config_file = treegen.write_script(dir, 'cfg.yaml', cfg)
    local opts = {config_file = config_file, chdir = dir}
    g.instance_1 = server:new(fun.chain(opts, {alias = 'instance-001'}):tomap())
    g.instance_2 = server:new(fun.chain(opts, {alias = 'instance-002'}):tomap())

    g.instance_1:start({wait_until_ready = false})
    g.instance_2:start({wait_until_ready = false})
    g.instance_1:wait_until_ready()
    g.instance_2:wait_until_ready()
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

local function check_box_cfg(rs_name, name)
    t.assert_equals(box.cfg.replicaset_name, rs_name)
    t.assert_equals(box.cfg.instance_name, name)
end

g.test_names_are_set = function(g)
    local rs_name = 'replicaset-001'
    g.instance_1:exec(check_names, {rs_name, 'instance-001', g.uuids})
    g.instance_2:exec(check_names, {rs_name, 'instance-002', g.uuids})

    g.instance_1:restart()
    g.instance_2:restart()

    -- After restart all names are passed to box.cfg.
    g.instance_1:exec(check_box_cfg, {rs_name, 'instance-001'})
    g.instance_2:exec(check_box_cfg, {rs_name, 'instance-002'})
end
