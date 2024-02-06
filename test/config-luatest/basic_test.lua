local fun = require('fun')
local yaml = require('yaml')
local fio = require('fio')
local t = require('luatest')
local treegen = require('test.treegen')
local justrun = require('test.justrun')
local server = require('test.luatest_helpers.server')
local helpers = require('test.config-luatest.helpers')
local cbuilder = require('test.config-luatest.cbuilder')

local g = helpers.group()

local function count_lines(s)
    return #s:split('\n')
end

local function last_n_lines(s, n)
    local lines = s:split('\n')
    local res = {}
    for i = #lines - n + 1, #lines do
        table.insert(res, lines[i])
    end
    return table.concat(res, '\n')
end

local function replicaset_has_no_alerts(g)
    local function no_alerts()
        local config = require('config')

        local info = config:info()
        t.assert_equals(info.alerts, {})
    end
    g.server_1:exec(no_alerts)
    g.server_2:exec(no_alerts)
    g.server_3:exec(no_alerts)
end

g.test_basic = function(g)
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
    local config_file = treegen.write_script(dir, 'config.yaml',
                                             yaml.encode(config))
    local opts = {config_file = config_file, chdir = dir}
    g.server = server:new(fun.chain(opts, {alias = 'instance-001'}):tomap())
    g.server:start()
    t.assert_equals(g.server:eval('return box.info.name'), g.server.alias)

    -- Verify that the default database mode for a singleton
    -- instance (the only one in its replicaset) is read-write.
    t.assert_equals(g.server:eval('return box.info.ro'), false)
end

g.test_example_single = function(g)
    local dir = treegen.prepare_directory(g, {}, {})
    local config_file = fio.abspath('doc/examples/config/single.yaml')
    local opts = {config_file = config_file, chdir = dir}
    g.server = server:new(fun.chain(opts, {alias = 'instance-001'}):tomap())
    g.server:start()
    t.assert_equals(g.server:eval('return box.info.name'), g.server.alias)
end

g.test_example_replicaset = function(g)
    local dir = treegen.prepare_directory(g, {}, {})
    fio.copytree('doc/examples/config/secrets', fio.pathjoin(dir, 'secrets'))

    local config_file = fio.abspath('doc/examples/config/replicaset.yaml')
    helpers.start_example_replicaset(g, dir, config_file)

    -- Verify that the default database mode for a replicaset with
    -- several instances (more than one) is read-only.
    t.assert_equals(g.server_1:eval('return box.info.ro'), false)
    t.assert_equals(g.server_2:eval('return box.info.ro'), true)
    t.assert_equals(g.server_3:eval('return box.info.ro'), true)

    -- Verify that this basic scenario starts without any alerts.
    replicaset_has_no_alerts(g)
end

g.test_example_replicaset_manual_failover = function(g)
    local dir = treegen.prepare_directory(g, {}, {})
    fio.copytree('doc/examples/config/secrets', fio.pathjoin(dir, 'secrets'))

    local config_file = fio.abspath('doc/examples/config/' ..
        'replicaset_manual_failover.yaml')
    helpers.start_example_replicaset(g, dir, config_file)

    -- Verify that the only read-write instance is one that is set
    -- as the leader.
    t.assert_equals(g.server_1:eval('return box.info.ro'), false)
    t.assert_equals(g.server_2:eval('return box.info.ro'), true)
    t.assert_equals(g.server_3:eval('return box.info.ro'), true)

    -- Verify that this basic scenario starts without any alerts.
    replicaset_has_no_alerts(g)
end

g.test_example_replicaset_election_failover = function(g)
    local dir = treegen.prepare_directory(g, {}, {})
    fio.copytree('doc/examples/config/secrets', fio.pathjoin(dir, 'secrets'))

    local config_file = fio.abspath('doc/examples/config/' ..
        'replicaset_election_failover.yaml')
    helpers.start_example_replicaset(g, dir, config_file)

    -- Verify that one of the instances is elected as leader.
    local rw_count = fun.iter({
        g.server_1:eval('return box.info.ro'),
        g.server_2:eval('return box.info.ro'),
        g.server_3:eval('return box.info.ro'),
    }):filter(function(ro)
        return not ro
    end):length()
    t.assert_equals(rw_count, 1)

    -- Verify that this basic scenario starts without any alerts.
    replicaset_has_no_alerts(g)
end

