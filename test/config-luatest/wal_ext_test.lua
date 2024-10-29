local t = require('luatest')
local cbuilder = require('luatest.cbuilder')
local cluster = require('test.config-luatest.cluster')

local g = t.group()

g.before_all(cluster.init)
g.after_each(cluster.drop)
g.after_all(cluster.clean)

g.before_all(function()
    t.tarantool.skip_if_not_enterprise(
        'The wal.ext option is supported only by Tarantool Enterprise Edition')
end)

-- Verify that if the wal.ext option is removed from the
-- declarative configuration, it is removed from the box-level
-- configuration (box.cfg.wal_ext) after config:reload().
--
-- This scenario was broken before tarantool/tarantool-ee#963.
g.test_basic = function(g)
    local config = cbuilder:new()
        :set_global_option('wal.ext', {old = true, new = true})
        :add_instance('i-001', {})
        :config()

    local cluster = cluster.new(g, config)
    cluster:start()

    -- Verify a test case prerequisite: the option is applied.
    cluster['i-001']:exec(function()
        t.assert_equals(box.cfg.wal_ext, {old = true, new = true})
    end)

    -- Remove the wal.ext option, write and reload the new
    -- configuration.
    --
    -- The wal.ext option removal had no effect before
    -- tarantool/tarantool-ee#963.
    local config_2 = cbuilder:new(config)
        :set_global_option('wal.ext', nil)
        :config()
    cluster:reload(config_2)

    -- Verify that the option is set to its default.
    cluster['i-001']:exec(function()
        t.assert_equals(box.cfg.wal_ext, nil)
        t.assert_type(box.cfg.wal_ext, 'nil')
    end)
end
