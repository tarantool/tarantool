local t = require('luatest')
local cbuilder = require('luatest.cbuilder')
local cluster = require('test.config-luatest.cluster')

local g = t.group()

g.before_all(cluster.init)
g.after_each(cluster.drop)
g.after_all(cluster.clean)

g.before_all(function()
    t.tarantool.skip_if_not_enterprise(
        'The audit_log.spaces option is supported only by Tarantool ' ..
        'Enterprise Edition')
end)

-- Verify that if the audit_log.spaces option is changed (removed,
-- for example) in the declarative configuration, a warning about
-- a non-dynamic option change is issued, but the option is NOT
-- changed in the underlying box-level configuration
-- (box.cfg.audit_spaces) after config:reload().
--
-- This scenario is described in tarantool/tarantool-ee#964.
g.test_basic = function(g)
    local config = cbuilder:new()
        :set_global_option('audit_log.spaces', {'myspace'})
        :add_instance('i-001', {})
        :config()

    local cluster = cluster.new(g, config)
    cluster:start()

    -- Verify a test case prerequisite: the option is applied.
    cluster['i-001']:exec(function()
        t.assert_equals(box.cfg.audit_spaces, {'myspace'})
    end)

    -- Remove the audit_log.spaces option, write and reload the
    -- new configuration.
    local config_2 = cbuilder:new(config)
        :set_global_option('audit_log.spaces', nil)
        :config()
    cluster:reload(config_2)

    -- Verify that the option remains unchanged and a warning is
    -- issued.
    --
    -- The warning was not issued before
    -- tarantool/tarantool-ee#964.
    cluster['i-001']:exec(function()
        local function find_alert(exp)
            local alerts = box.info.config.alerts

            for _, a in ipairs(alerts) do
                if a.message:find(exp) then
                    return a
                end
            end

            return false, {
                alerts = alerts,
                needle = exp,
            }
        end

        -- Check the warning.
        t.assert(find_alert('non%-dynamic option audit_spaces will not be ' ..
            'set until the instance is restarted'))

        -- Verify the actual box-level value.
        t.assert_equals(box.cfg.audit_spaces, {'myspace'})
    end)
end
