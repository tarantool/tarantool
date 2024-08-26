local t = require('luatest')
local cbuilder = require('luatest.cbuilder')
local cluster = require('test.config-luatest.cluster')

local g = t.group()

g.before_all(cluster.init)
g.after_each(cluster.drop)
g.after_all(cluster.clean)

g.test_sync_timeout_value_new = function(g)
    local config = cbuilder:new()
        :set_global_option('compat.box_cfg_replication_sync_timeout', 'new')
        :add_instance('i-001', {})
        :config()
    local cluster = cluster.new(g, config)
    cluster:start()

    cluster['i-001']:exec(function()
        t.assert_equals(box.cfg.replication_sync_timeout, 0)
    end)
end

g.test_sync_timeout_value_old = function(g)
    local config = cbuilder:new()
        :set_global_option('compat.box_cfg_replication_sync_timeout', 'old')
        :add_instance('i-001', {})
        :config()
    local cluster = cluster.new(g, config)
    cluster:start()

    cluster['i-001']:exec(function()
        t.assert_equals(box.cfg.replication_sync_timeout, 300)
    end)
end

g.test_sync_timeout_value_reload = function(g)
    local config = cbuilder:new()
        :set_global_option('replication.sync_timeout', 5)
        :add_instance('i-001', {})
        :config()
    local cluster = cluster.new(g, config)
    cluster:start()

    cluster['i-001']:exec(function()
        t.assert_equals(box.cfg.replication_sync_timeout, 5)
    end)

    local new_config = cbuilder:new()
        :add_instance('i-001', {})
        :config()
    cluster:reload(new_config)

    cluster['i-001']:exec(function()
        t.assert_equals(box.cfg.replication_sync_timeout, 0)
    end)
end
