local fun = require('fun')
local yaml = require('yaml')
local t = require('luatest')
local treegen = require('test.treegen')
local server = require('test.luatest_helpers.server')
local helpers = require('test.config-luatest.helpers')
local cbuilder = require('test.config-luatest.cbuilder')

local g = helpers.group()

local base_config = {
    credentials = {
        roles = {
            test = {
                privileges = {
                    {
                        permissions = {},
                        lua_call = {}
                    },
                }
            },
        },
        users = {
            guest = {
                roles = {'super'},
            },
            alice = {
                roles = {},
                password = "ALICE",
                privileges = {
                    {
                        permissions = {},
                        lua_call = {}
                    },
                }
            },
        },
    },
    database = {
        mode = "rw"
    },
    iproto = {
        listen = {{uri = 'unix/:./{{ instance_name }}.iproto'}},
    },
    groups = {
        ['group-001'] = {
            replicasets = {
                ['replicaset-001'] = {
                    instances = {
                        ['instance-001'] = {},
                    },
                },
            },
        },
    },
}

local function access_error_msg(user, func)
    local templ = 'Execute access to function \'%s\' is denied for user \'%s\''
    return string.format(templ, func, user)
end

local function define_access_error_msg_function(server)
    server:exec(function(access_error_msg)
        rawset(_G, 'access_error_msg', loadstring(access_error_msg))
    end, {string.dump(access_error_msg)})
end

g.test_direct_lua_call_in_ro_mode = function(g)
    local config = table.deepcopy(base_config)
    local dir = treegen.prepare_directory(g, {}, {})
    local config_file = treegen.write_script(dir, 'config.yaml',
                                             yaml.encode(config))

    local opts = {config_file = config_file, chdir = dir}
    g.server = server:new(fun.chain(opts, {alias = 'instance-001'}):tomap())
    g.server:start()
    g.server:stop()

    config.database.mode = 'ro'

    config.credentials.roles.test.privileges[1].lua_call = {'bar'}
    config.credentials.roles.test.privileges[1].permissions = {'execute'}

    config.credentials.users.alice.roles = {'test'}
    config.credentials.users.alice.privileges[1].lua_call = {'foo'}
    config.credentials.users.alice.privileges[1].permissions = {'execute'}

    treegen.write_script(dir, 'config.yaml',
                         yaml.encode(config))
    g.server:start()
    define_access_error_msg_function(g.server)
    g.server:exec(function()
        -- Starting in `ro` mode means that privileges are not written to the
        -- base, and we cannot grant any access using box.schema.user.grant.

        -- Verify that we cannot grant access using `box.schema.user.grant`.
        local msg = 'Can\'t modify data on a read-only instance - ' ..
            'box.cfg.read_only is true'
        local grant = function()
            box.schema.user.grant('alice', 'execute', 'lua_call', 'bar')
        end
        t.assert_error_msg_equals(msg, grant)

        local netbox = require('net.box')
        local config = require('config')
        local uri = config:instance_uri().uri
        local con = netbox.connect(uri, {user="alice", password="ALICE"})
        rawset(_G, 'foo', function() return true end)
        rawset(_G, 'bar', function() return true end)
        rawset(_G, 'baz', function() return true end)

        -- Verify that user `alice` able to call functions `foo` and `bar`
        -- and nothing else.
        t.assert(con:call('foo'))
        t.assert(con:call('bar'))

        t.assert_error_msg_equals(_G.access_error_msg('alice', 'baz'),
                                  function() con:call('baz') end)
    end)
end

g.test_universe_lua_call_in_ro_mode = function(g)
    local config = table.deepcopy(base_config)
    local dir = treegen.prepare_directory(g, {}, {})
    local config_file = treegen.write_script(dir, 'config.yaml',
                                             yaml.encode(config))

    local opts = {config_file = config_file, chdir = dir}
    g.server = server:new(fun.chain(opts, {alias = 'instance-001'}):tomap())
    g.server:start()
    g.server:stop()

    config.database.mode = 'ro'

    config.credentials.roles.test.privileges[1].lua_call = {'all', 'box.info'}
    config.credentials.roles.test.privileges[1].permissions = {'execute'}

    config.credentials.roles.no_exec = {
        privileges = {
            {
                permissions = {'read'},
                lua_call = {'loadstring'},
            }
        }
    }

    config.credentials.users.alice.roles = {'test', 'no_exec'}
    config.credentials.users.alice.privileges[1].lua_call = {'box.info'}
    config.credentials.users.alice.privileges[1].permissions = {'execute'}

    treegen.write_script(dir, 'config.yaml',
                         yaml.encode(config))
    g.server:start()
    define_access_error_msg_function(g.server)
    g.server:exec(function()
        local netbox = require('net.box')
        local config = require('config')
        local uri = config:instance_uri().uri
        local con = netbox.connect(uri, {user="alice", password="ALICE"})
        rawset(_G, 'foo', function() return true end)

        -- Verify that user `alice` able to call any global lua function.
        t.assert(con:call('foo'))

         -- Verify that user `alice` able to call granted built-in function.
        t.assert_equals(con:call('box.info'), box.info())

        -- User `alice` is unable to use the loadstring function because the
        -- `no_exec` role hasn't `execute` permissions.
        t.assert_error_msg_equals(_G.access_error_msg('alice', 'loadstring'),
                                  function() con:call('loadstring') end)
    end)
end
