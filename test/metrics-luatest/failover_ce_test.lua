require('test.metrics-luatest.helper')

local t = require('luatest')
local metrics = require('metrics')
local failover = require('metrics.tarantool.failover')
local rock_utils = require('test.metrics-luatest.test.rock_utils')
local g = t.group('failover-ce')

g.after_each(function()
    failover.set_source(nil)
    metrics.clear()
end)

-- The collector must be available without an external metrics installation.
g.test_builtin = function()
    rock_utils.assert_builtin('metrics.tarantool.failover')
    t.assert_type(failover.set_source, 'function')
end

-- Enabling failover in plain CE must not import or start the EE coordinator.
g.test_no_source = function()
    t.tarantool.skip_if_enterprise()
    local justrun = require('luatest.justrun')
    -- A subprocess keeps box.cfg untouched by the CE test-runner helper.
    local result = justrun.tarantool('.', {}, {'-e', [[
        local t = require('luatest')
        local rock_utils = require('test.metrics-luatest.test.rock_utils')
        rock_utils.remove_override('metrics')
        t.assert_type(box.cfg, 'function')
        local original_require = _G.require
        local imports = {}
        _G.require = function(name)
            table.insert(imports, name)
            return original_require(name)
        end
        local ok, err = pcall(function()
            local metrics = require('metrics')
            rock_utils.assert_builtin('metrics.tarantool.failover')
            metrics.clear()
            metrics.cfg{include = {'failover'}, exclude = {}, labels = {}}
            t.assert_equals(metrics.collect{invoke_callbacks = true}, {})
            metrics.cfg{include = {'failover'}, exclude = {'failover'}}
            t.assert_equals(metrics.collect{invoke_callbacks = true}, {})
        end)
        _G.require = original_require
        t.assert(ok, err)
        for _, name in ipairs(imports) do
            t.assert_not_equals(name, 'failover')
            t.assert_not(name:startswith('failover.'))
        end
        for name in pairs(package.loaded) do
            t.assert_not_equals(name, 'failover')
            t.assert_not(name:startswith('failover.'))
        end
    ]]}, {nojson = true, stderr = true, quote_args = true})
    t.assert_equals(result.exit_code, 0, result.stderr)
end
