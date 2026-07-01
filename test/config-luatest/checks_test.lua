local t = require('luatest')
local fio = require('fio')
local cbuilder = require('luatest.cbuilder')
local cluster = require('luatest.cluster')

local g = t.group('checks')

g.after_each(function(g)
    if g.cluster ~= nil then
        g.cluster:stop()
    end
    if g.temp_dir ~= nil then
        fio.rmtree(g.temp_dir)
    end
end)

-- {{{ checks: off

g.test_checks_off_is_valid = function(g)
    local builder = cbuilder:new():add_instance('i-001', {})
        :set_instance_option('i-001', 'config.checks', 'off')
    local config = builder:config()

    g.cluster = cluster:new(config)
    g.cluster:start()

    g.cluster['i-001']:exec(function()
        local t = require('luatest')
        local config = require('config')

        t.assert_equals(config:info().status, 'ready')
        t.assert_equals(#config:info().alerts, 0)
    end)
end

g.test_checks_off_no_fiber = function(g)
    local builder = cbuilder:new():add_instance('i-001', {})
        :set_instance_option('i-001', 'config.checks', 'off')
    local config = builder:config()

    g.cluster = cluster:new(config)
    g.cluster:start()

    g.cluster['i-001']:exec(function()
        local t = require('luatest')
        local fiber = require('fiber')

        local function has_check_fiber()
            for _, f in pairs(fiber.info()) do
                if f.name == 'config.checks' then
                    return true
                end
            end
            return false
        end

        t.assert_not(has_check_fiber(),
            'Check fiber should not run when checks are off')
    end)
end

g.test_no_fiber_without_checks = function(g)
    local builder = cbuilder:new():add_instance('i-001', {})
    local config = builder:config()

    g.cluster = cluster:new(config)
    g.cluster:start()

    g.cluster['i-001']:exec(function()
        local t = require('luatest')
        local fiber = require('fiber')

        local function has_check_fiber()
            for _, f in pairs(fiber.info()) do
                if f.name == 'config.checks' then
                    return true
                end
            end
            return false
        end

        t.assert_not(has_check_fiber(),
            'Check fiber should not run when no checks are registered')
    end)
end

-- }}} checks: off

-- {{{ Validation

g.test_checks_invalid_string = function()
    local instance_config = require('internal.config.instance_config')
    local iconfig = {
        config = {
            checks = 'on',
        },
    }
    local exp_err = '[instance_config] config.checks: Data does not match ' ..
        'selected union variant #1 (variant #1: Got on, but only the ' ..
        'following values are allowed: off)'
    t.assert_error_msg_equals(exp_err, function()
        instance_config:validate(iconfig)
    end)
end

-- }}} Validation
