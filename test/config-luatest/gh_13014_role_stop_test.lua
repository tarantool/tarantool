local t = require('luatest')
local helpers = require('test.config-luatest.helpers')

local g = helpers.group()

-- Source code of a role that counts its apply() and stop() calls in
-- the _G[<name>_counts] table with the 'apply' and 'stop' fields.
local function counting_role(name)
    return ([[
        local counts = {apply = 0, stop = 0}
        rawset(_G, %q, counts)

        return {
            validate = function() end,
            apply = function()
                counts.apply = counts.apply + 1
            end,
            stop = function()
                counts.stop = counts.stop + 1
            end,
        }
    ]]):format(name .. '_counts')
end

-- Start a server with the single 'one' role and reload the
-- configuration with the role removed from it.
local function remove_role_one_by_reload(g, one, verify, verify_2)
    helpers.reload_success_case(g, {
        roles = {one = one},
        roles_2 = {one = one},
        options = {
            ['roles'] = {'one'},
        },
        options_2 = {},
        verify = verify,
        verify_2 = verify_2,
    })
end

-- Verify that a role is stopped only once after it is removed and the
-- configuration is reloaded repeatedly.
g.test_stop_once_after_role_removal = function(g)
    local one = counting_role('one')

    remove_role_one_by_reload(g, one, function()
        t.assert_equals(rawget(_G, 'one_counts'), {apply = 1, stop = 0})
    end, function()
        t.assert_equals(rawget(_G, 'one_counts'), {apply = 1, stop = 1})
    end)

    g.server:exec(function()
        require('config'):reload()
        t.assert_equals(rawget(_G, 'one_counts'), {apply = 1, stop = 1})
    end)
end

-- Verify that a role removed by a reload is not stopped during shutdown.
g.test_stop_once_after_role_removal_on_shutdown = function(g)
    local one = [[
        return {
            validate = function() end,
            apply = function() end,
            stop = function()
                require('log').info('Role one stopped')
            end,
        }
    ]]

    remove_role_one_by_reload(g, one, function() end, function() end)

    -- The instance logs to stderr by default, and luatest writes the
    -- process output into the g.server.log_file file.
    g.server:stop()
    local log_file = assert(io.open(g.server.log_file, 'r'))
    local log_content = log_file:read('*all')
    log_file:close()
    local _, count = log_content:gsub('Role one stopped', '')
    t.assert_equals(count, 1)
end

-- Verify that a role stopped before a failed reload is not stopped again when
-- the reload is retried.
g.test_stop_once_after_partial_removal = function(g)
    local one = counting_role('one')
    local two = [[
        return {
            validate = function() end,
            apply = function(cfg)
                if cfg ~= nil and cfg.fail then
                    error('apply error', 0)
                end
            end,
            stop = function() end,
        }
    ]]

    local exp_err = 'Error applying role two: apply error'

    helpers.reload_failure_case(g, {
        roles = {one = one, two = two},
        roles_2 = {one = one, two = two},
        options = {
            ['roles'] = {'one', 'two'},
        },
        options_2 = {
            ['roles'] = {'two'},
            ['roles_cfg'] = {two = {fail = true}},
        },
        verify = function() end,
        exp_err = exp_err,
    })

    g.server:exec(function(exp_err)
        local config = require('config')
        t.assert_error_msg_equals(exp_err, config.reload, config)
        t.assert_equals(rawget(_G, 'one_counts'), {apply = 1, stop = 1})
    end, {exp_err})
end

-- Verify that only a role whose stop callback failed is retried.
g.test_stop_retry_after_stop_failure = function(g)
    local one = [[
        local counts = {apply = 0, stop = 0}
        rawset(_G, 'one_counts', counts)

        return {
            validate = function() end,
            apply = function()
                counts.apply = counts.apply + 1
            end,
            stop = function()
                counts.stop = counts.stop + 1
                error('stop error', 0)
            end,
        }
    ]]
    local two = counting_role('two')

    local exp_err = 'Error stopping role one: stop error'

    helpers.reload_failure_case(g, {
        roles = {one = one, two = two},
        roles_2 = {one = one, two = two},
        options = {
            ['roles'] = {'one', 'two'},
        },
        options_2 = {},
        verify = function() end,
        exp_err = exp_err,
    })

    g.server:exec(function(exp_err)
        local config = require('config')
        t.assert_error_msg_equals(exp_err, config.reload, config)
        t.assert_equals(rawget(_G, 'one_counts'), {apply = 1, stop = 2})
        t.assert_equals(rawget(_G, 'two_counts'), {apply = 1, stop = 1})
    end, {exp_err})
end

-- Verify that a callback suspended on an event sees the role state that was
-- active when the event was dispatched.
g.test_on_event_yield_during_role_removal = function(g)
    local one = [[
        return {
            validate = function() end,
            apply = function() end,
            stop = function()
                rawget(_G, 'stopped'):put(true)
            end,
            on_event = function(_, key)
                local started = rawget(_G, 'on_event_started')
                if key == 'box.status' and started ~= nil then
                    started:put(true)
                    assert(rawget(_G, 'resume_on_event'):get(5) ~= nil)
                end
            end,
        }
    ]]
    local two = [[
        return {
            validate = function() end,
            apply = function() end,
            stop = function() end,
            on_event = function(_, key, value)
                local count = rawget(_G, 'two_box_status_count')
                if key == 'box.status' and value.is_ro and count ~= nil then
                    rawset(_G, 'two_box_status_count', count + 1)
                end
            end,
        }
    ]]

    helpers.success_case(g, {
        roles = {one = one, two = two},
        options = {
            ['roles'] = {'one', 'two'},
        },
        verify = function()
            local fiber = require('fiber')
            local config = require('config')
            local yaml = require('yaml')

            rawset(_G, 'on_event_started', fiber.channel(1))
            rawset(_G, 'resume_on_event', fiber.channel(1))
            rawset(_G, 'stopped', fiber.channel(1))
            rawset(_G, 'reload_done', fiber.channel(1))
            rawset(_G, 'two_box_status_count', 0)

            box.cfg({read_only = true})
            t.assert(_G.on_event_started:get(5) ~= nil)

            local cconfig = config:cluster_config()
            cconfig.roles = {'two'}
            local file = assert(io.open('config.yaml', 'w'))
            file:write(yaml.encode(cconfig))
            file:close()

            local reload_ok, reload_err
            fiber.new(function()
                reload_ok, reload_err = pcall(config.reload, config)
                _G.reload_done:put(true)
            end)
            t.assert(_G.stopped:get(5) ~= nil)
            _G.resume_on_event:put(true)
            t.assert(_G.reload_done:get(5) ~= nil)

            t.assert(reload_ok, reload_err)
            t.assert_equals(_G.two_box_status_count, 1)
        end,
    })
end

-- Verify that a role removed from the configuration is applied again
-- after it is added back and is not stopped one more time.
g.test_role_readd_after_removal = function(g)
    local one = counting_role('one')

    remove_role_one_by_reload(g, one, function()
        t.assert_equals(rawget(_G, 'one_counts'), {apply = 1, stop = 0})
    end, function()
        t.assert_equals(rawget(_G, 'one_counts'), {apply = 1, stop = 1})
    end)

    -- Add the role back into the config file and reload.
    g.server:exec(function()
        local config = require('config')
        local cconfig = config:cluster_config()
        cconfig.roles = {'one'}
        local file = assert(io.open('config.yaml', 'w'))
        file:write(require('yaml').encode(cconfig))
        file:close()
        config:reload()
        t.assert_equals(rawget(_G, 'one_counts'), {apply = 2, stop = 1})
    end)
end
