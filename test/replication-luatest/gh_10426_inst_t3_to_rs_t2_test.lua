local t = require('luatest')
local fio = require('fio')
local yaml = require('yaml')
local fun = require('fun')
local treegen = require('luatest.treegen')
local justrun = require('luatest.justrun')
local server = require('luatest.server')

local g = t.group()

local function read_log_uuids(log_path)
    local fh = fio.open(log_path, {'O_RDONLY'})
    if fh == nil then
        return nil, nil
    end
    local log = fh:read()
    fh:close()

    local instance_uuid = log:match('instance uuid ([0-9a-f%-]+)')
    local replicaset_uuid = log:match('replicaset uuid ([0-9a-f%-]+)')

    return instance_uuid, replicaset_uuid
end

local function start_source_instance(dir)
    local workdir = fio.pathjoin(dir, 'i-001')
    local log_rel = 'var/lib/i-001/instance.log'
    local log_path = fio.pathjoin(workdir, log_rel)

    local socket_path = fio.pathjoin(workdir, 'i-001.iproto')
    local opts = {
        alias = 'i-001',
        workdir = workdir,
        chdir = workdir,
        net_box_uri = 'unix/:' .. socket_path,
        box_cfg = {
            listen = 'unix/:' .. socket_path,
            memtx_dir = 'var/lib/i-001',
            wal_dir = 'var/lib/i-001',
            log = log_rel,
        },
        env = {
            TARANTOOL_RUN_BEFORE_BOX_CFG =
                "require('fio').mktree('var/lib/i-001')",
        },
    }
    local instance = server:new(opts)
    instance:start()

    local instance_uuid
    local replicaset_uuid
    t.helpers.retrying({timeout = 20}, function()
        instance_uuid, replicaset_uuid = read_log_uuids(log_path)
        t.assert_not_equals(instance_uuid, nil)
        t.assert_not_equals(replicaset_uuid, nil)
    end)

    return instance, instance_uuid, replicaset_uuid, socket_path
end

local function build_config(dir, uuids, source_socket_path, schema)
    local data_dir_2 = fio.abspath(fio.pathjoin(dir, 'i-002/var/lib/i-002'))
    fio.mktree(data_dir_2)

    return {
        replication = {
            failover = 'manual',
        },
        groups = {
            ['g-001'] = {
                replicasets = {
                    ['r-001'] = {
                        database = {
                            replicaset_uuid = uuids.replicaset_uuid,
                            schema = schema,
                        },
                        leader = 'i-001',
                        instances = {
                            ['i-001'] = {
                                database = {
                                    instance_uuid = uuids.instance_uuid,
                                },
                                iproto = {
                                    listen = {
                                        {uri = 'unix/:' .. source_socket_path},
                                    },
                                },
                            },
                            ['i-002'] = {
                                iproto = {
                                    listen = {
                                        {uri = 'unix/:./i-002.iproto'},
                                    },
                                },
                            },
                        },
                    },
                },
            },
        },
    }
end

local function write_config(dir, config)
    return treegen.write_file(dir, 'config.yaml', yaml.encode(config))
end

local function start_replica(dir, config)
    local config_file = write_config(dir, config)
    local opts = {config_file = config_file, chdir = dir}
    local instance = server:new(fun.chain(opts, {alias = 'i-002'}):tomap())
    instance:start({wait_until_ready = false})
    instance:wait_until_ready()

    return instance
end

g.after_each(function(g)
    if g.instance_1 ~= nil then
        g.instance_1:drop()
        g.instance_1 = nil
    end
    if g.instance_2 ~= nil then
        g.instance_2:drop()
        g.instance_2 = nil
    end
end)

g.test_without_schema = function(g)
    local dir = treegen.prepare_directory({}, {})
    g.instance_1, g.instance_uuid, g.replicaset_uuid, g.source_socket_path =
        start_source_instance(dir)

    local config = build_config(dir, {
        instance_uuid = g.instance_uuid,
        replicaset_uuid = g.replicaset_uuid,
    }, g.source_socket_path)
    local config_file = write_config(dir, config)

    local args = {'--name', 'i-002', '--config', config_file}
    local res =
        justrun.tarantool(dir, {}, args, {nojson = true, stderr = true})
    t.assert_equals(res.exit_code, 1)
    t.assert_str_contains(res.stderr,
        "Replicaset name mismatch: name 'r-001' provided in config")
end

g.test_with_schema = function(g)
    local dir = treegen.prepare_directory({}, {})
    g.instance_1, g.instance_uuid, g.replicaset_uuid, g.source_socket_path =
        start_source_instance(dir)

    local config = build_config(dir, {
        instance_uuid = g.instance_uuid,
        replicaset_uuid = g.replicaset_uuid,
    }, g.source_socket_path, '2.11')

    g.instance_2 = start_replica(dir, config)

    g.instance_1:exec(function()
        t.helpers.retrying({timeout = 20}, function()
            t.assert(box.info.replication[2])
            t.assert_equals(box.info.replication[2].downstream.status,
                            'follow')
        end)
    end)
    g.instance_2:exec(function()
        t.helpers.retrying({timeout = 20}, function()
            t.assert(box.info.replication[1])
            t.assert_equals(box.info.replication[1].upstream.status,
                            'follow')
        end)
    end)
end
