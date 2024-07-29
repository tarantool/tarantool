local t = require('luatest')
local cbuilder = require('test.config-luatest.cbuilder')
local cluster = require('test.config-luatest.cluster')

local g = t.group()

g.before_all(cluster.init)
g.after_each(cluster.drop)
g.after_all(cluster.clean)

local base_config = cbuilder.new()
    :add_instance('i-001', {})
    :set_global_option('credentials.users.alice')
    :set_global_option('credentials.users.alice.password', 'ALICE')
    :set_global_option('credentials.users.alice.privileges', {})
    :config()

local function test_lua_call_direct_access()
    local netbox = require('net.box')
    local config = require('config')
    local uri = config:instance_uri().uri
    local con = netbox.connect(uri, {user='alice', password='ALICE'})

    rawset(_G, 'foo', function() return true end)
    rawset(_G, 'bar', function() return true end)
    rawset(_G, 'baz', function() return true end)

    -- User `alice` should be able to call functions `foo` and `bar`
    -- and nothing else.
    t.assert(con:call('foo'))
    t.assert(con:call('bar'))

    local msg = 'Execute access to function \'baz\' is denied ' ..
        'for user \'alice\''
    t.assert_error_msg_equals(msg, function() con:call('baz') end)
end

g.test_user_lua_call = function(g)
    local config = cbuilder.new(base_config)
        :config()
    config.credentials.users.alice.privileges = {
        {
            lua_call = {'foo', 'bar'},
            permissions = {'execute'}
        }
    }
    local cluster = cluster.new(g, config)
    cluster:start()
    cluster['i-001']:exec(test_lua_call_direct_access)
end

g.test_role_lua_call = function(g)
    local config = cbuilder.new(base_config)
        :set_global_option('credentials.roles.test.privileges', {})
        :config()
    config.credentials.roles.test.privileges = {
        {
            lua_call = {'foo', 'bar'},
            permissions = {'execute'}
        }
    }
    config.credentials.users.alice.roles = {'test'}
    local cluster = cluster.new(g, config)
    cluster:start()
    cluster['i-001']:exec(test_lua_call_direct_access)
end

local function test_lua_call_universe_access()
    local netbox = require('net.box')
    local config = require('config')
    local uri = config:instance_uri().uri
    local con = netbox.connect(uri, {user='alice', password='ALICE'})

    rawset(_G, 'foo', function() return true end)
    rawset(_G, 'bar', function() return true end)

    -- `lua_call: all` grants privileges to call any non-built-in functions.
    t.assert(con:call('foo'))
    t.assert(con:call('bar'))

    -- `lua_call: all` does not allow calling built-in functions.
    local msg = 'Execute access to function \'box.info\' is denied ' ..
        'for user \'alice\''
    t.assert_error_msg_equals(msg, function() con:call('box.info') end)
end

g.test_lua_call_all = function(g)
    local config = cbuilder.new(base_config)
        :config()
    config.credentials.users.alice.privileges = {
        {
            lua_call = {'all'},
            permissions = {'execute'}
        }
    }
    local cluster = cluster.new(g, config)
    cluster:start()
    cluster['i-001']:exec(test_lua_call_universe_access)
end

g.test_lua_call_all_with_func = function()
    local config = cbuilder.new(base_config)
        :config()
    config.credentials.users.alice.privileges = {
        {
            lua_call = {'all', 'foo'},
            permissions = {'execute'}
        }
    }
    local cluster = cluster.new(g, config)
    cluster:start()
    cluster['i-001']:exec(test_lua_call_universe_access)
end

g.test_lua_call_all_with_built_in_func = function()
    local config = cbuilder.new(base_config)
        :config()
    config.credentials.users.alice.privileges = {
        {
            lua_call = {'all', 'box.info'},
            permissions = {'execute'}
        }
    }
    local cluster = cluster.new(g, config)
    cluster:start()
    cluster['i-001']:exec(function()
        local netbox = require('net.box')
        local config = require('config')
        local uri = config:instance_uri().uri
        local con = netbox.connect(uri, {user='alice', password='ALICE'})

        rawset(_G, 'foo', function() return true end)
        t.assert(con:call('foo'))
        t.assert_equals(con:call('box.info'), box.info())
    end)
end

local function test_lua_call_reload()
    local netbox = require('net.box')
    local config = require('config')
    local uri = config:instance_uri().uri
    local con = netbox.connect(uri, {user='alice', password='ALICE'})

    rawset(_G, 'foo', function() return true end)
    rawset(_G, 'bar', function() return true end)

    t.assert(con:call('foo'))
    t.assert(con:call('bar'))
end

g.test_lua_call_reload = function()
    local config = cbuilder.new(base_config)
        :config()
    config.credentials.users.alice.privileges = {
            {
                lua_call = {'foo', 'bar'},
                permissions = {'execute'}
            }
        }
    local cluster = cluster.new(g, config)
    cluster:start()
    cluster['i-001']:exec(test_lua_call_reload)
    cluster:reload(config)
    cluster['i-001']:exec(test_lua_call_reload)
end
