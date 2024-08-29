local t = require('luatest')
local cbuilder = require('luatest.cbuilder')
local cluster = require('test.config-luatest.cluster')

local g = t.group()

g.before_all(cluster.init)
g.after_each(cluster.drop)
g.after_all(cluster.clean)

-- {{{ Testing helpers

local function access_error_msg(user, func)
    local templ = 'Execute access to function \'%s\' is denied for user \'%s\''
    return string.format(templ, func, user)
end

local function define_access_error_msg_function(server)
    server:exec(function(access_error_msg)
        rawset(_G, 'access_error_msg', loadstring(access_error_msg))
    end, {string.dump(access_error_msg)})
end

local function define_stub_function(server, func_name)
    server:exec(function(func_name)
        rawset(_G, func_name, function()
            return true
        end)
    end, {func_name})
end

local function define_get_connection_function(server)
    server:exec(function()
        local netbox = require('net.box')
        local config = require('config')

        rawset(_G, 'get_connection', function()
            local uri = config:instance_uri().uri
            return netbox.connect(uri, {user = 'alice', password = 'ALICE'})
        end)
    end)
end

local function new_cluster_in_ro()
    local config = cbuilder:new()
        :add_instance('i-001', {})
        -- Create a user and set the password while we're in RW.
        :set_global_option('credentials.users.alice', {
            password = 'ALICE',
        })
        :config()
    local cluster = cluster.new(g, config)
    cluster:start()

    define_access_error_msg_function(cluster['i-001'])
    define_stub_function(cluster['i-001'], 'foo')
    define_stub_function(cluster['i-001'], 'bar')
    define_stub_function(cluster['i-001'], 'baz')
    define_get_connection_function(cluster['i-001'])

    -- We can only bootstrap the initial database in the RW mode.
    -- Let's reload to RO after it.
    local config = cbuilder:new(config)
        :set_global_option('database.mode', 'ro')
        :config()
    cluster:reload(config)

    -- Verify that the instance is in the RO mode.
    cluster['i-001']:exec(function()
        t.assert_equals(box.info.ro, true)
    end)

    return config, cluster
end

local function lua_call_priv(funcs)
    return {
        permissions = {'execute'},
        lua_call = funcs,
    }
end

local function inane_lua_call_priv(funcs)
    return {
        permissions = {'read'},
        lua_call = funcs,
    }
end

-- }}} Testing helpers

g.test_direct_lua_call_in_ro_mode = function()
    local config, cluster = new_cluster_in_ro()

    -- Add new function privileges.
    local config = cbuilder:new(config)
        :set_global_option('credentials.roles.test', {
            privileges = {lua_call_priv({'bar'})},
        })
        :set_global_option('credentials.roles.deps', {
            privileges = {lua_call_priv({'box.info'})},
            roles = {'test'},
        })
        :set_global_option('credentials.users.alice', {
            privileges = {lua_call_priv({'foo'})},
            roles = {'deps'},
            password = 'ALICE',
        })
        :config()
    cluster:reload(config)

    -- Verify that the new privileges are granted.
    cluster['i-001']:exec(function()
        local conn = _G.get_connection()

        -- Granted by user's privileges.
        t.assert(conn:call('foo'))

        -- Granted by a directly assigned role.
        t.assert(conn:call('box.info'))

        -- Granted by an indirectly assigned role.
        t.assert(conn:call('bar'))

        -- Any other function is forbidden.
        local exp_err = _G.access_error_msg('alice', 'baz')
        t.assert_error_msg_equals(exp_err, function()
            conn:call('baz')
        end)
    end)
end

g.test_universe_lua_call_in_ro_mode = function()
    local config, cluster = new_cluster_in_ro()

    -- Add new function privileges.
    local config = cbuilder:new(config)
        :set_global_option('credentials.roles.test', {
            privileges = {lua_call_priv({'all', 'box.info'})},
        })
        :set_global_option('credentials.roles.no_exec', {
            privileges = {inane_lua_call_priv({'loadstring'})},
        })
        :set_global_option('credentials.users.alice', {
            privileges = {lua_call_priv({'box.info'})},
            roles = {'test', 'exec'},
            password = 'ALICE',
        })
        :config()
    cluster:reload(config)

    cluster['i-001']:exec(function()
        local conn = _G.get_connection()

        -- Verify that user `alice` able to call any global lua function.
        t.assert(conn:call('foo'))

         -- Verify that user `alice` able to call granted built-in function.
        t.assert(conn:call('box.info'))

        -- User `alice` is unable to use the loadstring function because the
        -- `no_exec` role hasn't `execute` permissions.
        local exp_err = _G.access_error_msg('alice', 'loadstring')
        t.assert_error_msg_equals(exp_err, function()
            conn:call('loadstring')
        end)
    end)
end

g.test_no_user_privileges_lua_call_ro_mode = function()
    -- This test case checks the scenario where the user 'alice' has no direct
    -- privileges, but inherits them from a role `test` that grants the lua_call
    -- privileges.
    local config, cluster = new_cluster_in_ro()

    -- Add new function privileges.
    local config = cbuilder:new(config)
        :set_global_option('credentials.roles.test', {
            privileges = {lua_call_priv({'box.info', 'foo'})},
        })
        :set_global_option('credentials.users.alice', {
            roles = {'test'},
            password = 'ALICE',
        })
        :config()
    cluster:reload(config)

    cluster['i-001']:exec(function()
        local conn = _G.get_connection()

        -- Verify that user `alice` able to call `foo` function.
        t.assert(conn:call('foo'))

         -- Verify that user `alice` able to call `box.info` built-in function.
        t.assert(conn:call('box.info'))

         -- Any other function is forbidden.
         local exp_err = _G.access_error_msg('alice', 'baz')
         t.assert_error_msg_equals(exp_err, function()
             conn:call('baz')
         end)
    end)
end
