local t = require('luatest')
local cbuilder = require('luatest.cbuilder')
local cluster = require('test.config-luatest.cluster')

local g = t.group()

g.before_all(cluster.init)
g.after_each(cluster.drop)
g.after_all(cluster.clean)

g.test_basic = function(g)
    -- Configuration limiting memory available to Lua with 256MB.
    local lua_memory = 256 * 1024 * 1024
    local config = cbuilder:new()
        :set_global_option('lua.memory', lua_memory)
        :add_instance('i-001', {})
        :config()

    local cluster = cluster.new(g, config)
    cluster:start()

    -- Check lua.memory has been successfully applied.
    cluster['i-001']:exec(function(lua_memory)
        local alloc = require('internal.alloc')
        local config = require('config')

        t.assert_equals(config:get('lua.memory'), lua_memory)
        t.assert_equals(alloc.getlimit(), lua_memory)
    end, {lua_memory})

    -- Check there is a "not enough memory" error when Lua
    -- tries to allocate more than configured.
    cluster['i-001']:exec(function()
        local alloc = require('internal.alloc')
        local digest = require('digest')

        local function overflow()
            -- Try to allocate all of the Lua available memory
            -- plus 1MB.
            local size = alloc.unused() + 1024 * 1024
            local _ = digest.urandom(size)
        end

        t.assert_error_msg_equals('not enough memory', overflow, {})
    end)

    -- Check it's still possible to allocate almost all of the
    -- memory up to the limit.
    cluster['i-001']:exec(function()
        local alloc = require('internal.alloc')
        local digest = require('digest')

        -- Try to allocate all of the Lua available memory
        -- minus 1MB.
        local size = alloc.unused() - 1 * 1024 * 1024
        local _ = digest.urandom(size)

        -- Check there is <= 2MB left. 2MB is used instead
        -- of 1MB to ensure the implementation details of Lua
        -- memory management won't affect the comparison.
        t.assert_le(alloc.unused(), 2 * 1024 * 1024)
    end)
end

g.test_update = function(g)
    -- Config with 512MB memory limit.
    local lua_memory_0 = 512 * 1024 * 1024
    local config_0 = cbuilder:new()
        :set_global_option('lua.memory', lua_memory_0)
        :add_instance('i-001', {})
        :config()

    local cluster = cluster.new(g, config_0)
    cluster:start()

    -- Config with 256MB memory limit.
    local lua_memory_1 = 256 * 1024 * 1024
    local config_1 = cbuilder:new(config_0)
        :set_global_option('lua.memory', lua_memory_1)
        :add_instance('i-001', {})
        :config()

    -- Load a config with a new lua.memory value.
    cluster:reload(config_1)

    -- lua.memory could be decreased dynamically if
    -- the limit is not too close to the currently used
    -- amount of memory.
    -- Check there is no alerts and it's performed successfully.
    cluster['i-001']:exec(function(lua_memory)
        local alloc = require('internal.alloc')
        local config = require('config')

        t.assert_equals(config:get('lua.memory'), lua_memory)
        t.assert_equals(alloc.getlimit(), lua_memory)

        local alerts = config:info().alerts
        t.assert_equals(alerts, {})
    end, {lua_memory_1})

    -- Config with 1GB Lua memory limit.
    local lua_memory_2 = 1024 * 1024 * 1024
    local config_2 = cbuilder:new(config_1)
        :set_global_option('lua.memory', lua_memory_2)
        :config()

    -- Load a config with a new lua.memory value.
    cluster:reload(config_2)

    -- lua.memory could be increased dynamically.
    -- Check there is no alerts and it's performed successfully.
    cluster['i-001']:exec(function(lua_memory)
        local alloc = require('internal.alloc')
        local config = require('config')

        t.assert_equals(config:get('lua.memory'), lua_memory)
        t.assert_equals(alloc.getlimit(), lua_memory)

        local alerts = config:info().alerts
        t.assert_equals(alerts, {})
    end, {lua_memory_2})

    -- Allocate a huge chunk of memory and fetch the used
    -- value (1/4 of the current memory limit).
    local used = cluster['i-001']:exec(function(lua_memory)
        local alloc = require('internal.alloc')
        local digest = require('digest')

        rawset(_G, 'stub', digest.urandom(lua_memory / 4))
        return alloc.used()
    end, {lua_memory_2})

    local lua_memory_3 = used + 1024 * 1024
    -- Config with currently used + 1MB memory limit.
    local config_3 = cbuilder:new(config_2)
        :set_global_option('lua.memory', lua_memory_3)
        :config()
    cluster:reload(config_3)

    -- The lua.memory option can't be applied if after applying
    -- there won't be enough unused space (currently it's 16MB).
    --
    -- Check applying doesn't change anything and creates an
    -- alert on restart is needed to apply changes.
    cluster['i-001']:exec(function(new_lua_memory, old_lua_memory)
        local alloc = require('internal.alloc')
        local config = require('config')

        t.assert_equals(config:get('lua.memory'), new_lua_memory)
        t.assert_equals(alloc.getlimit(), old_lua_memory)

        local alerts = config:info().alerts
        t.assert_equals(#alerts, 1)
        t.assert_equals(alerts[1].type, 'warn')
        local exp = 'lua.apply: lua.memory will be applied after ' ..
                    'restarting the instance since the new limit is too ' ..
                    'close to the currently allocated amount of memory'
        t.assert_equals(alerts[1].message, exp)
    end, {lua_memory_3, lua_memory_2})

    -- Restart the cluster. Check the option has been
    -- applied and there is no alerts.
    cluster:stop()
    cluster:start()

    cluster['i-001']:exec(function(lua_memory)
        local alloc = require('internal.alloc')
        local config = require('config')

        t.assert_equals(config:get('lua.memory'), lua_memory)
        t.assert_equals(alloc.getlimit(), lua_memory)

        local alerts = config:info().alerts
        t.assert_equals(alerts, {})
    end, {lua_memory_3})

    cluster:reload(config_3)

    -- Make sure loading configuration with the same lua.memory
    -- value don't issue an alert.
    cluster['i-001']:exec(function(lua_memory)
        local alloc = require('internal.alloc')
        local config = require('config')

        t.assert_equals(config:get('lua.memory'), lua_memory)
        t.assert_equals(alloc.getlimit(), lua_memory)

        local alerts = config:info().alerts
        t.assert_equals(alerts, {})
    end, {lua_memory_3})

end

g.test_wrong_value = function(g)
    local config = cbuilder:new()
        :add_instance('i-001', {})
        :config()

    -- cbuilder automatically validates config.
    -- Rewrite lua.memory option externally.
    config.lua = {memory = 1024}

    cluster.startup_error(g, config, 'Memory limit should be >= 256MB')
end
