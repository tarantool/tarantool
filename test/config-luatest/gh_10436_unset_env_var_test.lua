local t = require('luatest')
local yaml = require('yaml')
local treegen = require('luatest.treegen')
local justrun = require('luatest.justrun')
local cluster_config = require('internal.config.cluster_config')
local helpers = require('test.config-luatest.helpers')

local g = helpers.group()

-- Verify the given configuration option.
local function verify_option(option, exp)
    return loadstring(([[
        local t = require('luatest')
        local config = require('config')

        t.assert_equals(config:info().status, 'ready')
        t.assert_equals(config:get(%q), %q)
    ]]):format(option, exp))
end

-- Unset env var of an unreferenced context variable must not
-- prevent startup.
g.test_unused_unset_env_context_var_starts = function(g)
    helpers.success_case(g, {
        options = {
            ['config.context'] = {
                myvar = {
                    from = 'env',
                    env = 'MYVAR_UNSET_ENV',
                },
            },
        },
        verify = function()
            local config = require('config')
            t.assert_equals(config:info().status, 'ready')
        end,
    })
end

-- The exact ticket config: two unreferenced env context vars, both
-- unset. The instance must start.
g.test_ticket_config_two_unused_unset_env_vars = function(g)
    helpers.success_case(g, {
        options = {
            ['config.context'] = {
                dbadmin_password = {
                    from = 'env',
                    env = 'DBADMIN_PASSWORD',
                },
                sampleuser_password = {
                    from = 'env',
                    env = 'SAMPLEUSER_PASSWORD',
                },
            },
        },
        verify = function()
            local config = require('config')
            t.assert_equals(config:info().status, 'ready')
        end,
    })
end

-- An unreferenced unset env context var must produce a warning. The
-- warning is logged before box.cfg(), so it goes to stderr (not the
-- box log file); run the instance via justrun and grep its stderr.
g.test_unused_unset_env_context_var_warns = function()
    local dir = treegen.prepare_directory({}, {})
    local config = table.deepcopy(helpers.simple_config)
    cluster_config:set(config, 'config.context', {
        myvar = {from = 'env', env = 'MYVAR_UNSET_ENV'},
    })
    cluster_config:set(config, 'app.file', 'main.lua')
    treegen.write_file(dir, 'config.yaml', yaml.encode(config))
    -- The instance starts successfully; exit right after so that
    -- justrun collects the full stderr and returns.
    treegen.write_file(dir, 'main.lua', 'os.exit(0)')

    local opts = {nojson = true, stderr = true}
    local res = justrun.tarantool(dir, {},
        {'--name', 'instance-001', '--config', 'config.yaml'}, opts)
    t.assert_equals(res.exit_code, 0)
    t.assert_str_contains(res.stderr, 'config.context.myvar: cannot read ' ..
        'the variable value, it is left unset')
end

-- A referenced context var with an unset env var must still fail
-- startup (existing error contract).
g.test_referenced_unset_env_context_var_fails = function()
    helpers.failure_case({
        options = {
            ['config.context'] = {
                myvar = {
                    from = 'env',
                    env = 'MYVAR_UNSET_ENV',
                },
            },
            ['process.title'] = '{{ context.myvar }}',
        },
        exp_err = table.concat({
            'Unable to read config.context.myvar variable value',
            'no "MYVAR_UNSET_ENV" environment variable',
        }, ': '),
    })
end

-- A referenced context var with a set env var is substituted.
g.test_referenced_set_env_context_var_works = function(g)
    helpers.success_case(g, {
        env = {
            ['MYVAR_SET_ENV'] = 'needle',
        },
        options = {
            ['config.context'] = {
                myvar = {
                    from = 'env',
                    env = 'MYVAR_SET_ENV',
                },
            },
            ['process.title'] = '{{ context.myvar }}',
        },
        verify = verify_option('process.title', 'needle'),
    })
end

-- A referenced env var with rstrip = true is resolved and stripped.
g.test_referenced_set_env_context_var_rstrip = function(g)
    helpers.success_case(g, {
        env = {
            ['MYVAR_SET_ENV'] = 'needle   ',
        },
        options = {
            ['config.context'] = {
                myvar = {
                    from = 'env',
                    env = 'MYVAR_SET_ENV',
                    rstrip = true,
                },
            },
            ['process.title'] = '{{ context.myvar }}',
        },
        verify = verify_option('process.title', 'needle'),
    })
end

-- Lazy resolution: start with the env var unset and unreferenced,
-- then set it and reference it on reload -- reload must succeed.
g.test_lazy_env_context_var_set_before_reference = function(g)
    helpers.reload_success_case(g, {
        options = {
            ['config.context'] = {
                myvar = {
                    from = 'env',
                    env = 'MYVAR_LAZY_ENV',
                },
            },
        },
        verify = function()
            local config = require('config')
            t.assert_equals(config:info().status, 'ready')
            os.setenv('MYVAR_LAZY_ENV', 'needle')
        end,
        options_2 = {
            ['config.context'] = {
                myvar = {
                    from = 'env',
                    env = 'MYVAR_LAZY_ENV',
                },
            },
            ['process.title'] = '{{ context.myvar }}',
        },
        verify_2 = verify_option('process.title', 'needle'),
    })
end

-- A missing file of an unreferenced 'file' context var must not
-- prevent startup (same relaxation as for unset env vars).
g.test_unused_missing_file_context_var_starts = function(g)
    helpers.success_case(g, {
        options = {
            ['config.context'] = {
                myvar = {
                    from = 'file',
                    file = 'no_such_file.txt',
                },
            },
        },
        verify = function()
            local config = require('config')
            t.assert_equals(config:info().status, 'ready')
        end,
    })
end

-- An unreferenced unreadable 'file' context var must warn on stderr
-- (logged before box.cfg(), so use justrun; see the env case above).
g.test_unused_missing_file_context_var_warns = function()
    local dir = treegen.prepare_directory({}, {})
    local config = table.deepcopy(helpers.simple_config)
    cluster_config:set(config, 'config.context', {
        myvar = {from = 'file', file = 'no_such_file.txt'},
    })
    cluster_config:set(config, 'app.file', 'main.lua')
    treegen.write_file(dir, 'config.yaml', yaml.encode(config))
    treegen.write_file(dir, 'main.lua', 'os.exit(0)')

    local opts = {nojson = true, stderr = true}
    local res = justrun.tarantool(dir, {},
        {'--name', 'instance-001', '--config', 'config.yaml'}, opts)
    t.assert_equals(res.exit_code, 0)
    t.assert_str_contains(res.stderr, 'config.context.myvar: cannot read ' ..
        'the variable value, it is left unset')
end

-- A referenced 'file' context var with a missing file must still
-- fail startup (existing error contract).
g.test_referenced_missing_file_context_var_fails = function()
    helpers.failure_case({
        options = {
            ['config.context'] = {
                myvar = {
                    from = 'file',
                    file = 'no_such_file.txt',
                },
            },
            ['process.title'] = '{{ context.myvar }}',
        },
        exp_err = 'Unable to read config.context.myvar variable value: ' ..
            'Unable to open file',
    })
end
