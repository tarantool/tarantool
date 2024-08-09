local t = require('luatest')
local cbuilder = require('test.config-luatest.cbuilder')
local cluster = require('test.config-luatest.cluster')

local g = t.group()

local config = cbuilder.new()
    :add_instance('i-001', {})
    :set_global_option('credentials.users.alice')
    :set_global_option('credentials.users.alice.password', 'ALICE')
    :config()

g.before_all(cluster.init)
g.after_each(cluster.drop)
g.after_all(cluster.clean)

local function access_error_msg(user, func)
    local templ = 'Execute access to function \'%s\' is denied for user \'%s\''
    return string.format(templ, func, user)
end

local function define_access_error_msg_function(server)
    server:exec(function(access_error_msg)
        rawset(_G, 'access_error_msg', loadstring(access_error_msg))
    end, {string.dump(access_error_msg)})
end

g.test_grant_lua_call = function(g)
    local cluster = cluster.new(g, config)
    cluster:start()
    define_access_error_msg_function(cluster['i-001'])
    cluster['i-001']:exec(function()
        local netbox = require('net.box')
        local config = require('config')
        local uri = config:instance_uri().uri
        local con = netbox.connect(uri, {user='alice', password='ALICE'})

        rawset(_G, 'foo', function() return true end)
        rawset(_G, 'bar', function() return true end)

        -- Try to call 'foo' for user `alice`. Check that user `alice` does not
        -- have privileges to call `foo`.
        t.assert_error_msg_equals(_G.access_error_msg('alice', 'foo'),
                                  function() con:call('foo') end)

        -- Grant access to call function `foo` for user `alice`.
        box.internal.grant_lua_call('alice', 'foo')
        t.assert(con:call('foo'))

        -- Check that user `alice` does not have permission to call functions
        -- other than `foo`.
        t.assert_error_msg_equals(_G.access_error_msg('alice', 'bar'),
                                  function() con:call('bar') end)
    end)
end

g.test_revoke_lua_call = function(g)
    local cluster = cluster.new(g, config)
    cluster:start()
    define_access_error_msg_function(cluster['i-001'])
    cluster['i-001']:exec(function()
        local netbox = require('net.box')
        local config = require('config')
        local uri = config:instance_uri().uri
        local con = netbox.connect(uri, {user='alice', password='ALICE'})
        rawset(_G, 'foo', function() return true end)

        -- Grant access to call function `foo` for user `alice`.
        box.internal.grant_lua_call('alice', 'foo')
        t.assert(con:call('foo'))

        -- Revoke access to call function `foo` for user `alice`. Check that
        -- user `alice` does not have privileges to call `foo`.
        box.internal.revoke_lua_call('alice', 'foo')
        t.assert_error_msg_equals(_G.access_error_msg('alice', 'foo'),
                                  function() con:call('foo') end)
    end)
end

g.test_grant_universe_lua_call = function(g)
    local cluster = cluster.new(g, config)
    cluster:start()
    define_access_error_msg_function(cluster['i-001'])
    cluster['i-001']:exec(function()
        local netbox = require('net.box')
        local config = require('config')
        local uri = config:instance_uri().uri
        local con = netbox.connect(uri, {user='alice', password='ALICE'})
        rawset(_G, 'foo', function() return true end)
        rawset(_G, 'bar', function() return true end)

        -- Grant universe access to user `alice`.
        box.internal.grant_lua_call('alice', '')

        -- Universe access gives privileges to call any non-builtins functions.
        t.assert(con:call('foo'))
        t.assert(con:call('bar'))

        -- Universe access does not allow calling built-in functions.
        t.assert_error_msg_equals(_G.access_error_msg('alice', 'box.info'),
                                  function() con:call('box.info') end)
    end)
end

g.test_grant_builtins_lua_call = function(g)
    local cluster = cluster.new(g, config)
    cluster:start()
    define_access_error_msg_function(cluster['i-001'])
    cluster['i-001']:exec(function()
        local netbox = require('net.box')
        local config = require('config')
        local uri = config:instance_uri().uri
        local con = netbox.connect(uri, {user='alice', password='ALICE'})

        t.assert_error_msg_equals(_G.access_error_msg('alice', 'box.info'),
                                  function() con:call('box.info') end)

        -- Grant access to call builtins for user `alice`.
        box.internal.grant_lua_call('alice', 'box.info')

        -- Check that user `alice` can call built-in functions.
        t.assert_equals(con:call('box.info'), box.info())
    end)
end

g.test_grant_grant_lua_call = function(g)
    local cluster = cluster.new(g, config)
    cluster:start()
    define_access_error_msg_function(cluster['i-001'])
    cluster['i-001']:exec(function()
        local netbox = require('net.box')
        local config = require('config')
        local uri = config:instance_uri().uri
        local con = netbox.connect(uri, {user='alice', password='ALICE'})

        -- Grant user `alice` permission to call a function that grants
        -- runtime privileges.
        box.internal.grant_lua_call('alice', 'box.internal.grant_lua_call')

        -- Check that only admin or user with role SUPER can grant access.
        local msg = 'internal.grant_lua_call only ADMIN or user with role ' ..
            'SUPER can grant access.'
        t.assert_error_msg_equals(msg, function()
            con:call('box.internal.grant_lua_call', {'alice', 'foo'})
        end)
    end)
end
