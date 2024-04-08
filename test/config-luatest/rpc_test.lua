local t = require('luatest')
local fun = require('fun')
local treegen = require('test.treegen')
local server = require('test.luatest_helpers.server')
local helpers = require('test.config-luatest.helpers')

local g = helpers.group()

g.test_connect = function(g)
    local dir = treegen.prepare_directory(g, {}, {})
    local config = [[
    credentials:
      users:
        guest:
          roles: [super]
        myuser:
          password: "secret"
          roles: [replication]
          privileges:
          - permissions: [execute]
            universe: true

    iproto:
      listen:
        - uri: 'unix/:./{{ instance_name }}.iproto'
      advertise:
        peer:
          login: 'myuser'

    groups:
      group-001:
        replicasets:
          replicaset-001:
            instances:
              instance-001:
                database:
                  mode: rw
              instance-002: {}
          replicaset-002:
            instances:
              instance-003:
                database:
                  mode: rw
              instance-004: {}
          replicaset-003:
            instances:
              instance-005: {}
    ]]
    treegen.write_script(dir, 'config.yaml', config)

    local opts = {
        env = {LUA_PATH = os.environ()['LUA_PATH']},
        config_file = 'config.yaml',
        chdir = dir,
    }
    g.server_1 = server:new(fun.chain(opts, {alias = 'instance-001'}):tomap())
    g.server_2 = server:new(fun.chain(opts, {alias = 'instance-002'}):tomap())
    g.server_3 = server:new(fun.chain(opts, {alias = 'instance-003'}):tomap())
    g.server_4 = server:new(fun.chain(opts, {alias = 'instance-004'}):tomap())
    -- The instance-005 instance is not started to check if the correct error
    -- is returned.

    g.server_1:start({wait_until_ready = false})
    g.server_2:start({wait_until_ready = false})
    g.server_3:start({wait_until_ready = false})
    g.server_4:start({wait_until_ready = false})

    g.server_1:wait_until_ready()
    g.server_2:wait_until_ready()
    g.server_3:wait_until_ready()
    g.server_4:wait_until_ready()

    -- Make sure module pool is working.
    local function check_conn()
        local connpool = require('experimental.connpool')
        local conn1 = connpool.connect('instance-001')
        local conn2 = connpool.connect('instance-002')
        local conn3 = connpool.connect('instance-003')
        local conn4 = connpool.connect('instance-004')

        -- Make sure connections are active.
        t.assert_equals(conn1.state, 'active')
        t.assert_equals(conn2.state, 'active')
        t.assert_equals(conn3.state, 'active')
        t.assert_equals(conn4.state, 'active')

        -- Make sure connections are working.
        t.assert_equals(conn1:eval('return box.info.name'), 'instance-001')
        t.assert_equals(conn2:eval('return box.info.name'), 'instance-002')
        t.assert_equals(conn3:eval('return box.info.name'), 'instance-003')
        t.assert_equals(conn4:eval('return box.info.name'), 'instance-004')

        -- Make sure new connections are not created.
        t.assert(conn1 == connpool.connect('instance-001'))
        t.assert(conn2 == connpool.connect('instance-002'))
        t.assert(conn3 == connpool.connect('instance-003'))
        t.assert(conn4 == connpool.connect('instance-004'))

        local ok, err = pcall(connpool.connect, 'instance-005')
        t.assert_not(ok)
        t.assert(err:startswith('Unable to connect to instance "instance-005"'))
        t.assert(err:endswith('No such file or directory'))
    end

    g.server_1:exec(check_conn)
    g.server_2:exec(check_conn)
    g.server_3:exec(check_conn)
    g.server_4:exec(check_conn)
end

