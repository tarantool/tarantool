local t = require('luatest')
local cbuilder = require('luatest.cbuilder')
local cluster = require('test.config-luatest.cluster')

local g = t.group()

g.before_all(cluster.init)
g.after_each(cluster.drop)
g.after_all(cluster.clean)

-- gh-10205: verify that options inside app.cfg.<key> and
-- roles_cfg.<key> can be accessed using config:get().
g.test_basic = function(g)
    -- Minimal config.
    local mycfg = {foo = {bar = {baz = 42, n = box.NULL}}}
    local config = cbuilder:new()
        :add_instance('i-001', {})
        :set_global_option('app.cfg', mycfg)
        :set_global_option('roles_cfg', mycfg)
        :config()

    -- Minimal cluster.
    local cluster = cluster.new(g, config)
    cluster:start()

    -- Test cases.
    cluster['i-001']:exec(function()
        local config = require('config')

        -- A usual option defined in a strict part of the config.
        t.assert_equals(config:get('memtx.memory'), 256 * 1024 * 1024)

        -- An option inside app.cfg.<key> or roles_cfg.<key>.
        t.assert_equals(config:get('app.cfg.foo.bar.baz'), 42)
        t.assert_equals(config:get('roles_cfg.foo.bar.baz'), 42)

        -- A missed option inside app.cfg.<key> or
        -- roles_cfg.<key>.
        t.assert_type(config:get('app.cfg.x.y.z'), 'nil')
        t.assert_type(config:get('roles_cfg.x.y.z'), 'nil')

        -- A box.NULL (null is YAML) option inside app.cfg.<key>
        -- or roles_cfg.<key>.
        t.assert_type(config:get('app.cfg.foo.bar.n'), 'cdata')
        t.assert_type(config:get('roles_cfg.foo.bar.n'), 'cdata')

        -- Indexing a box.NULL option inside app.cfg.<key> or
        -- roles_cfg.<key>.
        t.assert_type(config:get('app.cfg.foo.bar.n.i'), 'nil')
        t.assert_type(config:get('roles_cfg.foo.bar.n.i'), 'nil')

        -- Attempt to index a primitive value (except nil/box.NULL)
        -- inside app.cfg.<key>.
        local exp_err_msg = '[instance_config] app.cfg.foo.bar.baz: ' ..
            'Attempt to index a non-table value (number) by field "fiz"'
        t.assert_error_msg_equals(exp_err_msg, function()
            config:get('app.cfg.foo.bar.baz.fiz')
        end)

        -- The same inside roles_cfg.<key>.
        local exp_err_msg = '[instance_config] roles_cfg.foo.bar.baz: ' ..
            'Attempt to index a non-table value (number) by field "fiz"'
        t.assert_error_msg_equals(exp_err_msg, function()
            config:get('roles_cfg.foo.bar.baz.fiz')
        end)
    end)
end
