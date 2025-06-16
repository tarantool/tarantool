local t = require('luatest')
local server = require('luatest.server')
local treegen = require('luatest.treegen')
local yaml = require('yaml')

---@class luatest.group
local g = t.group()

local common_config = {
    credentials = {
        users = {
            guest = {
                roles = {'super'},
            },
        },
    },

    iproto = {
        listen = {
            {uri = 'unix/:./instance-001.iproto'},
        },
    },

    groups = {
        ['group-001'] = {
            replicasets = {
                ['replicaset-001'] = {
                    instances = {
                        ['instance-001'] = {
                            labels = {
                                value_1 = 'from config.yaml',
                                value_2 = 'from config.yaml',
                            },
                        },
                    },
                },
            },
        },
    },
}

g.before_each(function()
    g.dir = treegen.prepare_directory({}, {})
end)

g.after_each(function()
    g.server:drop()
    g.server = nil
end)

g.test_initial_merge = function()
    local config = table.deepcopy(common_config)
    config.include = {'included_config.yaml'}
    local included_config = {
        groups = {
            ['group-001'] = {
                replicasets = {
                    ['replicaset-001'] = {
                        instances = {
                            ['instance-001'] = {
                                labels = {
                                    value_2 = 'from included_config.yaml',
                                    value_3 = 'from included_config.yaml',
                                },
                            },
                        },
                    },
                },
            },
        },
    }

    local config_file = treegen.write_file(
        g.dir, 'config.yaml', yaml.encode(config))
    treegen.write_file(
        g.dir, 'included_config.yaml', yaml.encode(included_config))

    local opts = {
        config_file = config_file,
        alias = 'instance-001',
        chdir = g.dir,
    }
    g.server = server:new(opts)
    g.server:start()

    g.server:exec(function()
        t.assert_equals(
            require('config'):get('labels'),
            {
                value_1 = 'from config.yaml',
                value_2 = 'from included_config.yaml',
                value_3 = 'from included_config.yaml',
            }
        )
    end)
end

g.test_reload_merge = function()
    local config = table.deepcopy(common_config)
    local included_config = {
        groups = {
            ['group-001'] = {
                replicasets = {
                    ['replicaset-001'] = {
                        instances = {
                            ['instance-001'] = {
                                labels = {
                                    value_2 = 'from included_config.yaml',
                                    value_3 = 'from included_config.yaml',
                                },
                            },
                        },
                    },
                },
            },
        },
    }

    local config_file = treegen.write_file(
        g.dir, 'config.yaml', yaml.encode(config))
    treegen.write_file(
        g.dir, 'included_config.yaml', yaml.encode(included_config))

    local opts = {
        config_file = config_file,
        alias = 'instance-001',
        chdir = g.dir,
    }
    g.server = server:new(opts)
    g.server:start()

    g.server:exec(function()
        t.assert_equals(
            require('config'):get('labels'),
            {
                value_1 = 'from config.yaml',
                value_2 = 'from config.yaml',
                value_3 = nil,
            }
        )
    end)

    config.include = {'included_config.yaml'}
    treegen.write_file(g.dir, 'config.yaml', yaml.encode(config))

    g.server:exec(function()
        require('config'):reload()
        t.assert_equals(
            require('config'):get('labels'),
            {
                value_1 = 'from config.yaml',
                value_2 = 'from included_config.yaml',
                value_3 = 'from included_config.yaml',
            }
        )
    end)
end

g.test_include_non_existing = function()
    local config = table.deepcopy(common_config)
    config.include = {'non_existing_config.yaml'}
    local config_file = treegen.write_file(
        g.dir, 'config.yaml', yaml.encode(config))

    local opts = {
        config_file = config_file,
        alias = 'instance-001',
        chdir = g.dir,
    }
    g.server = server:new(opts)
    g.server:start()

    g.server:exec(function()
        t.assert_equals(
            require('config'):get('labels'),
            {
                value_1 = 'from config.yaml',
                value_2 = 'from config.yaml',
                value_3 = nil,
            }
        )
    end)
end

g.test_include_glob = function()
    local config = table.deepcopy(common_config)
    config.include = {'conf.d/*.yaml'}
    local config_file = treegen.write_file(
        g.dir, 'config.yaml', yaml.encode(config))

    local included_config_1 = {
        groups = {
            ['group-001'] = {
                replicasets = {
                    ['replicaset-001'] = {
                        instances = {
                            ['instance-001'] = {
                                labels = {
                                    value_2 = 'from included_config_1.yaml',
                                    value_3 = 'from included_config_1.yaml',
                                },
                            },
                        },
                    },
                },
            },
        },
    }

    local included_config_2 = {
        groups = {
            ['group-001'] = {
                replicasets = {
                    ['replicaset-001'] = {
                        instances = {
                            ['instance-001'] = {
                                labels = {
                                    value_2 = 'from included_config_2.yaml',
                                },
                            },
                        },
                    },
                },
            },
        },
    }

    treegen.write_file(
        g.dir, 'conf.d/config_1.yaml', yaml.encode(included_config_1))
    treegen.write_file(
        g.dir, 'conf.d/config_2.yaml', yaml.encode(included_config_2))

    local opts = {
        config_file = config_file,
        alias = 'instance-001',
        chdir = g.dir,
    }
    g.server = server:new(opts)
    g.server:start()

    g.server:exec(function()
        t.assert_equals(
            require('config'):get('labels'),
            {
                value_1 = 'from config.yaml',
                value_2 = 'from included_config_2.yaml',
                value_3 = 'from included_config_1.yaml',
            }
        )
    end)
