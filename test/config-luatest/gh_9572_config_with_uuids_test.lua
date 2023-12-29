local uuid = require('uuid')
local t = require('luatest')
local cbuilder = require('test.config-luatest.cbuilder')
local replicaset = require('test.config-luatest.replicaset')

local g = t.group()

g.before_all(replicaset.init)
g.after_each(replicaset.drop)
g.after_all(replicaset.clean)

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
    local rs = replicaset.new(g, config)
    rs:start()

    rs['i-001']:exec(function()
        t.assert_equals(box.info.name, 'i-001')
        t.assert_equals(require('config')._alerts, {})
    end)

    rs['i-002']:exec(function()
        t.assert_equals(box.info.name, 'i-002')
        t.assert_equals(require('config')._alerts, {})
    end)
end
