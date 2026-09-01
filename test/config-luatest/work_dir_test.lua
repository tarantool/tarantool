local fun = require('fun')
local fio = require('fio')
local yaml = require('yaml')
local t = require('luatest')
local treegen = require('luatest.treegen')
local justrun = require('luatest.justrun')
local server = require('luatest.server')
local helpers = require('test.config-luatest.helpers')

local g = helpers.group()

-- Verify that `--config <...>` with a relative path to the config
-- works good with `process.work_dir` option, which changes CWD of
-- the instance.
g.test_relative_config_path = function(g)
    local dir = treegen.prepare_directory({}, {})
    local config = table.copy(helpers.simple_config)
    config.process = {
        work_dir = 'x',
    }
    local config_file = 'config.yaml'
    treegen.write_file(dir, config_file, yaml.encode(config))

    -- Important: `--config <...>` is passed with a relative path.
    local opts = {config_file = config_file, chdir = dir}
    g.server = server:new(fun.chain(opts, {alias = 'instance-001'}):tomap())
    g.server:start()

    local function server_is_ok(server)
        server:exec(function()
            local config = require('config')

            local info = config:info()
            t.assert_equals({
                status = info.status,
                alerts = info.alerts,
            }, {
                status = 'ready',
                alerts = {},
            })
        end)
    end

    -- Verify that the instance is started successfully. Reload
    -- the configuration. Verify that the reload is successful.
    server_is_ok(g.server)
    g.server:exec(function()
        local config = require('config')

        config:reload()
    end)
    server_is_ok(g.server)
end

-- An explicitly pinned module search root is respected:
-- process.work_dir must not override it. For example, luatest
-- pins the search root for the test servers it spawns and the
-- luatest module itself may be resolvable only through it.
g.test_pinned_searchroot = function()
    local dir = treegen.prepare_directory({}, {})
    treegen.write_file(dir, 'pinned/.rocks/share/tarantool/mymod.lua',
                       'return {}')
    treegen.write_file(dir, 'wd/main.lua', [[
        require('mymod')
        print('mymod is loaded')
        os.exit(0)
    ]])
    local config = [[
    process:
      work_dir: wd

    app:
      file: main.lua

    groups:
      group-001:
        replicasets:
          replicaset-001:
            instances:
              instance-001: {}
    ]]
    treegen.write_file(dir, 'config.yaml', config)

    -- LUA_PATH is emptied to not resolve anything from the
    -- testing environment: mymod is resolvable only through the
    -- pinned search root.
    local res = justrun.tarantool(dir, {LUA_PATH = ''}, {
        '-e', ('package.setsearchroot(%q)'):format(
            fio.pathjoin(dir, 'pinned')),
        '--name', 'instance-001', '--config', 'config.yaml',
    }, {nojson = true, stderr = true, quote_args = true,
        setsearchroot = false})
    t.assert_equals(res.exit_code, 0, res.stderr)
    t.assert_str_contains(res.stdout, 'mymod is loaded')
end
