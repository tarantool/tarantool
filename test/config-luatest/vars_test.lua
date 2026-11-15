local instance_config = require('internal.config.instance_config')
local fio = require('fio')
local justrun = require('luatest.justrun')
local server = require('luatest.server')
local yaml = require('yaml')
local t = require('luatest')
local treegen = require('luatest.treegen')
local helpers = require('test.config-luatest.helpers')

local g = helpers.group()

-- Verify the given configuration option.
--
-- Also ensure that the last configuration apply is successful.
--
-- Usage:
--
-- helpers.success_case(g, {
--     <...>,
--     verify = verify_option('process.title', 'foo'),
-- })
local function verify_option(option, exp)
    return loadstring(([[
        local t = require('luatest')
        local config = require('config')

        t.assert_equals(config:info().status, 'ready')
        t.assert_equals(config:get(%q), %q)
    ]]):format(option, exp))
end

-- Verify all the template variables:
--
-- * {{ instance_name }}
-- * {{ replicaset_name }}
-- * {{ group_name }}
g.test_basic = function(g)
    local cases = {
        {
            var_name = 'instance_name',
            var_value = 'instance-001',
        },
        {
            var_name = 'replicaset_name',
            var_value = 'replicaset-001',
        },
        {
            var_name = 'group_name',
            var_value = 'group-001',
        },
    }

    -- Choose some string option to set it to {{ var_name }}
    -- and read the value from inside the instance (in verify()
    -- function).
    local option = 'process.title'

    for _, case in ipairs(cases) do
        helpers.success_case(g, {
            options = {
                [option] = ('{{ %s }}'):format(case.var_name),
            },
            verify = function(option, var_value)
                local config = require('config')
                t.assert_equals(config:get(option), var_value)
            end,
            verify_args = {option, case.var_value},
        })
    end
end

-- Verify several template variables within one option.
g.test_several_vars = function(g)
    local option = 'process.title'

    helpers.success_case(g, {
        options = {
            [option] = ('{{ %s }}::{{ %s }}::{{ %s }}'):format('group_name',
                'replicaset_name', 'instance_name')
        },
        verify = function(option)
            local config = require('config')
            t.assert_equals(config:get(option), ('%s::%s::%s'):format(
                'group-001', 'replicaset-001', 'instance-001'))
        end,
        verify_args = {option},
    })
end

-- Verify that template variables are correctly calculated for
-- replicaset peers.
--
-- The configdata module depends on box.cfg(), so we can't run
-- this unit test directly. Let's run it as a script.
g.test_peer = helpers.run_as_script(function()
    local cluster_config = require('internal.config.cluster_config')
    local configdata_lib = require('internal.config.configdata')
    local t = require('myluatest')

    local listen_template = table.concat({'unix/:.', 'var', 'run',
        '{{ group_name }}', '{{ replicaset_name }}', '{{ instance_name }}',
        'tarantool.iproto'}, '/')

    local instance_name = 'instance-001'
    local cconfig = {
        iproto = {
            listen = {{uri = listen_template}},
        },
        groups = {
            ['group-001'] = {
                replicasets = {
                    ['replicaset-001'] = {
                        instances = {
                            ['instance-001'] = {},
                            ['instance-002'] = {},
                        },
                    },
                },
            },
        },
    }
    local iconfig = cluster_config:instantiate(cconfig, instance_name)
    local configdata = configdata_lib.new(iconfig, cconfig, instance_name)

    local function exp_uri(group_name, replicaset_name, instance_name)
        return table.concat({'unix/:./var/run', group_name, replicaset_name,
            instance_name, 'tarantool.iproto'}, '/')
    end

    local opts = {instance = 'instance-002'}
    local res = configdata:get('iproto.listen', opts)[1].uri
    t.assert_equals(res, exp_uri('group-001', 'replicaset-001', 'instance-002'))
end)

