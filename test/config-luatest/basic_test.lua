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
    local config_file = treegen.write_script(dir, 'config.yaml',
                                             yaml.encode(config))
    local opts = {config_file = config_file, chdir = dir}
    g.server = server:new(fun.chain(opts, {alias = 'instance-001'}):tomap())
    g.server:start()
    t.assert_equals(g.server:eval('return box.info.name'), g.server.alias)
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
end

g.test_no_advertise_no_listen = function(g)
    local dir = treegen.prepare_directory(g, {}, {})
    local config = {
        credentials = {
            users = {
                guest = {
                    roles = {'super'},
                },
            },
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
                                iproto = {
                                    listen =
                                        'unix/:./{{ instance_name }}.iproto',
                                },
                            },
                            -- No iproto.advertise or iproto.listen.
                            ['instance-002'] = {},
                            ['instance-003'] = {},
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
    t.assert_equals(res, {
        exit_code = 1,
        stderr = 'LuajitError: box_cfg.apply: unable to build replicaset ' ..
            '"replicaset-001" of group "group-001": instance "instance-002" ' ..
            'has neither iproto.advertise nor iproto.listen options\n' ..
            'fatal error, exiting the event loop',
    })
end

g.test_no_advertise_unsuitable_listen = function(g)
    local dir = treegen.prepare_directory(g, {}, {})
    local instance_002 = {
        iproto = {
            -- This option is set in the particular cases below.
            -- listen = <...>,
        },
    }
    local config = {
        credentials = {
            users = {
                guest = {
                    roles = {'super'},
                },
            },
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
                                iproto = {
                                    listen =
                                        'unix/:./{{ instance_name }}.iproto',
                                },
                            },
                            ['instance-002'] = instance_002,
                            ['instance-003'] = {},
                        },
                    },
                },
            },
        },
    }

    local exp_stderr = 'LuajitError: box_cfg.apply: unable to build ' ..
        'replicaset "replicaset-001" of group "group-001": instance ' ..
        '"instance-002" has no iproto.advertise option and neither ' ..
        'of the iproto.listen URIs are suitable to create a client ' ..
        'socket\nfatal error, exiting the event loop'
    for _, listen in ipairs({
        '0.0.0.0:3301',
        '[::]:3301',
        'localhost:0',
    }) do
        instance_002.iproto.listen = listen
        local config_file = treegen.write_script(dir, 'config.yaml',
            yaml.encode(config))
        local env = {}
        local args = {'--name', 'instance-001', '--config', config_file}
        local opts = {nojson = true, stderr = true}
        local res = justrun.tarantool(dir, env, args, opts)
        t.assert_equals(res, {
            exit_code = 1,
            stderr = exp_stderr,
        })
    end
end

g.test_no_advertise_second_listen_suitable = function()
    local config = {
        credentials = {
            users = {
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
            listen = 'localhost:0,unix/:./{{ instance_name }}.iproto',
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
                            ['instance-002'] = {},
                            ['instance-003'] = {},
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
end
