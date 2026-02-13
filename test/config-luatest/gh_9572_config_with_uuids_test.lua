local uuid = require('uuid')
local t = require('luatest')
local cbuilder = require('luatest.cbuilder')
local cluster = require('luatest.cluster')

local g = t.group()

g.test_basic = function()
    local config = cbuilder:new()
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
    local cluster = cluster:new(config)
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
