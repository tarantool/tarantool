local t = require('luatest')
local server = require('luatest.server')
local cbuilder = require('luatest.cbuilder')
local cluster = require('test.config-luatest.cluster')

local g = t.group()

g.before_all(cluster.init)
g.after_each(cluster.drop)
g.after_all(cluster.clean)

g.after_each(function(g)
    if g.server ~= nil then
        g.server:drop()
        g.server = nil
    end
end)

-- Verify box.info.config when tarantool is started from a script.
g.test_with_script = function(g)
    g.server = server:new()
    g.server:start()

    g.server:exec(function()
        local function verify_config(res)
            t.assert_equals(res, {
                status = 'uninitialized',
                alerts = {},
                meta = {
                    -- No 'active' field, because there is no
                    -- active configuration.
                    last = {},
                },
                hierarchy = {},
            })
        end

        verify_config(box.info.config)
        verify_config(box.info().config)
    end)
end

-- Verify box.info.config when tarantool is started from a config.
g.test_with_config = function(g)
    local config = cbuilder:new()
        :use_group('g-001')
        :use_replicaset('r-001')
        :add_instance('i-001', {})
        :config()
    local cluster = cluster.new(g, config)
    cluster:start()

    cluster['i-001']:exec(function()
        local function verify_config(res)
            t.assert_equals(res, {
                status = 'ready',
                alerts = {},
                meta = {
                    active = {},
                    last = {},
                },
                hierarchy = {
                    group = 'g-001',
                    replicaset = 'r-001',
                    instance = 'i-001',
                },
            })
        end

        verify_config(box.info.config)
        verify_config(box.info().config)
    end)
end

-- box.info() should work always, even if config:info() is broken.
g.test_broken_config_info = function(g)
    local config = cbuilder:new()
        :add_instance('i-001', {})
        :config()
    local cluster = cluster.new(g, config)
    cluster:start()

    cluster['i-001']:exec(function()
        local config = require('config')

        local function verify_config(res)
            t.assert_equals(res, {
                error = 'config:info() is broken',
            })
        end

        -- Break the method.
        config.info = function(_self, _version)
            error('config:info() is broken', 0)
        end

        verify_config(box.info.config)
        verify_config(box.info().config)
    end)
end

-- box.info() should work always, even if config module is broken.
g.test_broken_config_module = function(g)
    local config = cbuilder:new()
        :add_instance('i-001', {})
        :config()
    local cluster = cluster.new(g, config)
    cluster:start()

    cluster['i-001']:exec(function()
        local loaders = require('internal.loaders')

        local function verify_config(res)
            t.assert_type(res, 'table')
            t.assert_str_contains(res.error, "module 'config' not found")
        end

        -- Unload the module and break the next require('config').
        package.loaded.config = nil
        loaders.builtin.config = nil

        verify_config(box.info.config)
        verify_config(box.info().config)
    end)
end
