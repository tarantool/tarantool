local fun = require('fun')
local yaml = require('yaml')
local fio = require('fio')
local t = require('luatest')
local treegen = require('test.treegen')
local justrun = require('test.justrun')
local server = require('test.luatest_helpers.server')
local helpers = require('test.config-luatest.helpers')

local g = t.group()

g.before_all(function(g)
    treegen.init(g)
end)

g.after_all(function(g)
    treegen.clean(g)
end)

g.after_each(function(g)
    for k, v in pairs(g) do
        if k == 'server' or k:match('^server_%d+$') then
            v:stop()
        end
    end
end)

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
    local config_file = fio.abspath('doc/examples/config/replicaset.yaml')
    helpers.start_example_replicaset(g, dir, config_file)

    -- Verify that the default database mode for a replicaset with
    -- several instances (more than one) is read-only.
    t.assert_equals(g.server_1:eval('return box.info.ro'), false)
    t.assert_equals(g.server_2:eval('return box.info.ro'), true)
    t.assert_equals(g.server_3:eval('return box.info.ro'), true)
end

g.test_example_replicaset_manual_failover = function(g)
    local dir = treegen.prepare_directory(g, {}, {})
    local config_file = fio.abspath('doc/examples/config/' ..
        'replicaset_manual_failover.yaml')
    helpers.start_example_replicaset(g, dir, config_file)

    -- Verify that the only read-write instance is one that is set
    -- as the leader.
    t.assert_equals(g.server_1:eval('return box.info.ro'), false)
    t.assert_equals(g.server_2:eval('return box.info.ro'), true)
    t.assert_equals(g.server_3:eval('return box.info.ro'), true)
end

g.test_example_replicaset_election_failover = function(g)
    local dir = treegen.prepare_directory(g, {}, {})
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
end

local err_msg_cannot_find_user = 'box_cfg.apply: cannot find user unknown ' ..
    'in the config to use its password in a replication peer URI'
local err_msg_no_suitable_uris = 'box_cfg.apply: unable to build replicaset ' ..
    '"replicaset-001" of group "group-001": instance "instance-002" has no ' ..
    'iproto.advertise.peer or iproto.listen URI suitable to create a client ' ..
    'socket'