-- Verify that template variables are correctly calculated for
-- instances of the same sharding cluster.
--
-- The configdata module depends on box.cfg(), so we can't run
-- this unit test directly. Let's run it as a script.
g.test_sharding = helpers.run_as_script(function()
    local cluster_config = require('internal.config.cluster_config')
    local configdata_lib = require('internal.config.configdata')
    local t = require('myluatest')

    local listen_template = table.concat({'unix/:.', 'var', 'run',
        '{{ group_name }}', '{{ replicaset_name }}', '{{ instance_name }}',
        'tarantool.iproto'}, '/')

    local instance_name = 'router-001'
    local cconfig = {
        credentials = {
            users = {
                guest = {
                    roles = {'sharding'},
                },
            },
        },
        iproto = {
            listen = {{uri = listen_template}},
        },
        groups = {
            ['routers'] = {
                sharding = {
                    roles = {'router'},
                },
                replicasets = {
                    ['routers-a'] = {
                        instances = {
                            ['router-001'] = {},
                            ['router-002'] = {},
                        },
                    },
                },
            },
            ['storages'] = {
                sharding = {
                    roles = {'storage'},
                },
                replicasets = {
                    ['storages-a'] = {
                        instances = {
                            ['storage-a-001'] = {},
                            ['storage-a-002'] = {},
                            ['storage-a-003'] = {},
                        },
                    },
                    ['storages-b'] = {
                        instances = {
                            ['storage-b-001'] = {},
                            ['storage-b-002'] = {},
                            ['storage-b-003'] = {},
                        },
                    },
                },
            },
        },
    }
    local iconfig = cluster_config:instantiate(cconfig, instance_name)
    local configdata = configdata_lib.new(iconfig, cconfig, instance_name)

    local res = configdata:sharding()

    local function exp_uri(group_name, replicaset_name, instance_name)
        return table.concat({'guest@unix/:./var/run', group_name,
            replicaset_name, instance_name, 'tarantool.iproto'}, '/')
    end

    -- Verify URIs of the storages.
    t.assert_equals(res.sharding, {
        ['storages-a'] = {
            master = 'auto',
            replicas = {
                ['storage-a-001'] = {
                    uri = exp_uri('storages', 'storages-a', 'storage-a-001'),
                },
                ['storage-a-002'] = {
                    uri = exp_uri('storages', 'storages-a', 'storage-a-002'),
                },
                ['storage-a-003'] = {
                    uri = exp_uri('storages', 'storages-a', 'storage-a-003'),
                },
            },
            weight = 1,
        },
        ['storages-b'] = {
            master = 'auto',
            replicas = {
                ['storage-b-001'] = {
                    uri = exp_uri('storages', 'storages-b', 'storage-b-001'),
                },
                ['storage-b-002'] = {
                    uri = exp_uri('storages', 'storages-b', 'storage-b-002'),
                },
                ['storage-b-003'] = {
                    uri = exp_uri('storages', 'storages-b', 'storage-b-003'),
                },
            },
            weight = 1,
        },
    })
end)

-- Verify configuration schema constraints.
g.test_context_var_validation = function()
    local function verify(var_def, exp_err)
        local config = {
            config = {
                context = {
                    myvar = var_def,
                },
            },
        }
        t.assert_error_msg_equals(exp_err, instance_config.validate,
            instance_config, config)
    end

    local err_must_be_defined = table.concat({
        '[instance_config] config.context.myvar',
        '"from" field must be defined in a context variable definition',
    }, ': ')

    verify({}, err_must_be_defined)
    verify({env = 'FOO'}, err_must_be_defined)
    verify({file = 'foo'}, err_must_be_defined)
    verify({rstrip = true}, err_must_be_defined)

    verify({from = 'env'}, table.concat({
        '[instance_config] config.context.myvar',
        '"env" field must define an environment variable name if "from" ' ..
            'field is set to "env"',
    }, ': '))
    verify({from = 'file'}, table.concat({
        '[instance_config] config.context.myvar',
        '"file" field must define a file name if "from" field is set to ' ..
            '"file"',
    }, ': '))
end

-- No env variable -> apply error.
g.test_context_var_env_failure = function()
    helpers.failure_case({
        options = {
            ['config.context'] = {
                myvar = {
                    from = 'env',
                    env = 'UNKNOWN_ENV_VAR',
                },
            },
            ['process.title'] = '{{ context.myvar }}'
        },
        exp_err = table.concat({
            'Unable to read config.context.myvar variable value',
            'no "UNKNOWN_ENV_VAR" environment variable',
        }, ': '),
    })
end

-- No file -> apply error.
g.test_context_var_file_failure = function()
    helpers.failure_case({
        options = {
            ['config.context'] = {
                myvar = {
                    from = 'file',
                    file = 'unknown_file.txt',
                },
            },
            ['process.title'] = '{{ context.myvar }}'
        },
        exp_err = table.concat({
            'Unable to read config.context.myvar variable value',
            'Unable to open file',
        }, ': '),
    })
end

