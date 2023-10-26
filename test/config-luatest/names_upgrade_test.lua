local t = require('luatest')
local treegen = require('test.treegen')
local server = require('test.luatest_helpers.server')
local yaml = require('yaml')
local fun = require('fun')
local fio = require('fio')

local g = t.group('set-names-automatically-on-upgrade')

g.before_all(function(g)
    treegen.init(g)

    g.uuids = {
        ['replicaset-001'] = 'cbf06940-0790-498b-948d-042b62cf3d29',
        ['instance-001'] =   '8a274925-a26d-47fc-9e1b-af88ce939412',
        ['instance-002'] =   '3de2e3e1-9ebe-4d0d-abb1-26d301b84633'
    }

    local datadir_prefix = 'test/box-luatest/upgrade/2.11.0/replicaset/'
    local datadir_1 = fio.abspath(fio.pathjoin(datadir_prefix, 'instance-001'))
    local datadir_2 = fio.abspath(fio.pathjoin(datadir_prefix, 'instance-002'))

    local dir = treegen.prepare_directory(g, {}, {})
    local workdir_1 = fio.pathjoin(dir, 'instance-001')
    local workdir_2 = fio.pathjoin(dir, 'instance-002')

    fio.mktree(workdir_1)
    fio.mktree(workdir_2)

    fio.copytree(datadir_1, workdir_1)
    fio.copytree(datadir_2, workdir_2)

    local config = {
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
    g.instance_1:drop()
    g.instance_2:drop()
    treegen.clean(g)
end)

local function assert_before_upgrade()
    t.assert_equals(box.space._schema:get{'version'}, {'version', 2, 11, 0})
    local info = box.info
    t.assert_equals(info.name, nil)
    t.assert_equals(info.replicaset.name, nil)
end

local function assert_after_upgrade(instance_name, replicaset_name, names)
    t.helpers.retrying({timeout = 20}, function()
        local info = box.info
        t.assert_equals(info.name, instance_name)
        t.assert_equals(info.replicaset.name, replicaset_name)

        local alerts = require('config')._alerts
        for name, _ in pairs(names) do
            t.assert_equals(alerts[name], nil)
        end
    end)
end

g.test_upgrade = function()
    g.instance_1:exec(assert_before_upgrade)
    g.instance_2:exec(assert_before_upgrade)

    g.instance_1:exec(function()
        box.ctl.wait_rw()
        box.schema.upgrade()
    end)

    g.instance_2:wait_for_vclock_of(g.instance_1)

    local rs = 'replicaset-001'
    g.instance_1:exec(assert_after_upgrade, {g.instance_1.alias, rs, g.uuids})
    g.instance_2:exec(assert_after_upgrade, {g.instance_2.alias, rs, g.uuids})
end
