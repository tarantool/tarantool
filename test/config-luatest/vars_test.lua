local instance_config = require('internal.config.instance_config')
local t = require('luatest')
local treegen = require('test.treegen')
local justrun = require('test.justrun')
local helpers = require('test.config-luatest.helpers')

local g = helpers.group()

-- Write the given function into a Lua file and execute it.
--
-- Ensure that the exit code is zero and there is no stdout/stderr
-- output.
--
-- Use require('myluatest') instead of require('luatest') inside
-- the test case to get better diagnostics at failure.
local function run_as_script(f)
    return function(g)
        local dir = treegen.prepare_directory(g, {}, {})
        treegen.write_script(dir, 'myluatest.lua', string.dump(function()
            local t = require('luatest')

            -- Luatest raises a table as an error. If the error is
            -- not caught, it looks like the following on stderr.
            --
            -- LuajitError: table: 0x41b2ce08
            -- fatal error, exiting the event loop
            --
            -- Moreover, if the message is too long it is
            -- truncated at converting to box error. See
            -- DIAG_ERRMSG_MAX in src/lib/core/diag.h, it is 512
            -- at the time of writing.
            --
            -- Let's write the message on stderr and re-raise a
            -- short message instead.
            local saved_assert_equals = t.assert_equals
            t.assert_equals = function(...)
                local ok, err = pcall(saved_assert_equals, ...)
                if ok then
                    return
                end
                if type(err) == 'table' and type(err.message) == 'string' then
                    err = err.message
                end
                io.stderr:write(err .. '\n')
                error('See stderr output above', 2)
            end

            return t
        end))
        treegen.write_script(dir, 'main.lua', string.dump(f))

        local opts = {nojson = true, stderr = true}
        local res = justrun.tarantool(dir, {}, {'main.lua'}, opts)
        t.assert_equals(res, {
            exit_code = 0,
            stdout = '',
            stderr = '',
        })
    end
end

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
g.test_peer = run_as_script(function()
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

    local res = configdata:get('iproto.listen', {peer = 'instance-002'})[1].uri
    t.assert_equals(res, exp_uri('group-001', 'replicaset-001', 'instance-002'))
end)

-- Verify that template variables are correctly calculated for
-- instances of the same sharding cluster.
--
-- The configdata module depends on box.cfg(), so we can't run
-- this unit test directly. Let's run it as a script.
g.test_sharding = run_as_script(function()
    local cluster_config = require('internal.config.cluster_config')
    local configdata_lib = require('internal.config.configdata')
    local t = require('myluatest')

    local listen_template = table.concat({'unix/:.', 'var', 'run',
        '{{ group_name }}', '{{ replicaset_name }}', '{{ instance_name }}',
        'tarantool.iproto'}, '/')

    local instance_name = 'router-001'
    local cconfig = {
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
g.test_context_var_env_failure = function(g)
    helpers.failure_case(g, {
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
g.test_context_var_file_failure = function(g)
    helpers.failure_case(g, {
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
    local dir = treegen.prepare_directory(g, {}, {})
    treegen.write_script(dir, 'foo.txt', 'needle\n')

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
    local dir = treegen.prepare_directory(g, {}, {})
    treegen.write_script(dir, 'foo.txt', 'needle\n')

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
    local dir = treegen.prepare_directory(g, {}, {})
    treegen.write_script(dir, 'foo.txt', 'needle')

    -- foo.txt
    -- w/
    --   var/
    helpers.success_case(g, {
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

    local dir = treegen.prepare_directory(g, {}, {})
    treegen.write_script(dir, 'w/foo.txt', 'needle')

    -- w/
    --   foo.txt
    --   var/
    helpers.success_case(g, {
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