-- Verify a config.context variable with 'from: env'.
g.test_context_var_env = function(g)
    helpers.success_case(g, {
        env = {
            ['FOO'] = 'needle',
        },
        options = {
            ['config.context'] = {
                myvar = {
                    from = 'env',
                    env = 'FOO',
                },
            },
            ['process.title'] = '{{ context.myvar }}'
        },
        verify = verify_option('process.title', 'needle'),
    })
end

-- Verify a config.context variable with 'from: file'.
g.test_context_var_file = function(g)
    local dir = treegen.prepare_directory({}, {})
    treegen.write_file(dir, 'foo.txt', 'needle\n')

    helpers.success_case(g, {
        dir = dir,
        options = {
            ['config.context'] = {
                myvar = {
                    from = 'file',
                    file = 'foo.txt',
                },
            },
            ['process.title'] = '{{ context.myvar }}'
        },
        verify = verify_option('process.title', 'needle\n'),
    })
end

-- Verify a config.context variable with 'from: file' and
-- 'rstrip: true'.
g.test_context_var_file_rstrip = function(g)
    local dir = treegen.prepare_directory({}, {})
    treegen.write_file(dir, 'foo.txt', 'needle\n')

    helpers.success_case(g, {
        dir = dir,
        options = {
            ['config.context'] = {
                myvar = {
                    from = 'file',
                    file = 'foo.txt',
                    rstrip = true,
                },
            },
            ['process.title'] = '{{ context.myvar }}'
        },
        verify = verify_option('process.title', 'needle'),
    })
end

-- Verify how file paths are working together with
-- process.work_dir.
g.test_context_var_file_process_work_dir = function(g)
    local dir = treegen.prepare_directory({}, {})
    treegen.write_file(dir, 'foo.txt', 'needle')

    -- foo.txt
    -- w/
    --   var/
    helpers.reload_success_case(g, {
        dir = dir,
        options = {
            ['process.work_dir'] = 'w',
            ['config.context'] = {
                myvar = {
                    from = 'file',
                    file = '../foo.txt',
                },
            },
            ['process.title'] = '{{ context.myvar }}'
        },
        verify = verify_option('process.title', 'needle'),
        verify_2 = verify_option('process.title', 'needle'),
    })

    local dir = treegen.prepare_directory({}, {})
    treegen.write_file(dir, 'w/foo.txt', 'needle')

    -- w/
    --   foo.txt
    --   var/
    helpers.reload_success_case(g, {
        dir = dir,
        options = {
            ['process.work_dir'] = 'w',
            ['config.context'] = {
                myvar = {
                    from = 'file',
                    file = 'foo.txt',
                },
            },
            ['process.title'] = '{{ context.myvar }}'
        },
        verify = verify_option('process.title', 'needle'),
        verify_2 = verify_option('process.title', 'needle'),
    })
end

local g_context_file = helpers.group('context_file_paths', {
    {parent = 'symlink', decoy = true},
    {parent = 'symlink'},
    {parent = 'symlink', workdir_context = true},
    {parent = 'symlink', absolute_file = true},
    {parent = 'symlink', missing_file_parent = true},
    {parent = 'symlink', missing_workdir = true},
    {parent = 'missing'},
    {parent = 'mixed'},
})

