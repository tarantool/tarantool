local t = require('luatest')
local fun = require('fun')
local json = require('json')
local yaml = require('yaml')
local treegen = require('test.treegen')
local justrun = require('test.justrun')
local source_file = require('internal.config.source.file').new()
local server = require('test.luatest_helpers.server')

local g = t.group()

g.before_all(function()
    treegen.init(g)
end)

g.after_all(function()
    treegen.clean(g)
end)

g.after_each(function(g)
    if g.server ~= nil then
        g.server:stop()
        g.server = nil
    end
end)

g.test_source_file = function()
    local config = {_config_file = 'doc/examples/config/single.yaml'}
    source_file:sync(config, {})
    local res = source_file:get()
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
        local source_env = require('internal.config.source.env').new({
            env_var_suffix = arg[1],
        })
        source_env:sync({}, {})
        print(json.encode(source_env:get()))
    ]]
    treegen.write_script(dir, 'main.lua', script)

    local exp = {
        log = {
            level = 'info',
        },
        memtx = {
            memory = 1000000
        },
    }

    local cases = {
        {
            name = 'env',
            env_var_suffix = nil,
            env = {
                TT_LOG_LEVEL = 'info',
                TT_MEMTX_MEMORY = 1000000,
            },
        },
        {
            name = 'env default',
            env_var_suffix = 'default',
            env = {
                TT_LOG_LEVEL_DEFAULT = 'info',
                TT_MEMTX_MEMORY_DEFAULT = 1000000,
            },
        },
    }
    local opts = {nojson = true, stderr = false}
    for _, case in ipairs(cases) do
        local comment = ('case: %s'):format(case.name)
        local args = {'main.lua', case.env_var_suffix}
        local res = justrun.tarantool(dir, case.env, args, opts)
        t.assert_equals(res.exit_code, 0, comment)
        t.assert_equals(json.decode(res.stdout), exp, comment)
    end
end

-- Verify priority of configuration sources.
--
-- 1. env (TT_*)
-- 2. file
-- 3. env default (TT_*_DEFAULT)
--
-- Several string options from the instance config are chosen for
-- testing purposes, their meaning is irrelevant for the test.
--
-- The table below shows where the given option is set (which
-- source defines it) and what we expect as a result.
--
-- | option              | env | file | env default | result      |
-- | ------------------- | --- | ---- | ----------- | ----------- |
-- | process.title       |     |  +   |      +      | file        |
-- | log.file            |  +  |  +   |             | env         |
-- | log.pipe            |  +  |      |      +      | env         |
-- | log.syslog.identity |     |      |      +      | env default |
g.test_sources_priority = function(g)
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
        process = {
            title = 'from file',
        },
        log = {
            file = 'from file',
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
        chdir = dir,
        env = {
            TT_PROCESS_TITLE_DEFAULT = 'from env default',
            TT_LOG_FILE = 'from env',
            TT_LOG_PIPE = 'from env',
            TT_LOG_PIPE_DEFAULT = 'from env default',
            TT_LOG_SYSLOG_IDENTITY_DEFAULT = 'from env default',
        },
    }
    g.server = server:new(fun.chain(opts, {alias = 'instance-001'}):tomap())
    g.server:start()
    g.server:exec(function()
        local config = require('config')
        t.assert_equals(config:get('process.title'), 'from file')
        t.assert_equals(config:get('log.file'), 'from env')
        t.assert_equals(config:get('log.pipe'), 'from env')
        t.assert_equals(config:get('log.syslog.identity'), 'from env default')
    end)
end