end

g.test_include_multiple = function()
    local config = table.deepcopy(common_config)
    config.include = {
        'included_config_1.yaml',
        'included_config_2.yaml',
    }
    local config_file = treegen.write_file(
        g.dir, 'config.yaml', yaml.encode(config))

    local included_config_1 = {
        groups = {
            ['group-001'] = {
                replicasets = {
                    ['replicaset-001'] = {
                        instances = {
                            ['instance-001'] = {
                                labels = {
                                    value_2 = 'from included_config_1.yaml',
                                    value_3 = 'from included_config_1.yaml',
                                },
                            },
                        },
                    },
                },
            },
        },
    }

    local included_config_2 = {
        groups = {
            ['group-001'] = {
                replicasets = {
                    ['replicaset-001'] = {
                        instances = {
                            ['instance-001'] = {
                                labels = {
                                    value_2 = 'from included_config_2.yaml',
                                },
                            },
                        },
                    },
                },
            },
        },
    }

    treegen.write_file(
        g.dir, 'included_config_1.yaml', yaml.encode(included_config_1))
    treegen.write_file(
        g.dir, 'included_config_2.yaml', yaml.encode(included_config_2))

    local opts = {
        config_file = config_file,
        alias = 'instance-001',
        chdir = g.dir,
    }
    g.server = server:new(opts)
    g.server:start()

    g.server:exec(function()
        t.assert_equals(
            require('config'):get('labels'),
            {
                value_1 = 'from config.yaml',
                value_2 = 'from included_config_2.yaml',
                value_3 = 'from included_config_1.yaml',
            }
        )
    end)
end

g.test_include_nested = function()
    local config = table.deepcopy(common_config)
    config.include = {'included_config_1.yaml'}
    local config_file = treegen.write_file(
        g.dir, 'config.yaml', yaml.encode(config))

    local included_config_1 = {
        include = {'included_config_2.yaml'},
        groups = {
            ['group-001'] = {
                replicasets = {
                    ['replicaset-001'] = {
                        instances = {
                            ['instance-001'] = {
                                labels = {
                                    value_2 = 'from included_config_1.yaml',
                                    value_3 = 'from included_config_1.yaml',
                                },
                            },
                        },
                    },
                },
            },
        },
    }

    local included_config_2 = {
        groups = {
            ['group-001'] = {
                replicasets = {
                    ['replicaset-001'] = {
                        instances = {
                            ['instance-001'] = {
                                labels = {
                                    value_2 = 'from included_config_2.yaml',
                                },
                            },
                        },
                    },
                },
            },
        },
    }

    treegen.write_file(
        g.dir, 'included_config_1.yaml', yaml.encode(included_config_1))
    treegen.write_file(
        g.dir, 'included_config_2.yaml', yaml.encode(included_config_2))

    local opts = {
        config_file = config_file,
        alias = 'instance-001',
        chdir = g.dir,
    }
    g.server = server:new(opts)
    g.server:start()

    g.server:exec(function()
        t.assert_equals(
            require('config'):get('labels'),
            {
                value_1 = 'from config.yaml',
                value_2 = 'from included_config_2.yaml',
                value_3 = 'from included_config_1.yaml',
            }
        )
    end)
end

g.test_include_relative = function()
    local config = table.deepcopy(common_config)
    config.include = {'conf.d/included_config_1.yaml'}
    local config_file = treegen.write_file(
        g.dir, 'config.yaml', yaml.encode(config))

    local included_config_1 = {
        include = {'included_config_2.yaml'},
        groups = {
            ['group-001'] = {
                replicasets = {
                    ['replicaset-001'] = {
                        instances = {
                            ['instance-001'] = {
                                labels = {
                                    value_2 = 'from included_config_1.yaml',
                                    value_3 = 'from included_config_1.yaml',
                                },
                            },
                        },
                    },
                },
            },
        },
    }

    local included_config_2 = {
        groups = {
            ['group-001'] = {
                replicasets = {
                    ['replicaset-001'] = {
                        instances = {
                            ['instance-001'] = {
                                labels = {
                                    value_2 = 'from included_config_2.yaml',
                                },
                            },
                        },
                    },
                },
            },
        },
    }

    treegen.write_file(
        g.dir, 'conf.d/included_config_1.yaml', yaml.encode(included_config_1))
    treegen.write_file(
        g.dir, 'conf.d/included_config_2.yaml', yaml.encode(included_config_2))

    local opts = {
        config_file = config_file,
        alias = 'instance-001',
        chdir = g.dir,
    }
    g.server = server:new(opts)
    g.server:start()

    g.server:exec(function()
        t.assert_equals(
            require('config'):get('labels'),
            {
                value_1 = 'from config.yaml',
                value_2 = 'from included_config_2.yaml',
                value_3 = 'from included_config_1.yaml',
            }
        )
    end)
