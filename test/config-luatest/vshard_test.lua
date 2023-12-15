local fun = require('fun')
local t = require('luatest')
local treegen = require('test.treegen')
local server = require('test.luatest_helpers.server')
local helpers = require('test.config-luatest.helpers')

local g = helpers.group()

local has_vshard = pcall(require, 'vshard')

g.test_fixed_masters = function(g)
    t.skip_if(not has_vshard, 'Module "vshard" is not available')
    local dir = treegen.prepare_directory(g, {}, {})
    local config = [[
    credentials:
      users:
        guest:
          roles: [super]
        storage:
          roles: [super]
          password: "storage"

    iproto:
      listen: 'unix/:./{{ instance_name }}.iproto'
      advertise:
        sharding: 'storage@'

    sharding:
      bucket_count: 1234
      sched_ref_quota: 258

    groups:
      group-001:
        replicasets:
          replicaset-001:
            database:
              replicaset_uuid: '11111111-1111-1111-0011-111111111111'
            instances:
              instance-001:
                database:
                  mode: rw
                sharding:
                  roles: [storage]
              instance-002:
                sharding:
                  roles: [storage]
          replicaset-002:
            instances:
              instance-003:
                database:
                  instance_uuid: '22222222-2222-2222-0022-222222222222'
                  mode: rw
                sharding:
                  roles: [storage]
              instance-004:
                sharding:
                  roles: [storage]
          replicaset-003:
            instances:
              instance-005:
                database:
                  mode: rw
                sharding:
                  roles: [router]
    ]]
    local config_file = treegen.write_script(dir, 'config.yaml', config)
    local opts = {
        env = {LUA_PATH = os.environ()['LUA_PATH']},
        config_file = config_file,
        chdir = dir,
    }
    g.server_1 = server:new(fun.chain(opts, {alias = 'instance-001'}):tomap())
    g.server_2 = server:new(fun.chain(opts, {alias = 'instance-002'}):tomap())
    g.server_3 = server:new(fun.chain(opts, {alias = 'instance-003'}):tomap())
    g.server_4 = server:new(fun.chain(opts, {alias = 'instance-004'}):tomap())
    g.server_5 = server:new(fun.chain(opts, {alias = 'instance-005'}):tomap())

    g.server_1:start({wait_until_ready = false})
    g.server_2:start({wait_until_ready = false})
    g.server_3:start({wait_until_ready = false})
    g.server_4:start({wait_until_ready = false})
    g.server_5:start({wait_until_ready = false})

    g.server_1:wait_until_ready()
    g.server_2:wait_until_ready()
    g.server_3:wait_until_ready()
    g.server_4:wait_until_ready()
    g.server_5:wait_until_ready()

    -- Check that cluster was created.
    local info = g.server_1:eval('return box.info')
    t.assert_equals(info.name, 'instance-001')
    t.assert_equals(info.replicaset.name, 'replicaset-001')

    info = g.server_2:eval('return box.info')
    t.assert_equals(info.name, 'instance-002')
    t.assert_equals(info.replicaset.name, 'replicaset-001')

    info = g.server_3:eval('return box.info')
    t.assert_equals(info.name, 'instance-003')
    t.assert_equals(info.replicaset.name, 'replicaset-002')

    info = g.server_4:eval('return box.info')
    t.assert_equals(info.name, 'instance-004')
    t.assert_equals(info.replicaset.name, 'replicaset-002')

    info = g.server_5:eval('return box.info')
    t.assert_equals(info.name, 'instance-005')
    t.assert_equals(info.replicaset.name, 'replicaset-003')

    t.assert_equals(g.server_1:eval('return box.info.ro'), false)
    t.assert_equals(g.server_2:eval('return box.info.ro'), true)
    t.assert_equals(g.server_3:eval('return box.info.ro'), false)
    t.assert_equals(g.server_4:eval('return box.info.ro'), true)
    t.assert_equals(g.server_5:eval('return box.info.ro'), false)

    -- Check vshard config on each instance.
    local exp = {
        box_cfg_mode = "manual",
        bucket_count = 1234,
        discovery_mode = "on",
        failover_ping_timeout = 5,
        rebalancer_disbalance_threshold = 1,
        rebalancer_max_receiving = 100,
        rebalancer_max_sending = 1,
        rebalancer_mode = "auto",
        sched_move_quota = 1,
        sched_ref_quota = 258,
        schema_management_mode = "auto",
        shard_index = "bucket_id",
        sync_timeout = 1,
        sharding = {
            ["11111111-1111-1111-0011-111111111111"] = {
                master = "auto",
                replicas = {
                    ["ef10b92d-9ae9-e7bb-004c-89d8fb468341"] = {
                        name = "instance-002",
                        uri = "storage:storage@unix/:./instance-002.iproto",
                    },
                    ["ffe08155-a26d-bd7c-0024-00ee6815a41c"] = {
                        name = "instance-001",
                        uri = "storage:storage@unix/:./instance-001.iproto",
                    },
                },
                weight = 1,
            },
            ["d1f75e70-6883-d7fe-0087-e582c9c67543"] = {
                master = "auto",
                replicas = {
                    ["22222222-2222-2222-0022-222222222222"] = {
                        name = "instance-003",
                        uri = "storage:storage@unix/:./instance-003.iproto",
                    },
                    ["50367d8e-488b-309b-001a-138a0c516772"] = {
                        name = "instance-004",
                        uri = "storage:storage@unix/:./instance-004.iproto"
                    },
                },
                weight = 1,
            },
        },
    }

    -- Storages.
    local exec = 'return vshard.storage.internal.current_cfg'
    local res = g.server_1:eval(exec)
    t.assert_equals(res, exp)
    res = g.server_2:eval(exec)
    t.assert_equals(res, exp)
    res = g.server_3:eval(exec)
    t.assert_equals(res, exp)
    res = g.server_4:eval(exec)
    t.assert_equals(res, exp)

    -- Router.
    exec = 'return vshard.router.internal.static_router.current_cfg'
    res = g.server_5:eval(exec)
    t.assert_equals(res, exp)

    -- Check that basic sharding works.
    exec = [[
        function put(v)
            box.space.a:insert({v.id, v.bucket_id})
            return true
        end

        function get(id)
            return box.space.a:get(id)
        end
    ]]
    g.server_1:eval(exec)
    g.server_2:eval(exec)
    g.server_3:eval(exec)
    g.server_4:eval(exec)

    exec = [[
        box.schema.func.create('put')
        box.schema.role.grant('public', 'execute', 'function', 'put')
        box.schema.func.create('get')
        box.schema.role.grant('public', 'execute', 'function', 'get')
        local format = {{'id', 'unsigned'}, {'bucket_id', 'unsigned'}}
        a = box.schema.space.create('a', {format = format})
        a:create_index('id', {parts = {'id'}})
        a:create_index('bucket_id', {parts = {'bucket_id'}, unique = false})
    ]]
    g.server_1:eval(exec)
    g.server_3:eval(exec)
    t.helpers.retrying({timeout = 60}, function()
        t.assert_equals(g.server_2:eval([[return box.space.a:select()]]), {})
        t.assert_equals(g.server_4:eval([[return box.space.a:select()]]), {})
    end)

    exec = [[
        vshard.router.bootstrap()
        vshard.router.call(1, 'write', 'put', {{id = 1, bucket_id = 1}})
        vshard.router.call(800, 'write', 'put', {{id = 800, bucket_id = 800}})
    ]]
    g.server_5:eval(exec)
    t.helpers.retrying({timeout = 60}, function()
        local res = g.server_2:eval([[return box.space.a:select()]])
        t.assert_equals(res, {{800, 800}})
        res = g.server_4:eval([[return box.space.a:select()]])
        t.assert_equals(res, {{1, 1}})
    end)

    -- Make sure that the new master is auto-discovered when master is changed.
    g.server_1:eval([[box.cfg{read_only = true}]])
    g.server_2:eval([[box.cfg{read_only = false}]])
    g.server_3:eval([[box.cfg{read_only = true}]])
    g.server_4:eval([[box.cfg{read_only = false}]])
    exec = [[
        vshard.router.call(2, 'write', 'put', {{id = 2, bucket_id = 2}},
                           {timeout = 30})
        vshard.router.call(799, 'write', 'put', {{id = 799, bucket_id = 799}},
                           {timeout = 30})
    ]]
    g.server_5:eval(exec)
    t.helpers.retrying({timeout = 60}, function()
        local res = g.server_1:eval([[return box.space.a:select()]])
        t.assert_equals(res, {{799, 799}, {800, 800}})
        res = g.server_3:eval([[return box.space.a:select()]])
        t.assert_equals(res, {{1, 1}, {2, 2}})
    end)
end