local err_msg_cannot_find_user = 'Cannot find user unknown ' ..
    'in the config to use its password in a replication peer URI'
local err_msg_no_suitable_uris = 'replication.peers construction for ' ..
    'instance "instance-001" of replicaset "replicaset-001" of group ' ..
    '"group-001": no suitable peer URIs found'

-- Bad cases for building replicaset.
for case_name, case in pairs({
    advertise_unknown_user = {
        listen_2 = {{
            uri = 'unix/:./{{ instance_name }}.iproto'
        }},
        advertise_2 = {
            login = 'unknown',
        },
        exp_err = err_msg_cannot_find_user,
    },
    advertise_unknown_user_uri = {
        listen_2 = {{
            uri = 'unix/:./{{ instance_name }}.iproto'
        }},
        advertise_2 = {
            uri = 'unix/:./{{ instance_name }}.iproto',
            login = 'unknown',
        },
        exp_err = err_msg_cannot_find_user,
    },
    no_advertise_no_listen = {
        listen_2 = {},
        listen_3 = {},
        advertise_2 = box.NULL,
        advertise_3 = box.NULL,
        exp_err = err_msg_no_suitable_uris,
    },
    no_advertise_no_suitable_listen = {
        listen_2 = {
            {uri = 'localhost:0'},
            {uri = '0.0.0.0:3301'},
            {uri = '[::]:3301'}
        },
        listen_3 = {
            {uri = 'localhost:0'},
            {uri = '0.0.0.0:3301'},
            {uri = '[::]:3301'}
        },
        advertise_2 = box.NULL,
        advertise_3 = box.NULL,
        exp_err = err_msg_no_suitable_uris,
    },
    advertise_user_no_suitable_listen = {
        listen_2 = {
            {uri = 'localhost:0'},
            {uri = '0.0.0.0:3301'},
            {uri = '[::]:3301'}
        },
        listen_3 = {
            {uri = 'localhost:0'},
            {uri = '0.0.0.0:3301'},
            {uri = '[::]:3301'}
        },
        advertise_2 = {
            login = 'replicator',
        },
        advertise_3 = {
            login = 'replicator',
        },
        exp_err = err_msg_no_suitable_uris,
    },
    advertise_user_pass_no_suitable_listen = {
        listen_2 = {
            {uri = 'localhost:0'},
            {uri = '0.0.0.0:3301'},
            {uri = '[::]:3301'}
        },
        listen_3 = {
            {uri = 'localhost:0'},
            {uri = '0.0.0.0:3301'},
            {uri = '[::]:3301'}
        },
        advertise_2 = {
            login = 'replicator',
            password = 'topsecret',
        },
        advertise_3 = {
            login = 'replicator',
            password = 'topsecret',
        },
        exp_err = err_msg_no_suitable_uris,
    },
    all_ro = {
        -- All the instances are  configured to the read-only mode
        -- (and instance-001 has no existing snapshot).
        mode = 'ro',
        exp_err = 'Startup failure.\nNo leader to register new instance ' ..
            '"instance-001". All the instances in replicaset ' ..
            '"replicaset-001" of group "group-001" are configured to the ' ..
            'read-only mode.',
    },
    failover_off_leader_is_set = {
        -- The leader option is set together with failover =
        -- "off". This configuration is forbidden.
        failover = 'off',
        leader = 'instance-001',
        exp_err = '"leader" = "instance-001" option is set for replicaset ' ..
            '"replicaset-001" of group "group-001", but this option cannot ' ..
            'be used together with replication.failover = "off"',
    },
    failover_election_leader_is_set = {
        -- The leader option is set together with failover =
        -- "election". This configuration is forbidden.
        failover = 'election',
        leader = 'instance-001',
        exp_err = '"leader" = "instance-001" option is set for replicaset ' ..
            '"replicaset-001" of group "group-001", but this option cannot ' ..
            'be used together with replication.failover = "election"',
    },
    failover_manual_mode_is_set = {
        -- The database.mode option is set together with
        -- failover = "manual". This configuration is forbidden.
        failover = 'manual',
        mode = 'rw',
        exp_err = 'database.mode = "rw" is set for instance "instance-001" ' ..
            'of replicaset "replicaset-001" of group "group-001", but this ' ..
            'option cannot be used together with replication.failover = ' ..
            '"manual"',
    },
    failover_election_mode_is_set = {
        -- The database.mode option is set together with
        -- failover = "election". This configuration is forbidden.
        failover = 'election',
        mode = 'rw',
        exp_err = 'database.mode = "rw" is set for instance "instance-001" ' ..
            'of replicaset "replicaset-001" of group "group-001", but this ' ..
            'option cannot be used together with replication.failover = ' ..
            '"election"',
    },
    failover_manual_unknown_instance = {
        -- The leader option points to an unknown instance.
        failover = 'manual',
        leader = 'unknown',
        mode = box.NULL,
        exp_err = '"leader" = "unknown" option is set for replicaset ' ..
            '"replicaset-001" of group "group-001", but instance "unknown" ' ..
            'is not found in this replicaset',
    },
}) do
    g[('test_bad_replicaset_build_%s'):format(case_name)] = function(g)
        local dir = treegen.prepare_directory(g, {}, {})

        local good_listen = {{
            uri = 'unix/:./{{ instance_name }}.iproto'
        }}
        local good_advertise = {
            login = 'replicator',
        }

        local instance_001 = {
            database = {
                mode = case.mode or 'rw',
            },
            iproto = {
                listen = case.listen_1 or good_listen,
                advertise = {
                    peer = case.advertise_1 or good_advertise,
                },
            },

        }
        local instance_002 = {
            iproto = {
                listen = case.listen_2 or good_listen,
                advertise = {
                    peer = case.advertise_2 or good_advertise,
                },
            },
        }
        local instance_003 = {
            iproto = {
                listen = case.listen_3 or good_listen,
                advertise = {
                    peer = case.advertise_3 or good_advertise,
                },
            },
        }

        local config = {
            credentials = {
                users = {
                    replicator = {
                        password = 'topsecret',
                        roles = {'replication'},
                    },
                    guest = {
                        roles = {'super'},
                    },
                },
            },
            replication = {
                failover = case.failover,
            },
            groups = {
                ['group-001'] = {
                    replicasets = {
                        ['replicaset-001'] = {
                            leader = case.leader,
                            instances = {
                                ['instance-001'] = instance_001,
                                ['instance-002'] = instance_002,
                                ['instance-003'] = instance_003,
                            },
                        },
                    },
                },
            },
        }

        local config_file = treegen.write_script(dir, 'config.yaml',
            yaml.encode(config))
        local env = {}
        local args = {'--name', 'instance-001', '--config', config_file}
        local opts = {nojson = true, stderr = true}
        local res = justrun.tarantool(dir, env, args, opts)

        local exp_stderr = table.concat({
            ('LuajitError: %s'):format(case.exp_err),
            'fatal error, exiting the event loop',
        }, '\n')
        t.assert_equals({
            exit_code = res.exit_code,
            stderr = last_n_lines(res.stderr, count_lines(exp_stderr)),
        }, {
            exit_code = 1,
            stderr = exp_stderr,
        })
    end
