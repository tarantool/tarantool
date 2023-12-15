local fun = require('fun')
local t = require('luatest')
local treegen = require('test.treegen')
local justrun = require('test.justrun')
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
          roles: [sharding]
          password: "storage"

    iproto:
      listen:
        - uri: 'unix/:./{{ instance_name }}.iproto'
      advertise:
        sharding:
          login: 'storage'

    sharding:
      bucket_count: 1234
      sched_ref_quota: 258

    groups:
      group-001:
        replicasets:
          replicaset-001:
            sharding:
              roles: [storage]
            database:
              replicaset_uuid: '11111111-1111-1111-0011-111111111111'
            instances:
              instance-001:
                database:
                  mode: rw
              instance-002: {}
          replicaset-002:
            sharding:
              roles: [storage]
            instances:
              instance-003:
                database:
                  instance_uuid: '22222222-2222-2222-0022-222222222222'
                  mode: rw
              instance-004: {}
          replicaset-003:
            sharding:
              roles: [router]
            instances:
              instance-005:
                database:
                  mode: rw
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
        identification_mode = 'name_as_key',
        rebalancer_disbalance_threshold = 1,
        rebalancer_max_receiving = 100,
        rebalancer_max_sending = 1,
        rebalancer_mode = "auto",
        sched_move_quota = 1,
        sched_ref_quota = 258,
        schema_management_mode = "manual_access",
        shard_index = "bucket_id",
        sync_timeout = 1,
        sharding = {
            ["replicaset-001"] = {
                master = "auto",
                replicas = {
                    ["instance-001"] = {
                        uri = {
                            login = "storage",
                            password = "storage",
                            uri = "unix/:./instance-001.iproto",
                        },
                    },
                    ["instance-002"] = {
                        uri = {
                            login = "storage",
                            password = "storage",
                            uri = "unix/:./instance-002.iproto",
                        },
                    },
                },
                uuid = "11111111-1111-1111-0011-111111111111",
                weight = 1,
            },
            ["replicaset-002"] = {
                master = "auto",
                replicas = {
                    ["instance-003"] = {
                        uri = {
                            login = "storage",
                            password = "storage",
                            uri = "unix/:./instance-003.iproto",
                        },
                        uuid = "22222222-2222-2222-0022-222222222222",
                    },
                    ["instance-004"] = {
                        uri = {
                            login = "storage",
                            password = "storage",
                            uri = "unix/:./instance-004.iproto",
                        },
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
        t.assert_equals(res, {{1, 1}})
        res = g.server_4:eval([[return box.space.a:select()]])
        t.assert_equals(res, {{800, 800}})
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
        t.assert_equals(res, {{1, 1}, {2, 2}})
        res = g.server_3:eval([[return box.space.a:select()]])
        t.assert_equals(res, {{799, 799}, {800, 800}})
    end)
end

g.test_rebalancer_role = function(g)
    t.skip_if(not has_vshard, 'Module "vshard" is not available')
    local dir = treegen.prepare_directory(g, {}, {})
    local config = [[
    credentials:
      users:
        guest:
          roles: [super]
        storage:
          roles: [sharding]
          password: "storage"

    iproto:
      listen:
        - uri: 'unix/:./{{ instance_name }}.iproto'
      advertise:
        sharding:
          login: 'storage'

    groups:
      group-001:
        replicasets:
          replicaset-001:
            sharding:
              roles: [storage, rebalancer]
            instances:
              instance-001:
                database:
                  mode: rw
              instance-002: {}
          replicaset-002:
            sharding:
              roles: [storage]
            instances:
              instance-003:
                database:
                  mode: rw
              instance-004: {}
          replicaset-003:
            sharding:
              roles: [router]
            instances:
              instance-005: {}
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
        ["replicaset-001"] = {
            master = "auto",
            rebalancer = true,
            replicas = {
                ["instance-001"] = {
                    uri = {
                        login = "storage",
                        password = "storage",
                        uri = "unix/:./instance-001.iproto"
                    },
                },
                ["instance-002"] = {
                    uri = {
                        login = "storage",
                        password = "storage",
                        uri = "unix/:./instance-002.iproto"
                    },
                },
            },
            weight = 1,
        },
        ["replicaset-002"] = {
            master = "auto",
            replicas = {
                ["instance-003"] = {
                    uri = {
                        login = "storage",
                        password = "storage",
                        uri = "unix/:./instance-003.iproto"
                    },
                },
                ["instance-004"] = {
                    uri = {
                        login = "storage",
                        password = "storage",
                        uri = "unix/:./instance-004.iproto"
                    },
                },
            },
            weight = 1,
        },
    }

    -- Storages.
    local exec = 'return vshard.storage.internal.current_cfg'
    local res = g.server_1:eval(exec)
    t.assert_equals(res.sharding, exp)
    res = g.server_2:eval(exec)
    t.assert_equals(res.sharding, exp)
    res = g.server_3:eval(exec)
    t.assert_equals(res.sharding, exp)
    res = g.server_4:eval(exec)
    t.assert_equals(res.sharding, exp)

    -- Router.
    exec = 'return vshard.router.internal.static_router.current_cfg'
    res = g.server_5:eval(exec)
    t.assert_equals(res.sharding, exp)

    -- Check that rebalancer is master of replicaset-001.
    exec = 'return vshard.storage.internal.rebalancer_service ~= nil'
    t.assert_equals(g.server_1:eval(exec), true)
    t.assert_equals(g.server_2:eval(exec), false)
    t.assert_equals(g.server_3:eval(exec), false)
    t.assert_equals(g.server_4:eval(exec), false)
end

g.test_too_many_rebalancers = function(g)
    t.skip_if(not has_vshard, 'Module "vshard" is not available')
    local dir = treegen.prepare_directory(g, {}, {})
    local config = [[
    credentials:
      users:
        guest:
          roles: [super]
        storage:
          roles: [sharding]
          password: "storage"

    iproto:
      listen:
        - uri: 'unix/:./{{ instance_name }}.iproto'
      advertise:
        sharding:
          login: 'storage'

    groups:
      group-001:
        replicasets:
          replicaset-001:
            sharding:
              roles: [storage, rebalancer, router]
            instances:
              instance-001: {}
          replicaset-002:
            sharding:
              roles: [storage, rebalancer]
            instances:
              instance-002: {}
    ]]
    treegen.write_script(dir, 'config.yaml', config)
    local env = {LUA_PATH = os.environ()['LUA_PATH']}
    local opts = {nojson = true, stderr = true}
    local args = {'--name', 'instance-001', '--config', 'config.yaml'}
    local res = justrun.tarantool(dir, env, args, opts)
    t.assert_equals(res.exit_code, 1)
    local err = 'The rebalancer role must be present in no more than one ' ..
                'replicaset. Replicasets with the role:'
    t.assert_str_contains(res.stderr, err)
end

--
-- Make sure that if the vshard storage user is in credentials, it has the
-- credentials sharding role.
--
g.test_no_sharding_credentials_role_success = function(g)
    t.skip_if(not has_vshard, 'Module "vshard" is not available')
    helpers.success_case(g, {
        options = {
            ['credentials.users.storage.roles'] = {'sharding'},
            ['credentials.users.storage.password'] = 'storage',
            ['sharding.roles'] = {'storage', 'router'},
            ['iproto.advertise.sharding'] = {login = 'storage'},
        },
        verify = function() end,
    })

    helpers.success_case(g, {
        options = {
            ['credentials.roles.some_role.roles'] = {'sharding', 'super'},
            ['credentials.users.storage.roles'] = {'some_role'},
            ['credentials.users.storage.password'] = 'storage',
            ['sharding.roles'] = {'storage', 'router'},
            ['iproto.advertise.sharding'] = {login = 'storage'},
        },
        verify = function() end,
    })
end

g.test_no_sharding_credentials_role_error = function(g)
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
      listen:
      - uri: 'unix/:./{{ instance_name }}.iproto'
      advertise:
        sharding:
            login: 'storage'

    sharding:
        roles: [storage, router]

    groups:
      group-001:
        replicasets:
          replicaset-001:
            instances:
              instance-001: {}
    ]]
    treegen.write_script(dir, 'config.yaml', config)
    local opts = {nojson = true, stderr = true}
    local args = {'--name', 'instance-001', '--config', 'config.yaml'}
    local res = justrun.tarantool(dir, {}, args, opts)
    local exp_err = 'storage user "storage" should have "sharding" role'
    t.assert_equals(res.exit_code, 1)
    t.assert_str_contains(res.stderr, exp_err)
end

--
-- Make sure that if the vshard storage user is not specified in the
-- credentials, its credential sharding role is not checked.
--
g.test_no_storage_user = function(g)
    t.skip_if(not has_vshard, 'Module "vshard" is not available')
    helpers.success_case(g, {
        options = {
            ['sharding.roles'] = {'storage', 'router'},
            ['iproto.advertise.sharding'] = {
                login = 'storage',
                password = 'storage',
            },
        },
        verify = function() end,
    })
end

-- Make sure that the credential sharding role has all necessary credentials.
g.test_sharding_credentials_role = function(g)
    t.skip_if(not has_vshard, 'Module "vshard" is not available')
    local dir = treegen.prepare_directory(g, {}, {})
    local config = [[
    credentials:
      users:
        guest:
          roles: [super]
        storage:
          roles: [sharding]
          password: "storage"

    iproto:
      listen:
      - uri: 'unix/:./{{ instance_name }}.iproto'
      advertise:
        sharding:
          login: 'storage'

    sharding:
        roles: [storage, router]

    groups:
      group-001:
        replicasets:
          replicaset-001:
            instances:
              instance-001: {}
    ]]
    local config_file = treegen.write_script(dir, 'config.yaml', config)
    local opts = {
        env = {LUA_PATH = os.environ()['LUA_PATH']},
        config_file = config_file,
        alias = 'instance-001',
        chdir = dir,
    }
    g.server = server:new(opts)
    g.server:start()

    g.server:exec(function()
        local user = box.space._user.index.name:get('storage')
        local role = box.space._user.index.name:get('sharding')
        local repl_id = box.space._user.index.name:get('replication').id
        t.assert(role ~= nil)
        local vexports = require('vshard.storage.exports')
        local exports = vexports.compile(vexports.log[#vexports.log])

        -- The role should have the necessary privileges.
        t.assert(box.space._priv:get({role.id, 'role', repl_id}) ~= nil)
        t.assert(box.space._priv:get({user.id, 'role', role.id}) ~= nil)
        for name in pairs(exports.funcs) do
            local func = box.space._func.index.name:get(name)
            t.assert(box.space._priv:get({role.id, 'function', func.id}) ~= nil)
        end

        -- After reload(), the role should have the necessary privileges.
        local config = require('config')
        config:reload()
        t.assert(box.space._priv:get({role.id, 'role', repl_id}) ~= nil)
        t.assert(box.space._priv:get({user.id, 'role', role.id}) ~= nil)
        for name in pairs(exports.funcs) do
            local func = box.space._func.index.name:get(name)
            t.assert(box.space._priv:get({role.id, 'function', func.id}) ~= nil)
        end
    end)
end
