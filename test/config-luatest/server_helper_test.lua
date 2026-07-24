local fun = require('fun')
local yaml = require('yaml')
local t = require('luatest')
local treegen = require('test.treegen')
local server = require('test.luatest_helpers.server')
local helpers = require('test.config-luatest.helpers')

local g = helpers.group()

g.test_multiple_groups_and_replicasets_cluster_bootstrap = function(g)
    local dir = treegen.prepare_directory(g, {}, {})
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
            ['group-a'] = {
                replicasets = {
                    ['replicaset-a-01'] = {
                        leader = 'instance-a-01-001',
                        instances = {
                            ['instance-a-01-001'] = {},
                            ['instance-a-01-002'] = {},
                        },
                    },
                    ['replicaset-a-02'] = {
                        leader = 'instance-a-02-001',
                        instances = {
                            ['instance-a-02-001'] = {},
                            ['instance-a-02-002'] = {},
                        },
                    },
                },
            },
            ['group-b'] = {
                replicasets = {
                    ['replicaset-b-01'] = {
                        leader = 'instance-b-01-001',
                        instances = {
                            ['instance-b-01-001'] = {},
                            ['instance-b-01-002'] = {},
                        },
                    },
                    ['replicaset-b-02'] = {
                        leader = 'instance-b-02-001',
                        instances = {
                            ['instance-b-02-001'] = {},
                            ['instance-b-02-002'] = {},
                        },
                    },
                },
            },
        },
        replication = {
            failover = 'manual',
        },
    }
    local config_file = treegen.write_script(dir, 'config.yaml',
                                             yaml.encode(config))
    local opts = {config_file = config_file, chdir = dir}

    local servers = {}
    for _, group in pairs(config.groups) do
        for _, replicaset in pairs(group.replicasets) do
            for alias, _ in pairs(replicaset.instances) do
                local server_opts = fun.chain(opts, {alias = alias}):tomap()
                local server = server:new(server_opts)

                servers[alias] = server
                g['server_' .. alias] = server -- For autocleanup.
            end
        end
    end

    for _, server in pairs(servers) do
        server:start({wait_until_ready = false})
    end

    for _, server in pairs(servers) do
        server:wait_until_ready()
    end

    for _, server in pairs(servers) do
        t.assert_equals(server:eval('return box.info.name'), server.alias)
    end
end