-- Bad cases for building replicaset.
for case_name, case in pairs({
    no_advertise_no_listen = {
        listen = nil,
        advertise = nil,
        exp_err = 'box_cfg.apply: unable to build replicaset ' ..
            '"replicaset-001" of group "group-001": instance "instance-002" ' ..
            'has neither iproto.advertise nor iproto.listen options',
    },
    advertise_unknown_user_uri = {
        listen = 'unix/:./{{ instance_name }}.iproto',
        advertise = 'unknown@unix/:./{{ instance_name }}.iproto',
        exp_err = err_msg_cannot_find_user,
    },
    no_advertise_no_suitable_listen = {
        listen = 'localhost:0,0.0.0.0:3301,[::]:3301',
        advertise = nil,
        exp_err = err_msg_no_suitable_uris,
    },
    advertise_user_no_suitable_listen = {
        listen = 'localhost:0,0.0.0.0:3301,[::]:3301',
        advertise = 'replicator@',
        exp_err = err_msg_no_suitable_uris,
    },
    advertise_user_pass_no_suitable_listen = {
        listen = 'localhost:0,0.0.0.0:3301,[::]:3301',
        advertise = 'replicator:topsecret@',
        exp_err = err_msg_no_suitable_uris,
    },
    advertise_unknown_user = {
        listen = 'unix/:./{{ instance_name }}.iproto',
        advertise = 'unknown@',
        exp_err = err_msg_cannot_find_user,
    },
    all_ro = {
        -- The URIs here are good. But all the instances are
        -- configured to the read-only mode (and instance-001
        -- has no existing snapshot).
        listen = 'unix/:./{{ instance_name }}.iproto',
        advertise = 'replicator@',
        mode = 'ro',
        exp_err = 'Startup failure.\nNo leader to register new instance ' ..
            '"instance-001". All the instances in replicaset ' ..
            '"replicaset-001" of group "group-001" are configured to the ' ..
            'read-only mode.',
    },
    failover_off_leader_is_set = {
        -- The URIs here are good. But the leader option is set
        -- together with failover = "off". This configuration
        -- is forbidden.
        listen = 'unix/:./{{ instance_name }}.iproto',
        advertise = 'replicator@',
        failover = 'off',
        leader = 'instance-001',
        exp_err = '"leader" = "instance-001" option is set for replicaset ' ..
            '"replicaset-001" of group "group-001", but this option cannot ' ..
            'be used together with replication.failover = "off"',
    },
    failover_election_leader_is_set = {
        -- The URIs here are good. But the leader option is set
        -- together with failover = "election". This configuration
        -- is forbidden.
        listen = 'unix/:./{{ instance_name }}.iproto',
        advertise = 'replicator@',
        failover = 'election',
        leader = 'instance-001',
        exp_err = '"leader" = "instance-001" option is set for replicaset ' ..
            '"replicaset-001" of group "group-001", but this option cannot ' ..
            'be used together with replication.failover = "election"',
    },
    failover_manual_mode_is_set = {
        -- The URIs here are good. But the database.mode option is
        -- set together with failover = "manual". This
        -- configuration is forbidden.
        listen = 'unix/:./{{ instance_name }}.iproto',
        advertise = 'replicator@',
        failover = 'manual',
        mode = 'rw',
        exp_err = 'database.mode = "rw" is set for instance "instance-001" ' ..
            'of replicaset "replicaset-001" of group "group-001", but this ' ..
            'option cannot be used together with replication.failover = ' ..
            '"manual"',
    },
    failover_election_mode_is_set = {
        -- The URIs here are good. But the database.mode option is
        -- set together with failover = "election". This
        -- configuration is forbidden.
        listen = 'unix/:./{{ instance_name }}.iproto',
        advertise = 'replicator@',
        failover = 'election',
        mode = 'rw',
        exp_err = 'database.mode = "rw" is set for instance "instance-001" ' ..
            'of replicaset "replicaset-001" of group "group-001", but this ' ..
            'option cannot be used together with replication.failover = ' ..
            '"election"',
    },
    failover_manual_unknown_instance = {
        -- The URIs here are good. But the leader option points
        -- to an unknown instance.
        listen = 'unix/:./{{ instance_name }}.iproto',
        advertise = 'replicator@',
        failover = 'manual',
        leader = 'unknown',
        mode = box.NULL,
        exp_err = '"leader" = "unknown" option is set for replicaset ' ..
            '"replicaset-001" of group "group-001", but instance "unknown" ' ..
            'is not found in this replicaset',
    },
}) do
    g[('test_bad_replicaset_build_%s'):format(case_name)] = function()
        local dir = treegen.prepare_directory(g, {}, {})
        local instance_002 = {
            iproto = {
                listen = case.listen,
                advertise = {
                    peer = case.advertise,
                },
            },
        }
        local good_listen = 'unix/:./{{ instance_name }}.iproto'
        local config = {
            credentials = {
                users = {
                    replicator = {
                        password = {
                            plain = 'topsecret',
                        },
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
                                ['instance-001'] = {
                                    database = {
                                        mode = case.mode or 'rw',
                                    },
                                    iproto = {
                                        listen = good_listen,
                                        advertise = {
                                            peer = 'replicator@',
                                        },
                                    },
                                },
                                ['instance-002'] = instance_002,
                                ['instance-003'] = {
                                    iproto = {
                                        listen = good_listen,
                                        advertise = {
                                            peer = 'replicator@',
                                        },
                                    },
                                },
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
        t.assert_equals(res, {
            exit_code = 1,
            stderr = exp_stderr,
        })
    end
end

-- Successful cases for building replicaset.
for case_name, case in pairs({
    advertise_user_pass_uri = {
        listen = 'unix/:./{{ instance_name }}.iproto',
        advertise = 'replicator:topsecret@unix/:./{{ instance_name }}.iproto',
    },
    advertise_user_uri = {
        listen = 'unix/:./{{ instance_name }}.iproto',
        advertise = 'replicator@unix/:./{{ instance_name }}.iproto',
    },
    advertise_uri = {
        listen = 'unix/:./{{ instance_name }}.iproto',
        advertise = 'unix/:./{{ instance_name }}.iproto',
    },
    advertise_user_pass = {
        listen = 'unix/:./{{ instance_name }}.iproto',
        advertise = 'replicator:topsecret@',
    },
    advertise_user = {
        listen = 'unix/:./{{ instance_name }}.iproto',
        advertise = 'replicator@',
    },
    advertise_user_first_listen_suitable = {
        listen = 'unix/:./{{ instance_name }}.iproto,localhost:0',
        advertise = 'replicator@',
    },
    advertise_user_second_listen_suitable = {
        listen = 'localhost:0,unix/:./{{ instance_name }}.iproto',
        advertise = 'replicator@',
    },
    advertise_guest = {
        listen = 'unix/:./{{ instance_name }}.iproto',
        advertise = 'guest@',
    },
    advertise_guest_uri = {
        listen = 'unix/:./{{ instance_name }}.iproto',
        advertise = 'guest@unix/:./{{ instance_name }}.iproto',
    },
    election_mode = {
        listen = 'unix/:./{{ instance_name }}.iproto',
        advertise = 'replicator@',
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
                        password = {
                            plain = 'topsecret',
                        },
                        roles = {'replication'},
                    },
                    guest = {
                        roles = {'super'},
                    },
                    client = {
                        password = {
                            plain = 'secret',
                        },
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

        local dir = treegen.prepare_directory(g, {}, {})
        local config_file = treegen.write_script(dir, 'config.yaml',
            yaml.encode(config))
        helpers.start_example_replicaset(g, dir, config_file)
        if case.check ~= nil then
            case.check(g)
        end
    end
end