g.test_filter = function(g)
    local dir = treegen.prepare_directory(g, {}, {})
    local config = [[
    credentials:
      users:
        guest:
          roles: [super]

    iproto:
      listen:
        - uri: 'unix/:./{{ instance_name }}.iproto'

    groups:
      group-001:
        replicasets:
          replicaset-001:
            instances:
              instance-001:
                database:
                  mode: rw
                roles: [r1, r2]
                labels:
                  l1: 'one'
                  l2: 'two'
              instance-002:
                labels:
                  l1: 'one'
          replicaset-002:
            instances:
              instance-003:
                database:
                  mode: rw
                roles: [r1, r2]
              instance-004:
                labels:
                  l1: 'one_one'
                  l3: 'three'
                roles: [r1]
    ]]
    treegen.write_script(dir, 'config.yaml', config)

    local role = string.dump(function()
        return {
            stop = function() end,
            apply = function() end,
            validate = function() end,
        }
    end)
    treegen.write_script(dir, 'r1.lua', role)
    treegen.write_script(dir, 'r2.lua', role)

    local opts = {
        env = {LUA_PATH = os.environ()['LUA_PATH']},
        config_file = 'config.yaml',
        chdir = dir,
    }
    g.server_1 = server:new(fun.chain(opts, {alias = 'instance-001'}):tomap())
    g.server_2 = server:new(fun.chain(opts, {alias = 'instance-002'}):tomap())
    g.server_3 = server:new(fun.chain(opts, {alias = 'instance-003'}):tomap())
    g.server_4 = server:new(fun.chain(opts, {alias = 'instance-004'}):tomap())

    g.server_1:start({wait_until_ready = false})
    g.server_2:start({wait_until_ready = false})
    g.server_3:start({wait_until_ready = false})
    g.server_4:start({wait_until_ready = false})

    g.server_1:wait_until_ready()
    g.server_2:wait_until_ready()
    g.server_3:wait_until_ready()
    g.server_4:wait_until_ready()

    local function check()
        local connpool = require('experimental.connpool')
        local exp = {"instance-001", "instance-004", "instance-003"}
        local opts = {roles = {'r1'}}
        t.assert_items_equals(connpool.filter(opts), exp)

        exp = {"instance-001", "instance-003"}
        opts = {roles = {'r1', 'r2'}}
        t.assert_items_equals(connpool.filter(opts), exp)

        exp = {"instance-001", "instance-002"}
        opts = {labels = {l1 = 'one'}}
        t.assert_items_equals(connpool.filter(opts), exp)

        exp = {"instance-001"}
        opts = {labels = {l1 = 'one', l2 = 'two'}}
        t.assert_items_equals(connpool.filter(opts), exp)

        exp = {"instance-004"}
        opts = {labels = {l1 = 'one_one'}, roles = {'r1'}}
        t.assert_items_equals(connpool.filter(opts), exp)

        exp = {}
        opts = {labels = {l1 = 'one_one', l2 = 'two'}, roles = {'r1'}}
        t.assert_items_equals(connpool.filter(opts), exp)

        exp = {"instance-001", "instance-002", "instance-003", "instance-004"}
        t.assert_items_equals(connpool.filter(), exp)
    end

    g.server_1:exec(check)
    g.server_2:exec(check)
    g.server_3:exec(check)
    g.server_4:exec(check)
end

g.test_filter_mode = function(g)
    local dir = treegen.prepare_directory(g, {}, {})
    local config = [[
    credentials:
      users:
        guest:
          roles: [super]

    iproto:
      listen:
        - uri: 'unix/:./{{ instance_name }}.iproto'

    groups:
      group-001:
        replicasets:
          replicaset-001:
            instances:
              instance-001:
                database:
                  mode: rw
                labels:
                  l1: 'one'
              instance-002: {}
          replicaset-002:
            replication:
              failover: manual
            leader: 'instance-003'
            instances:
              instance-003: {}
              instance-004:
                labels:
                  l1: 'one'
          replicaset-003:
            instances:
              instance-005: {}
    ]]
    treegen.write_script(dir, 'config.yaml', config)

    local opts = {
        env = {LUA_PATH = os.environ()['LUA_PATH']},
        config_file = 'config.yaml',
        chdir = dir,
    }
    g.server_1 = server:new(fun.chain(opts, {alias = 'instance-001'}):tomap())
    g.server_2 = server:new(fun.chain(opts, {alias = 'instance-002'}):tomap())
    g.server_3 = server:new(fun.chain(opts, {alias = 'instance-003'}):tomap())
    g.server_4 = server:new(fun.chain(opts, {alias = 'instance-004'}):tomap())
    -- The instance-005 is not running because we want to make sure that it does
    -- not appear in any results filtered by mode.

    g.server_1:start({wait_until_ready = false})
    g.server_2:start({wait_until_ready = false})
    g.server_3:start({wait_until_ready = false})
    g.server_4:start({wait_until_ready = false})

    g.server_1:wait_until_ready()
    g.server_2:wait_until_ready()
    g.server_3:wait_until_ready()
    g.server_4:wait_until_ready()

    local function check()
        local connpool = require('experimental.connpool')
        local exp = {"instance-001", "instance-003"}
        local opts = {mode = 'rw'}
        t.assert_items_equals(connpool.filter(opts), exp)

        exp = {"instance-002", "instance-004"}
        opts = {mode = 'ro'}
        t.assert_items_equals(connpool.filter(opts), exp)

        exp = {"instance-001"}
        opts = {mode = 'rw', labels = {l1 = 'one'}}
        t.assert_items_equals(connpool.filter(opts), exp)

        exp = {"instance-004"}
        opts = {mode = 'ro', labels = {l1 = 'one'}}
        t.assert_items_equals(connpool.filter(opts), exp)

        exp = {}
        opts = {mode = 'ro', labels = {l1 = 'two'}}
        t.assert_items_equals(connpool.filter(opts), exp)

        local exp_err = 'Expected nil, "ro" or "rw", got "something"'
        opts = {mode = 'something'}
        t.assert_error_msg_equals(exp_err, connpool.filter, opts)
    end

    g.server_1:exec(check)
    g.server_2:exec(check)
    g.server_3:exec(check)
    g.server_4:exec(check)
