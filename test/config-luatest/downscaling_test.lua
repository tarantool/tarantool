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