end

g.test_include_recursion = function()
    local config = table.deepcopy(common_config)
    config.log = {to = 'file'}

    local included_config = {
        include = {'config.yaml'},
        groups = {
            ['group-001'] = {
                replicasets = {
                    ['replicaset-001'] = {
                        instances = {
                            ['instance-001'] = {
                                labels = {
                                    value_2 = 'from included_config.yaml',
                                    value_3 = 'from included_config.yaml',
                                },
                            },
                        },
                    },
                },
            },
        },
    }

    local config_file = treegen.write_file(
        g.dir, 'config.yaml', yaml.encode(config))
    treegen.write_file(
        g.dir, 'included_config.yaml', yaml.encode(included_config))

    local opts = {
        config_file = config_file,
        alias = 'instance-001',
        chdir = g.dir,
    }
    g.server = server:new(opts)
    g.server:start()

    g.server:exec(function()
        t.assert_equals(
            require('config'):get('labels'),
            {
                value_1 = 'from config.yaml',
                value_2 = 'from config.yaml',
                value_3 = nil,
            }
        )
    end)

    config.include = {'included_config.yaml'}
    treegen.write_file(g.dir, 'config.yaml', yaml.encode(config))

    g.server:exec(function()
        require('config'):reload()
        t.assert_equals(
            require('config'):get('labels'),
            {
                value_1 = 'from config.yaml',
                value_2 = 'from included_config.yaml',
                value_3 = 'from included_config.yaml',
            }
        )
    end)

    t.helpers.retrying({timeout = 10}, function()
        t.assert(g.server:grep_log(
            ('skipping already processed config file: ' ..
            '"%s/config.yaml"'):format(g.dir),
            1024, {filename = g.dir .. '/var/log/instance-001/tarantool.log'}
        ))
    end)
end

g.test_include_erroneous = function()
    local config = table.deepcopy(common_config)
    local errorneous_config = ":"

    local config_file = treegen.write_file(
        g.dir, 'config.yaml', yaml.encode(config))
    treegen.write_file(g.dir, 'errorneous_config.yaml', errorneous_config)

    local opts = {
        config_file = config_file,
        alias = 'instance-001',
        chdir = g.dir,
    }

    g.server = server:new(opts)
    g.server:start()

    config.include = {'errorneous_config.yaml'}
    treegen.write_file(g.dir, 'config.yaml', yaml.encode(config))

    t.assert_error_msg_matches(
        ".*Unable to parse config file .* as YAML.*",
        g.server.exec, g.server, function()
            require('config'):reload()
    end)
end

g.test_conditional_include = function()
    local config = table.deepcopy(common_config)
    config.conditional = {
        {
            ['if'] = 'tarantool_version >= 0.0.0',
            ['include'] = {'included_config.yaml'},
        },
    }

    local included_config = {
        groups = {
            ['group-001'] = {
                replicasets = {
                    ['replicaset-001'] = {
                        instances = {
                            ['instance-001'] = {
                                labels = {
                                    value_1 = 'from included_config.yaml',
                                },
                            },
                        },
                    },
                },
            },
        },
    }

    local config_file = treegen.write_file(
        g.dir, 'config.yaml', yaml.encode(config))
    treegen.write_file(
        g.dir, 'included_config.yaml', yaml.encode(included_config))

    local opts = {
        config_file = config_file,
        alias = 'instance-001',
        chdir = g.dir,
    }
    g.server = server:new(opts)
    g.server:start()

    g.server:exec(function()
        t.assert_equals(
            require('config'):get('labels').value_1,
            'from included_config.yaml'
        )
    end)

    config = table.deepcopy(common_config)
    config.conditional = {
        {
            ['if'] = 'tarantool_version < 0.0.0',
            ['include'] = {'included_config.yaml'},
        },
    }
    treegen.write_file(g.dir, 'config.yaml', yaml.encode(config))

    g.server:exec(function()
        require('config'):reload()
        t.assert_equals(
            require('config'):get('labels').value_1,
            'from config.yaml'
        )
    end)
end