end

-- Successful cases for building replicaset.
for case_name, case in pairs({
    advertise_user_pass_uri = {
        listen = {{
            uri = 'unix/:./{{ instance_name }}.iproto',
        }},
        advertise = {
            uri = 'unix/:./{{ instance_name }}.iproto',
            login = 'replicator',
            password = 'topsecret',
        },
    },
    advertise_user_uri = {
        listen = {{
            uri = 'unix/:./{{ instance_name }}.iproto',
        }},
        advertise = {
            uri = 'unix/:./{{ instance_name }}.iproto',
            login = 'replicator',
        },
    },
    advertise_uri = {
        listen = {{
            uri = 'unix/:./{{ instance_name }}.iproto',
        }},
        advertise = {
            uri = 'unix/:./{{ instance_name }}.iproto',
        },
    },
    advertise_user_pass = {
        listen = {{
            uri = 'unix/:./{{ instance_name }}.iproto',
        }},
        advertise = {
            login = 'replicator',
            password = 'topsecret',
        },
    },
    advertise_user = {
        listen = {{
            uri = 'unix/:./{{ instance_name }}.iproto',
        }},
        advertise = {
            login = 'replicator',
        },
    },
    advertise_user_first_listen_suitable = {
        listen = {
            {uri = 'unix/:./{{ instance_name }}.iproto'},
            {uri = 'localhost:0'},
        },
        advertise = {
            login = 'replicator',
        },
    },
    advertise_user_second_listen_suitable = {
        listen = {
            {uri = 'localhost:0'},
            {uri = 'unix/:./{{ instance_name }}.iproto'},
        },
        advertise = {
            login = 'replicator',
        },
    },
    advertise_guest = {
        listen = {{
            uri = 'unix/:./{{ instance_name }}.iproto',
        }},
        advertise = {
            login = 'guest',
        },
    },
    advertise_guest_uri = {
        listen = {{
            uri = 'unix/:./{{ instance_name }}.iproto',
        }},
        advertise = {
            uri = 'unix/:./{{ instance_name }}.iproto',
            login = 'guest',
        },
    },
    some_peers_have_no_suitable_uri = {
        -- It is OK to have a peer with iproto.listen unsuitable
        -- to connect if there is at least one suitable to
        -- replicate from.
        listen = {{
            uri = 'unix/:./{{ instance_name }}.iproto',
        }},
        advertise = {
            login = 'replicator',
        },
        listen_4 = {{
            uri = 'localhost:0',
        }},
    },
    election_mode = {
        listen = {{
            uri = 'unix/:./{{ instance_name }}.iproto'
        }},
        advertise = {
            login = 'replicator',
        },
        failover = 'election',
        database_mode = {box.NULL, box.NULL, box.NULL},
        election_mode = {'off', 'voter', 'candidate'},
        check = function(g)
            t.assert_equals(g.server_1:eval('return box.info.ro'), true)
            t.assert_equals(g.server_2:eval('return box.info.ro'), true)
            t.assert_equals(g.server_3:eval('return box.info.ro'), false)
        end,
    },
}) do
    g[('test_good_replicaset_build_%s'):format(case_name)] = function(g)
        local database_mode = case.database_mode or {'rw', nil, nil}
        local election_mode = case.election_mode or {}
        local config = {
            credentials = {
                users = {
                    replicator = {
                        password = 'topsecret',
                        roles = {'replication'},
                    },
                    guest = {
                        roles = {'super'},
                    },
                    client = {
                        password = 'secret',
                        roles = {'super'},
                    },
                },
            },
            iproto = {
                listen = case.listen,
                advertise = {
                    peer = case.advertise,
                },
            },
            replication = {
                failover = case.failover,
            },
            groups = {
                ['group-001'] = {
                    replicasets = {
                        ['replicaset-001'] = {
                            instances = {
                                ['instance-001'] = {
                                    database = {
                                        mode = database_mode[1],
                                    },
                                    replication = {
                                        election_mode = election_mode[1],
                                    },
                                },
                                ['instance-002'] = {
                                    database = {
                                        mode = database_mode[2],
                                    },
                                    replication = {
                                        election_mode = election_mode[2],
                                    },
                                },
                                ['instance-003'] = {
                                    database = {
                                        mode = database_mode[3],
                                    },
                                    replication = {
                                        election_mode = election_mode[3],
                                    },
                                },
                            },
                        },
                    },
                },
            },
        }

        -- Add fourth instance into the config if it is requested
        -- by the test case.
        --
        -- This instance is not started, just present in the
        -- configuration.
        if case.listen_4 ~= nil then
            local group = config.groups['group-001']
            local replicaset = group.replicasets['replicaset-001']
            replicaset.instances['instance-004'] = {
                iproto = {
                    listen = case.listen_4,
                },
            }
        end

        local dir = treegen.prepare_directory(g, {}, {})
        local config_file = treegen.write_script(dir, 'config.yaml',
            yaml.encode(config))
        helpers.start_example_replicaset(g, dir, config_file)
        if case.check ~= nil then
            case.check(g)
        end
    end