end

g.test_call = function(g)
    local dir = treegen.prepare_directory(g, {}, {})
    local config = [[
    credentials:
      users:
        guest:
          roles: [super]

    iproto:
      listen:
        - uri: 'unix/:./{{ instance_name }}.iproto'

    groups:
      group-001:
        replicasets:
          replicaset-001:
            instances:
              instance-001:
                roles: [one]
                database:
                  mode: rw
                labels:
                  l1: 'first'
                  l2: 'second'
              instance-002: {}
          replicaset-002:
            instances:
              instance-003:
                roles: [one]
                database:
                  mode: rw
                labels:
                  l2: 'second'
              instance-004:
                roles: [one]
                labels:
                  l1: 'first'
                  l3: 'second'
    ]]
    treegen.write_script(dir, 'config.yaml', config)

    local role = string.dump(function()
        local function f1()
            return box.info.name
        end

        local function f2(a, b, c)
            return a + b * c
        end

        rawset(_G, 'f1', f1)
        rawset(_G, 'f2', f2)

        return {
            stop = function() end,
            apply = function() end,
            validate = function() end,
        }
    end)
    treegen.write_script(dir, 'one.lua', role)

    local opts = {
        env = {LUA_PATH = os.environ()['LUA_PATH']},
        config_file = 'config.yaml',
        chdir = dir,
    }
    g.server_1 = server:new(fun.chain(opts, {alias = 'instance-001'}):tomap())
    g.server_2 = server:new(fun.chain(opts, {alias = 'instance-002'}):tomap())
    g.server_3 = server:new(fun.chain(opts, {alias = 'instance-003'}):tomap())
    g.server_4 = server:new(fun.chain(opts, {alias = 'instance-004'}):tomap())

    g.server_1:start({wait_until_ready = false})
    g.server_2:start({wait_until_ready = false})
    g.server_3:start({wait_until_ready = false})
    g.server_4:start({wait_until_ready = false})

    g.server_1:wait_until_ready()
    g.server_2:wait_until_ready()
    g.server_3:wait_until_ready()
    g.server_4:wait_until_ready()

    -- Make sure module pool is working.
    local function check()
        local connpool = require('experimental.connpool')
        local opts = {
            labels = {l1 = 'first'},
            roles = {'one'},
        }
        local candidates = connpool.filter(opts)

        local is_candidate = false
        for _, candidate_name in ipairs(candidates) do
            is_candidate = is_candidate or candidate_name == box.info.name
        end

        t.assert(not is_candidate or box.info.name == 'instance-001' or
                 box.info.name == 'instance-004')

        if is_candidate then
            t.assert(opts.prefer_local == nil)
            t.assert_equals(connpool.call('f1', nil, opts), box.info.name)
            opts.prefer_local = true
            t.assert_equals(connpool.call('f1', nil, opts), box.info.name)
        else
            t.assert(opts.prefer_local == nil)
            t.assert_items_include(candidates, {connpool.call('f1', nil, opts)})
            opts.prefer_local = true
            t.assert_items_include(candidates, {connpool.call('f1', nil, opts)})
        end

        t.assert_equals(connpool.call('f2', {1,2,3}, {roles = {'one'}}), 7)
    end

    g.server_1:exec(check)
    g.server_2:exec(check)
    g.server_3:exec(check)
    g.server_4:exec(check)
end
