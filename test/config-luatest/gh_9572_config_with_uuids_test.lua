local uuid = require('uuid')
local t = require('luatest')
local cbuilder = require('test.config-luatest.cbuilder')
local cluster = require('test.config-luatest.cluster')

local g = t.group()

g.before_all(cluster.init)
g.after_each(cluster.drop)
g.after_all(cluster.clean)

g.test_basic = function(g)
    local config = cbuilder.new()
        :set_replicaset_option('replication.failover', 'manual')
        :set_replicaset_option('leader', 'i-001')
        :add_instance('i-001', {
            database = {
                instance_uuid = uuid.str()
            }
        })
        :add_instance('i-002', {
            database = {
                instance_uuid = uuid.str()
            }
        })
        :config()
    local cluster = cluster.new(g, config)
    cluster:start()

    cluster['i-001']:exec(function()
        t.assert_equals(box.info.name, 'i-001')
        t.assert_equals(require('config'):info().alerts, {})
    end)

    cluster['i-002']:exec(function()
        t.assert_equals(box.info.name, 'i-002')
        t.assert_equals(require('config'):info().alerts, {})
    end)
end
