local fun = require('fun')
local yaml = require('yaml')
local t = require('luatest')
local treegen = require('test.treegen')
local server = require('test.luatest_helpers.server')
local helpers = require('test.config-luatest.helpers')

local g = helpers.group()

-- Verify that `--config <...>` with a relative path to the config
-- works good with `process.work_dir` option, which changes CWD of
-- the instance.
g.test_relative_config_path = function(g)
    local dir = treegen.prepare_directory(g, {}, {})
    local config = table.copy(helpers.simple_config)
    config.process = {
        work_dir = 'x',
    }
    local config_file = 'config.yaml'
    treegen.write_script(dir, config_file, yaml.encode(config))

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
