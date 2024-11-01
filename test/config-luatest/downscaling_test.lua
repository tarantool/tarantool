local t = require('luatest')
local cbuilder = require('luatest.cbuilder')
local cluster = require('test.config-luatest.cluster')

local g = t.group()

g.before_all(cluster.init)
g.after_each(cluster.drop)
g.after_all(cluster.clean)

-- Verify that if an instance is removed from the declarative
-- configuration, it is removed from upstreams in the box-level
-- configuration (box.cfg.replication) after config:reload().
--
-- It effectively stops the data flow from the removed instance to
-- others.
g.test_size_3_to_2 = function(g)
    local config = cbuilder:new()
        :set_replicaset_option('replication.failover', 'manual')
        :set_replicaset_option('leader', 'i-001')
        :add_instance('i-001', {})
        :add_instance('i-002', {})
        :add_instance('i-003', {})
        :config()

    local cluster = cluster.new(g, config)
    cluster:start()

    -- Verify a test case prerequisite: all the instances have all
    -- the instances in upstreams.
    cluster:each(function(server)
        server:exec(function()
            local config = require('config')

            t.assert_equals(box.cfg.replication, {
                config:instance_uri('peer', {instance = 'i-001'}),
                config:instance_uri('peer', {instance = 'i-002'}),
                config:instance_uri('peer', {instance = 'i-003'}),
            })
        end)
    end)

    -- Remove i-003 from the configuration and write it to the
    -- file.
    local config_2 = cbuilder:new(config)
        :set_replicaset_option('instances.i-003', nil)
        :config()
    cluster:sync(config_2)

    -- Define a helper method.
    cluster.each_except = function(self, exclusion, f)
        self:each(function(server)
            if server.alias ~= exclusion then
                f(server)
            end
        end)
    end

    -- Reload the configuration on i-001 and i-002.
    cluster:each_except('i-003', function(server)
        server:exec(function()
            local config = require('config')

            config:reload()
        end)
    end)

    -- Verify that i-003 is not in the configured upstreams on
    -- i-001 and i-002.
    cluster:each_except('i-003', function(server)
        server:exec(function()
            local config = require('config')

            t.assert_equals(box.cfg.replication, {
                config:instance_uri('peer', {instance = 'i-001'}),
                config:instance_uri('peer', {instance = 'i-002'}),
            })
        end)
    end)
end

-- Same as previous, but scales the replicaset down from two
-- instances to one.
--
-- This scenario was broken before gh-10716.
g.test_size_2_to_1 = function(g)
    local config = cbuilder:new()
        :set_replicaset_option('replication.failover', 'manual')
        :set_replicaset_option('leader', 'i-001')
        :add_instance('i-001', {})
        :add_instance('i-002', {})
        :config()

    local cluster = cluster.new(g, config)
    cluster:start()

    -- Verify a test case prerequisite: both instances are in the
    -- upstream list on i-001.
    cluster['i-001']:exec(function()
        local config = require('config')

        t.assert_equals(box.cfg.replication, {
            config:instance_uri('peer', {instance = 'i-001'}),
            config:instance_uri('peer', {instance = 'i-002'}),
        })
    end)

    -- Remove i-002 from the configuration and write it to the
    -- file.
    local config_2 = cbuilder:new(config)
        :set_replicaset_option('instances.i-002', nil)
        :config()
    cluster:sync(config_2)

    -- Reload the configuration on i-001.
    cluster['i-001']:exec(function()
        local config = require('config')

        config:reload()
    end)

    -- Verify that there is no i-002 in the upstreams. In fact,
    -- when the only instance in the upstreams list is the
    -- current instance, the empty upstreams list is generated.
    cluster['i-001']:exec(function()
        t.assert_equals(box.cfg.replication, {})
    end)
end

-- Verify how the replication.disabled option works.
g.test_replication_disabled = function(g)
    -- A shortcut function to verify box.cfg.replication.
    --
    -- Returns a function and a list of its arguments.
    --
    -- Suitable to pass to <server object>:exec().
    local function verify_upstreams(instances)
        return function(instances)
            local config = require('config')

            local uris = {}
            for _, instance in ipairs(instances) do
                local opts = {instance = instance}
                local uri = config:instance_uri('peer', opts)
                table.insert(uris, uri)
            end

            t.assert_equals(box.cfg.replication, uris)
        end, {instances}
    end

    -- A shortcut function to verify that the given alert exists.
    --
    -- Returns a function and a list of its arguments.
    --
    -- Suitable to pass to <server object>:exec().
    local function find_alert(type, message)
        return function(type, message)
            for _, alert in ipairs(box.info.config.alerts) do
                if alert.type == type and alert.message == message then
                    return
                end
            end
            t.fail(('Unable to find alert: {type = %q, message = %q}'):format(
                type, message))
        end, {type, message}
    end

    local config = cbuilder:new()
        :set_replicaset_option('replication.failover', 'manual')
        :set_replicaset_option('leader', 'i-001')
        :add_instance('i-001', {})
        :add_instance('i-002', {})
        :add_instance('i-003', {})
        :config()

    local cluster = cluster.new(g, config)
    cluster:start()

    -- Verify a test case prerequisite: all the instances have all
    -- the instances in upstreams.
    cluster:each(function(server)
        server:exec(verify_upstreams({'i-001', 'i-002', 'i-003'}))
    end)

    -- Mark i-003 as disabled in the configuration, write it to
    -- the file and reload the configuration on all the instances.
    local config_2 = cbuilder:new(config)
        :set_instance_option('i-003', 'replication.disabled', true)
        :config()
    cluster:reload(config_2)

    -- Verify that i-003 is not in the configured upstreams on
    -- i-001 and i-002.
    cluster['i-001']:exec(verify_upstreams({'i-001', 'i-002'}))
    cluster['i-002']:exec(verify_upstreams({'i-001', 'i-002'}))

    -- Verify that i-003 has no configured upstreams.
    cluster['i-003']:exec(verify_upstreams({}))

    -- Verify that a warning is issued on the disabled instance.
    cluster['i-003']:exec(find_alert('warn', 'replication.disabled = true ' ..
        'is set for the instance "i-003": the data is not replicated from ' ..
        'or to this instance'))

    -- Mark i-003 as enabled back in the configuration, write it
    -- to the file and reload the configuration on all the
    -- instances.
    local config_3 = cbuilder:new(config_2)
        :set_instance_option('i-003', 'replication.disabled', false)
        :config()
    cluster:reload(config_3)

    -- Verify that the initial situation is now here back: all the
    -- instances have all the instances in upstreams.
    cluster:each(function(server)
        server:exec(verify_upstreams({'i-001', 'i-002', 'i-003'}))
    end)

    -- Verify that the alert about the replication.disabled option
    -- is gone.
    cluster['i-003']:exec(function()
        t.assert_equals(box.info.config.alerts, {})
    end)
end