end

-- Verify that it is possible to extend config's functionality
-- using a non-public API.
--
-- The test registers an additional configuration source and
-- verifies that values from it are taken into account.
--
-- It also verifies that the extension can add a post-apply hook.
g.test_extras_on_community_edition = function(g)
    -- Tarantool EE has its own internal.config.extras module, so
    -- the external one will be ignored (if it is not placed into
    -- the `override` directory).
    t.tarantool.skip_if_enterprise()

    local extras = string.dump(function()
        local mysource = setmetatable({
            name = 'mysource',
            type = 'instance',
            _values = {},
        }, {
            __index = {
                sync = function(self, _config, _iconfig)
                    self._values = {
                        fiber = {
                            slice = {
                                warn = 10,
                                err = 15,
                            },
                        },
                    }
                end,
                get = function(self)
                    return self._values
                end,
            },
        })

        local function initialize(config)
            config:_register_source(mysource)
        end

        local function post_apply(_config)
            rawset(_G, 'foo', 'extras.post_apply() is called')
        end

        return {
            initialize = initialize,
            post_apply = post_apply,
        }
    end)

    local dir = treegen.prepare_directory(g, {}, {})
    treegen.write_script(dir, 'internal/config/extras.lua', extras)

    local verify = function()
        local config = require('config')
        t.assert_equals(config:get('fiber.slice'), {warn = 10, err = 15})
        t.assert_equals(rawget(_G, 'foo'), 'extras.post_apply() is called')
    end

    helpers.success_case(g, {
        dir = dir,
        verify = verify,
    })
