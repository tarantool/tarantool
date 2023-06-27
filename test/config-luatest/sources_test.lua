local t = require('luatest')
local json = require('json')
local treegen = require('test.treegen')
local justrun = require('test.justrun')
local source_file = require('internal.config.source.file')

local g = t.group()

g.before_all(function()
    treegen.init(g)
end)

g.after_all(function()
    treegen.clean(g)
end)

g.test_source_file = function()
    local config = {_config_file = 'doc/examples/config/single.yaml'}
    source_file.sync(config, {})
    local res = source_file.get()
    local exp = {
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
                        instances = {
                            ['instance-001'] = {},
                        },
                    },
                },
            },
        },
    }

    t.assert_equals(res, exp)
end

g.test_source_env = function()
    local dir = treegen.prepare_directory(g, {}, {})
    local script = [[
        local json = require('json')
        local source_env = require('internal.config.source.env')
        source_env.sync({}, {})
        print(json.encode(source_env.get()))
    ]]
    treegen.write_script(dir, 'main.lua', script)

    local env = {TT_LOG_LEVEL = 'info', TT_MEMTX_MEMORY = 1000000}
    local opts = {nojson = true, stderr = false}
    local res = justrun.tarantool(dir, env, {'main.lua'}, opts)
    local exp = {
        config = {
            version = 'dev',
        },
        log = {
            level = 'info',
        },
        memtx = {
            memory = 1000000
        },
    }
    t.assert_equals(res.exit_code, 0)
    t.assert_equals(json.decode(res.stdout), exp)
end
