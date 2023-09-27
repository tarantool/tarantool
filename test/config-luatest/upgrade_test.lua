local t = require('luatest')
local server = require('test.luatest_helpers.server')
local treegen = require('test.treegen')

local g = t.group('config_upgrade_test')

g.before_all(function()
    treegen.init(g)
    local dir = treegen.prepare_directory(g, {}, {})
    local config = [[
    credentials:
      users:
        guest:
          roles: [super]
    iproto:
      listen: 'unix/:./{{ instance_name }}.iproto'
    wal:
      dir: %s
    snapshot:
      dir: %s
    groups:
      group-001:
        replicasets:
          replicaset-001:
            database:
              replicaset_uuid: 'cbf06940-0790-498b-948d-042b62cf3d29'
            instances:
              instance-001:
                database:
                  mode: rw
                  instance_uuid: '8a274925-a26d-47fc-9e1b-af88ce939412'
              instance-002:
                database:
                  mode: ro
                  instance_uuid: '3de2e3e1-9ebe-4d0d-abb1-26d301b84633'
    ]]

    local datadir_prefix = 'test/box-luatest/upgrade/2.11.0/replicaset/'
    -- Create dirs to generate config
    g.server_1 = server:new({
        -- Requires non-empty config
        chdir = dir,
        config_file = '',
        alias = 'instance-001',
        datadir = datadir_prefix .. 'instance-001',
    })
    g.server_2 = server:new({
        chdir = dir,
        config_file = '',
        alias = 'instance-002',
        datadir = datadir_prefix .. 'instance-002',
    })

    local config_1 = string.format(config, g.server_1.workdir,
                                   g.server_1.workdir, g.server_1.workdir)
    local config_2 = string.format(config, g.server_2.workdir,
                                   g.server_2.workdir, g.server_2.workdir)

    local config_file_1 = treegen.write_script(dir, 'config_1.yaml', config_1)
    local config_file_2 = treegen.write_script(dir, 'config_2.yaml', config_2)

    g.server_1.net_box_uri = 'unix/:' .. dir .. '/instance-001.iproto'
    g.server_1.config_file = config_file_1
    g.server_1:initialize()

    g.server_2.net_box_uri = 'unix/:' .. dir .. '/instance-002.iproto'
    g.server_2.config_file = config_file_2
    g.server_2:initialize()

    g.server_1:start({wait_until_ready = false})
    g.server_2:start({wait_until_ready = false})
    g.server_1:wait_until_ready()
    g.server_2:wait_until_ready()
end)

g.after_all(function()
    g.server_1:stop()
    g.server_2:stop()
    treegen.clean(g)
end)

local function assert_before_upgrade()
    t.assert_equals(box.space._schema:get{'version'}, {'version', 2, 11, 0})
    local info = box.info
    t.assert_equals(info.name, nil)
    t.assert_equals(info.replicaset.name, nil)
end

local function assert_after_upgrade(instance_name, replicaset_name)
    local info = box.info
    t.helpers.retrying({}, function()
        t.assert_equals(box.space._schema:get{'version'}, {'version', 3, 0, 0})
        t.assert_equals(info.name, instance_name)
        t.assert_equals(info.replicaset.name, replicaset_name)
    end)
end

g.test_upgrade = function()
    g.server_1:exec(assert_before_upgrade)
    g.server_2:exec(assert_before_upgrade)

    g.server_1:exec(function() box.schema.upgrade() end)
    g.server_2:wait_for_vclock_of(g.server_1)

    local rs_name = 'replicaset-001'
    g.server_1:exec(assert_after_upgrade, {g.server_1.alias, rs_name})
    g.server_2:exec(assert_after_upgrade, {g.server_2.alias, rs_name})
end