end

-- Verify that extras.post_apply() will be called even if reload fails.
g.test_extras_after_fail = function(g)
    -- Tarantool EE has its own internal.config.extras module, so
    -- the external one will be ignored (if it is not placed into
    -- the `override` directory).
    t.tarantool.skip_if_enterprise()

    local dir = treegen.prepare_directory(g, {}, {})
    local extras = string.dump(function()
        local function post_apply(_config)
            local config = require('config')
            rawset(_G, 'foo', ('status = %s'):format(config:info().status))
        end

        return {
            initialize = function() end,
            post_apply = post_apply,
        }
    end)
    treegen.write_script(dir, 'internal/config/extras.lua', extras)

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
    local config_file = treegen.write_script(dir, 'config.yaml',
                                             yaml.encode(config))
    local opts = {
        config_file = config_file,
        alias = 'instance-001',
        chdir = dir,
    }
    g.server = server:new(opts)
    g.server:start()

    g.server:exec(function()
        t.assert_equals(rawget(_G, 'foo'), 'status = ready')
    end)

    config.app = {file = 'one'}
    treegen.write_script(dir, 'config.yaml', yaml.encode(config))
    g.server:exec(function()
        local config = require('config')
        pcall(config.reload, config)
        t.assert_equals(rawget(_G, 'foo'), 'status = check_errors')
    end)
end

-- Verify that an instance name and a replicaset name are set from
-- the cluster config topology, while a cluster name remains
-- unset.
g.test_persistent_names = function(g)
    local verify = function()
        t.assert_equals(box.info.name, 'instance-001')
        t.assert_equals(box.info.replicaset.name, 'replicaset-001')
        t.assert_equals(box.info.cluster.name, nil)
    end

    helpers.success_case(g, {
        verify = verify,
    })
end

-- Verify that a directory with the configuration file is added
-- into the Lua loader paths.
g.test_loader_paths = function(g)
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
    local config_file = treegen.write_script(dir, 'foo/config.yaml',
                                             yaml.encode(config))

    treegen.write_script(dir, 'foo/bar.lua', [[return {whoami = 'bar'}]])

    local opts = {config_file = config_file, chdir = dir}
    g.server = server:new(fun.chain(opts, {alias = 'instance-001'}):tomap())
    g.server:start()

    g.server:exec(function()
        local bar = require('bar')
        t.assert_equals(bar.whoami, 'bar')
    end)
end

-- Verify, that instance can recover from the xlogs using config.
g.test_recovery_without_uuid = function(g)
    local dir = treegen.prepare_directory(g, {}, {})
    local cfg = cbuilder.new()
        :add_instance('instance-001', {
            database = {
                mode = 'rw',
                instance_uuid = '22222222-2222-2222-0022-222222222222',
            }
        })
        :config()
    local cfg_file = treegen.write_script(dir, 'config.yaml', yaml.encode(cfg))
    local opts = {
        config_file = cfg_file,
        chdir = dir,
        net_box_credentials = {
            user = 'client',
            password = 'secret',
        },
    }
    g.server = server:new(fun.chain(opts, {alias = 'instance-001'}):tomap())

    -- Initial start.
    g.server:start()
    g.server:exec(function()
        t.assert_equals(box.info.status, 'running')
    end)

    -- Remove UUID. Previously generated ones were used, so it wasn't possible
    -- to recover from the xlogs without passing UUID to config.
    local cfg_rs = cfg.groups['group-001'].replicasets['replicaset-001']
    cfg_rs.instances['instance-001'].database.instance_uuid = nil
    cfg_file = treegen.write_script(dir, 'config.yaml', yaml.encode(cfg))
    g.server.config_file = cfg_file
    -- Recovery process.
    g.server:restart()
    g.server:exec(function()
        t.assert_equals(box.info.status, 'running')
    end)
end