-- Verify context files follow physical paths on startup and reload.
g_context_file.test_startup_and_reload = function(cg)
    local dir = treegen.prepare_directory({}, {})
    local workdirs = {
        symlink = 'link/../wd',
        missing = 'missing/../wd',
        mixed = 'link/../missing/inner/../../wd',
    }
    local work_dir = workdirs[cg.params.parent]
    local physical_dir = 'wd'
    if cg.params.parent ~= 'missing' then
        treegen.write_file(dir, 'target/child/.keep', '')
        t.assert(fio.symlink('target/child', dir .. '/link'))
        physical_dir = 'target/wd'
    end
    local marker_path = physical_dir .. '/marker.txt'
    local context_file = 'marker.txt'
    if cg.params.missing_workdir then
        marker_path = 'target/marker.txt'
        context_file = '../marker.txt'
    elseif cg.params.missing_file_parent then
        context_file = 'missing/../marker.txt'
    elseif cg.params.absolute_file then
        context_file = dir .. '/' .. work_dir .. '/marker.txt'
    end
    treegen.write_file(dir, marker_path, 'physical\n')
    if cg.params.decoy then
        treegen.write_file(dir, 'wd/marker.txt', 'lexical\n')
    end
    local context = {
        marker = {from = 'file', file = context_file, rstrip = true},
    }
    if cg.params.workdir_context then
        context.wd = work_dir
        work_dir = '{{ context.wd }}'
    end
    local script = treegen.write_file(dir, 'main.lua', [[
        if rawget(_G, 'reload_started') then return end
        rawset(_G, 'reload_started', true)
        local config = require('config')
        local function check()
            assert(config:get('process.title') == 'physical')
            assert(config:get('process.title', {instance = 'instance-001'})
                   == 'physical')
            assert(require('fio').cwd() == os.getenv('EXPECTED_WORKDIR'))
        end
        check()
        require('fiber').new(function()
            config:wait_status('ready')
            config:reload()
            check()
            assert(#config:info().alerts == 0)
            print('physical context file survives reload')
            os.exit(0)
        end)
    ]])
    local config = table.deepcopy(helpers.simple_config)
    config.iproto = nil
    config.process = {work_dir = work_dir, title = '{{ context.marker }}'}
    config.config = {context = context}
    config.app = {file = script}
    treegen.write_file(dir, 'config.yaml', yaml.encode(config))
    local res = justrun.tarantool(dir, {
        EXPECTED_WORKDIR = dir .. '/' .. physical_dir,
    }, {'--name', 'instance-001', '--config', 'config.yaml'},
        {nojson = true, stderr = true, setsearchroot = false})
    t.assert_equals(res.exit_code, 0, res.stderr)
    t.assert_str_contains(res.stdout, 'physical context file survives reload')
    t.assert(fio.path.is_dir(dir .. '/' .. physical_dir))
end

-- Verify early workdir resolution reads only the required context values.
g.test_resolve_work_dir = helpers.run_as_script(function()
    local instance_config = require('internal.config.instance_config')
    local t = require('myluatest')
    t.assert_equals(instance_config:resolve_work_dir({}, {}), nil)
    for _, value in ipairs({'wd', '', 42, true, false}) do
        local config = {
            config = {context = {wd = value}},
            process = {work_dir = '{{ context.wd }}/{{ instance_name }}'},
        }
        local vars = {instance_name = 'instance-001'}
        local result = instance_config:resolve_work_dir(config, vars)
        t.assert_equals(result, tostring(value) .. '/instance-001')
        t.assert_equals(vars['context.wd'], tostring(value))
    end
    local config = {
        config = {context = {
            wd = 'wd',
            unused = {from = 'file', file = 'missing.txt'},
        }},
        process = {work_dir = '{{ context.wd }}'},
    }
    local vars = {}
    t.assert_equals(instance_config:resolve_work_dir(config, vars), 'wd')
    t.assert_equals(vars['context.unused'], nil)
    t.assert_error_msg_contains('Unable to read config.context.unused',
                               instance_config.apply_vars,
                               instance_config, config, vars)

    for _, template in ipairs({'{{ unknown }}', '{{ context.unknown }}'}) do
        config.process.work_dir = template
        t.assert_equals(instance_config:resolve_work_dir(config, {}), template)
    end
    config.config.context = {
        wd = {from = 'file', file = 'workdir.txt'},
    }
    config.process.work_dir = '{{ context.wd }}'
    vars = {}
    t.assert_equals(instance_config:resolve_work_dir(config, vars),
                    '{{ context.wd }}')
    t.assert_equals(vars['context.wd'], nil)

    config.config.context.wd = '{{ instance_name }}'
    vars = {instance_name = 'instance-001'}
    t.assert_equals(instance_config:resolve_work_dir(config, vars),
                    '{{ instance_name }}')
    t.assert_equals(instance_config:apply_vars(config, vars).process.work_dir,
                    '{{ instance_name }}')
end)

-- Verify one config build reuses raw context values across instance views.
g.test_context_cache = helpers.run_as_script(function()
    local fio = require('fio')
    local instance_config = require('internal.config.instance_config')
    local t = require('myluatest')
    for _, source in ipairs({'env', 'file', 'symlink'}) do
        local revision = 0
        local function set_value(value)
            if source == 'env' then
                os.setenv('TEST_CONTEXT_CACHE', value)
            else
                revision = revision + 1
                local path = source == 'symlink' and
                    ('context-%d.txt'):format(revision) or 'context.txt'
                local f = assert(io.open(path, 'w'))
                f:write(value)
                f:close()
                if source == 'symlink' then
                    if fio.path.lexists('context.txt') then
                        assert(fio.unlink('context.txt'))
                    end
                    assert(fio.symlink(path, 'context.txt'))
                end
            end
        end
        set_value('wd\n')
        local definition = {
            from = source == 'env' and 'env' or 'file', rstrip = true,
        }
        if source == 'env' then
            definition.env = 'TEST_CONTEXT_CACHE'
        else
            definition.file = fio.cwd() .. '/context.txt'
        end
        local config = {
            config = {context = {wd = definition}},
            process = {
                work_dir = '{{ context.wd }}',
                title = '{{ instance_name }}',
            },
        }
        local cache = {}
        local vars = {instance_name = 'current'}
        t.assert_equals(instance_config:resolve_work_dir(config, vars, cache),
                        'wd')
        set_value('changed\n')
        local current = instance_config:apply_vars(config, vars, cache)
        t.assert_equals(current.process.work_dir, 'wd')
        local peer = table.deepcopy(config)
        peer.config.context.wd.rstrip = false
        if source ~= 'env' then
            peer.config.context.wd.file = fio.cwd() .. '/./context.txt'
        end
        local peer_vars = {instance_name = 'peer'}
        local result = instance_config:apply_vars(peer, peer_vars, cache)
        t.assert_equals(result.process.work_dir, 'wd\n')
        t.assert_equals(result.process.title, 'peer')
        peer.config.context.wd = 'peer-workdir'
        result = instance_config:apply_vars(peer, peer_vars, cache)
        t.assert_equals(result.process.work_dir, 'peer-workdir')
        result = instance_config:apply_vars(config, peer_vars, {})
        t.assert_equals(result.process.work_dir, 'changed')
    end
end)

-- Verify missing workdir context sources report their read errors.
g.test_work_dir_context_errors = function()
    local dir = treegen.prepare_directory({}, {})
    local cases = {
        {
            definition = {from = 'env', env = 'UNKNOWN_WORKDIR_ENV_VAR'},
            error = 'no "UNKNOWN_WORKDIR_ENV_VAR" environment variable',
        },
        {
            definition = {from = 'file', file = dir .. '/missing.txt'},
            error = 'Unable to open file',
        },
    }
    for _, case in ipairs(cases) do
        helpers.failure_case({
            dir = dir,
            options = {
                ['process.work_dir'] = '{{ context.wd }}',
                ['config.context'] = {wd = case.definition},
            },
            exp_err = 'Unable to read config.context.wd variable value: ' ..
                case.error,
        })
    end
end

-- Verify a config.context variable with a scalar value.
g.test_context_var_scalar = function(g)
    helpers.success_case(g, {
        options = {
            ['config.context'] = {
                myvar = 'needle',
            },
            ['process.title'] = '{{ context.myvar }}',
        },
        verify = verify_option('process.title', 'needle'),
    })
end

-- Scalar number value.
g.test_context_var_scalar_number = function(g)
    helpers.success_case(g, {
        options = {
            ['config.context'] = { myvar = 42 },
            ['process.title'] = '{{ context.myvar }}',
        },
        verify = verify_option('process.title', '42'),
    })
end

-- Scalar boolean value.
g.test_context_var_scalar_boolean = function(g)
    helpers.success_case(g, {
        options = {
            ['config.context'] = { myvar = true },
            ['process.title'] = '{{ context.myvar }}',
        },
        verify = verify_option('process.title', 'true'),
    })
end

-- Scalar empty string value.
g.test_context_var_scalar_empty_string = function(g)
    helpers.success_case(g, {
        options = {
            ['config.context'] = { myvar = '' },
            ['process.title'] = 'prefix-{{ context.myvar }}-suffix',
        },
        verify = verify_option('process.title', 'prefix--suffix'),
    })
end

-- Mixed sources: env + file + scalar.
g.test_context_var_mixed_sources = function(g)
    local dir = treegen.prepare_directory({}, {})
    treegen.write_file(dir, 'foo.txt', 'fromfile')

    helpers.success_case(g, {
        dir = dir,
        env = { MY_ENV_VAR = 'fromenv' },
        options = {
            ['config.context'] = {
                from_env = { from = 'env', env = 'MY_ENV_VAR' },
                from_file = { from = 'file', file = 'foo.txt' },
                from_value = 'fromvalue',
            },
            ['process.title'] = '{{ context.from_env }}:' ..
                '{{ context.from_file }}:{{ context.from_value }}',
        },
        verify = verify_option('process.title', 'fromenv:fromfile:fromvalue'),
    })
end

-- Same context variable used in multiple options.
g.test_context_var_scalar_in_multiple_options = function(g)
    helpers.success_case(g, {
        options = {
            ['config.context'] = { myvar = 'needle' },
            ['process.title'] = '{{ context.myvar }}',
            ['process.pid_file'] = '{{ context.myvar }}.pid',
        },
        verify = function()
            local t = require('luatest')
            local config = require('config')
            t.assert_equals(config:get('process.title'), 'needle')
            t.assert_equals(config:get('process.pid_file'), 'needle.pid')
        end,
    })
end

-- Reload config with changed scalar value.
g.test_context_var_scalar_reload = function(g)
    helpers.reload_success_case(g, {
        options = {
            ['config.context'] = { myvar = 'before' },
            ['process.title'] = '{{ context.myvar }}',
        },
        verify = verify_option('process.title', 'before'),
        options_2 = {
            ['config.context'] = { myvar = 'after' },
            ['process.title'] = '{{ context.myvar }}',
        },
        verify_2 = verify_option('process.title', 'after'),
    })
end

-- Verify that process.title matches the expected value.
local function assert_process_title(server, exp)
    server:exec(function(exp)
        local t = require('luatest')
        local config = require('config')
        t.assert_equals(config:get('process.title'), exp)
    end, {exp})
end

-- Verify config.context at different hierarchy levels.
g.test_context_var_hierarchical = function(g)
    local dir = treegen.prepare_directory({}, {})
    local config = [[
        credentials:
          users:
            guest:
              roles:
              - super
        iproto:
          listen:
            - uri: 'unix/:./{{ instance_name }}.iproto'
        config:
          context:
            host: global.example.com
        groups:
          group-001:
            config:
              context:
                host: group.example.com
            replicasets:
              replicaset-001:
                instances:
                  instance-001:
                    database:
                      mode: rw
                    process:
                      title: '{{ context.host }}'
                  instance-002:
                    config:
                      context:
                        host: instance.example.com
                    process:
                      title: '{{ context.host }}'
          group-002:
            replicasets:
              replicaset-002:
                instances:
                  instance-003:
                    database:
                      mode: rw
                    process:
                      title: '{{ context.host }}'
    ]]
    local config_file = treegen.write_file(dir, 'config.yaml', config)

    g.server_1 = server:new({
        config_file = config_file,
        chdir = dir,
        alias = 'instance-001',
    })
    g.server_2 = server:new({
        config_file = config_file,
        chdir = dir,
        alias = 'instance-002',
    })
    g.server_3 = server:new({
        config_file = config_file,
        chdir = dir,
        alias = 'instance-003',
    })
    g.server_1:start({wait_until_ready = false})
    g.server_2:start({wait_until_ready = false})
    g.server_3:start({wait_until_ready = false})
    g.server_1:wait_until_ready()
    g.server_2:wait_until_ready()
    g.server_3:wait_until_ready()

    -- instance-001: inherited from group-001.
    assert_process_title(g.server_1, 'group.example.com')
    -- instance-002: overridden at instance level.
    assert_process_title(g.server_2, 'instance.example.com')
    -- instance-003: fallback to global.
    assert_process_title(g.server_3, 'global.example.com')
end

-- Verify the use case from the issue: a group-level template
-- in iproto.listen URI using {{ context.<name> }} with
-- per-instance config.context values.
g.test_context_var_per_instance_ports = function()
    local dir = treegen.prepare_directory({}, {})
    local config = [[
        credentials:
          users:
            guest:
              roles:
              - super
        config:
          context:
            port: 13301
        groups:
          group-001:
            iproto:
              listen:
                - uri: 'unix/:./{{ context.port }}.iproto'
            replicasets:
              replicaset-001:
                instances:
                  instance-001:
                    config:
                      context:
                        port: 13302
    ]]
    treegen.write_file(dir, 'config.yaml', config)
    local script = [[
        local config = require('config')
        local yaml = require('yaml')
        print(yaml.encode(config:get('iproto.listen')))
        os.exit(0)
    ]]
    treegen.write_file(dir, 'main.lua', script)
    local env = {TT_LOG_LEVEL = 0}
    local opts = {nojson = true, stderr = true}

    -- Verify instance-001: port from its own extras.
    local args = {'--name', 'instance-001', '--config',
                   'config.yaml', 'main.lua'}
    local res = justrun.tarantool(dir, env, args, opts)
    t.assert_equals(res.exit_code, 0, res.stderr)
    t.assert_equals(yaml.decode(res.stdout), {{uri = 'unix/:./13302.iproto'}})
end
