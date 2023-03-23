require('test.metrics-luatest.helper')

local t = require('luatest')
local g = t.group('box-cfg-metrics')
local server = require('luatest.server')

local utils = require('third_party.metrics.test.utils')

g.before_all(function()
    g.server = server:new()
    g.server:start()
end)

g.after_all(function()
    g.server:drop()
end)

g.before_each(function()
    g.server:exec(function()
        box.cfg{
            metrics = {
                include = 'all',
                exclude = {},
                labels = {},
            }
        }
    end)
end)

g.test_include = function()
    local default_metrics = g.server:exec(function()
        box.cfg{
            metrics = {
                include = {'info'},
            }
        }

        return require('metrics').collect{invoke_callbacks = true}
    end)

    local uptime = utils.find_metric('tnt_info_uptime', default_metrics)
    t.assert_not_equals(uptime, nil)
    local memlua = utils.find_metric('tnt_info_memory_lua', default_metrics)
    t.assert_equals(memlua, nil)
end

g.test_exclude = function()
    local default_metrics = g.server:exec(function()
        box.cfg{
            metrics = {
                exclude = {'memory'},
            }
        }

        return require('metrics').collect{invoke_callbacks = true}
    end)

    local uptime = utils.find_metric('tnt_info_uptime', default_metrics)
    t.assert_not_equals(uptime, nil)
    local memlua = utils.find_metric('tnt_info_memory_lua', default_metrics)
    t.assert_equals(memlua, nil)
end

g.test_include_with_exclude = function()
    local default_metrics = g.server:exec(function()
        box.cfg{
            metrics = {
                include = {'info', 'memory'},
                exclude = {'memory'},
            }
        }

        return require('metrics').collect{invoke_callbacks = true}
    end)

    local uptime = utils.find_metric('tnt_info_uptime', default_metrics)
    t.assert_not_equals(uptime, nil)
    local memlua = utils.find_metric('tnt_info_memory_lua', default_metrics)
    t.assert_equals(memlua, nil)
end

g.test_include_none = function()
    local default_metrics = g.server:exec(function()
        box.cfg{
            metrics = {
                include = 'none',
                exclude = {'memory'},
            }
        }

        return require('metrics').collect{invoke_callbacks = true}
    end)

    t.assert_equals(default_metrics, {})
end

g.test_labels = function()
    local default_metrics = g.server:exec(function()
        box.cfg{
            metrics = {
                labels = {mylabel = 'myvalue'}
            }
        }

        return require('metrics').collect{invoke_callbacks = true}
    end)

    local uptime = utils.find_obs('tnt_info_uptime', {mylabel = 'myvalue'},
                                  default_metrics)
    t.assert_equals(uptime.label_pairs, {mylabel = 'myvalue'})

    default_metrics = g.server:exec(function()
        box.cfg{
            metrics = {
                labels = {}
            }
        }

        return require('metrics').collect{invoke_callbacks = true}
    end)

    uptime = utils.find_obs('tnt_info_uptime', {}, default_metrics)
    t.assert_equals(uptime.label_pairs, {})
end
