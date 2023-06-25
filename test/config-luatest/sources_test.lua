local t = require('luatest')
local source_file = require('internal.config.source.file')

local g = t.group()

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
                            ['instance-001'] = {
                                database = {
                                    rw = true,
                                },
                            },
                        },
                    },
                },
            },
        },
    }

    t.assert_equals(res, exp)
end
