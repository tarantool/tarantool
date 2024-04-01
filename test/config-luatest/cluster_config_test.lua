local t = require('luatest')
local cbuilder = require('test.config-luatest.cbuilder')
local cluster = require('test.config-luatest.cluster')

local g = t.group()

g.before_all(cluster.init)
g.after_each(cluster.drop)
g.after_all(cluster.clean)

-- Attempt to pass an empty group and an empty replicaset.
g.test_misplace_option = function(g)
    local config = cbuilder.new()
        :use_group('g-001')

        :use_replicaset('r-001')
        :add_instance('i-001', {})

        :use_group('sharding')
        :set_group_option('roles', {'storage'})
        :config()

    cluster.startup_error(g, config, "group \"sharding\" should " ..
                                     "include at least one replicaset.")

    local config = cbuilder.new()
        :use_group('g-001')

        :use_replicaset('r-001')
        :add_instance('i-001', {})

        :use_replicaset('sharding')
        :set_replicaset_option('roles', {'storage'})

        :config()

    cluster.startup_error(g, config, "replicaset \"sharding\" should " ..
                                     "include at least one instance.")
end
