local t = require('luatest')
local yaml = require('yaml')
local treegen = require('luatest.treegen')
local justrun = require('luatest.justrun')

local g = t.group()

g.test_report_instance_config_on_startup_failure = function()
    local dir = treegen.prepare_directory({}, {})
    treegen.write_file(dir, 'main.lua', [[
        error('application startup failed', 0)
    ]])
    local config = {
        credentials = {
            users = {
                guest = {
                    roles = {'super'},
                },
                client = {
                    password = 'topsecret',
                    roles = {'super'},
                },
            },
        },
        app = {
            file = 'main.lua',
        },
        iproto = {
            listen = {{
                uri = 'unix/:./{{ instance_name }}.iproto',
            }},
        },
        replication = {
            failover = 'manual',
        },
        groups = {
            ['group-001'] = {
                replicasets = {
                    ['replicaset-001'] = {
                        leader = 'instance-001',
                        instances = {
                            ['instance-001'] = {},
                        },
                    },
                },
            },
        },
    }
    local config_file = treegen.write_file(dir, 'config.yaml',
                                           yaml.encode(config))
    local env = {TT_CONFIG_DEBUG = '1'}
    local args = {'--name', 'instance-001', '--config', config_file}
    local opts = {nojson = true, stderr = true}

    local res = justrun.tarantool(dir, env, args, opts)

    t.assert_equals(res.exit_code, 1)
    t.assert_str_contains(res.stderr, 'application startup failed')
    t.assert_str_contains(res.stderr,
                          'Instance configuration at startup failure:')
    t.assert_str_contains(res.stderr, 'replication:')
    t.assert_str_contains(res.stderr, '  failover: manual')
    t.assert_str_contains(res.stderr, 'iproto:')
    t.assert_str_contains(res.stderr,
                          '  - uri: unix/:./instance-001.iproto')
    t.assert_str_contains(res.stderr, 'client:')
    t.assert_str_contains(res.stderr, 'password: <hidden>')
    t.assert_not_str_contains(res.stderr, 'topsecret')
end
