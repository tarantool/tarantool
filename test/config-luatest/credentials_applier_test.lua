local json = require('json')
local it = require('test.interactive_tarantool')
local t = require('luatest')
local treegen = require('test.treegen')
local helpers = require('test.config-luatest.helpers')
local cbuilder = require('test.config-luatest.cbuilder')
local cluster = require('test.config-luatest.cluster')

local g = helpers.group()

local internal = require('internal.config.applier.credentials')._internal

g.before_all(cluster.init)
g.after_each(cluster.drop)
g.after_all(cluster.clean)

-- Collect delayed grant alerts and transform to
-- '<space> <permission>' form.
local function warnings()
    local config = require('config')

    local res = {}
    for _, alert in ipairs(config:info().alerts) do
        if alert.message:find('box.schema.user.grant') then
            local pattern = 'box.schema.user.grant%(".-", "(.-)", ' ..
                '"space", "(.-)"%)'
            local permissions, space = alert.message:match(pattern)
            for _, permission in ipairs(permissions:split(',')) do
                table.insert(res, ('%s %s'):format(space, permission))
            end
        end
    end
    table.sort(res)
    return res
end

-- Define the _G.warnings() function on the given server.
local function define_warnings_function(server)
    server:exec(function(warnings)
        rawset(_G, 'warnings', loadstring(warnings))
    end, {string.dump(warnings)})
end

-- Define the _G.assert_priv() function of the given server to
-- verify privileges on the given space granted for the 'guest'
-- user.
local function define_assert_priv_function(server)
    server:exec(function()
        rawset(_G, 'assert_priv', function(space_name, exp_privs)
            -- Collect all the privileges into a hash table.
            local ht = {}
            for _, priv in ipairs(box.schema.user.info('guest')) do
                local perms, obj_type, obj_name = unpack(priv)
                if obj_type == 'space' and obj_name == space_name then
                    for _, perm in ipairs(perms:split(',')) do
                        ht[perm] = true
                    end
                end
            end

            -- Transform them into an array-like table and sort.
            local res = {}
            for priv in pairs(ht) do
                table.insert(res, priv)
            end
            table.sort(res)

            -- Compare to the given list of privileges.
            t.assert_equals(table.concat(res, ','), exp_privs)
        end)
    end)
end

g.test_converters = function()
    local credentials = {
        roles = {
            myrole = {
                roles = {'super'},
                privileges = {
                    {
                        permissions = {'read'},
                        spaces = {'_priv'},
                    },
                    {
                        permissions = {'execute'},
                        functions = {'box.schema.user.info'},
                    },
                },
            },
        },
        users = {
            myuser = {
                roles = {'myrole', 'replication'},
                privileges = {
                    {
                        permissions = {'read', 'drop'},
                        spaces = {'_space', '_index'},
                    },
                    {
                        permissions = {'write', 'create'},
                        spaces = {'_func', '_space'},
                    },
                    {
                        permissions = {'write'},
                        sequences = {''},
                    },
                    {
                        permissions = {'execute'},
                        functions = {'LUA'},
                        sql = {'all'},
                        lua_call = {'all'},
                        lua_eval = true,
                    },
                },
            },
            guest = {
                roles = {'super'},
            },
        },
    }

    local function verify()
        local config = require('config')
        local credentials = require('internal.config.applier.credentials')
        local internal = credentials._internal
        local box_privs = internal.privileges_from_box
        local cfg_privs = internal.privileges_from_config

        -- Intermediate representation of myrole privileges.
        local exp = {
            ['user'] = {},
            ['role'] = {
                super = {
                    execute = true,
                },
            },
            ['space'] = {
                _priv = {
                    read = true,
                },
            },
            ['function'] = {
                ['box.schema.user.info'] = {
                    execute = true,
                }
            },
            ['sequence'] = {},
            ['universe'] = {},
            ['lua_eval'] = {},
            ['lua_call'] = {},
            ['sql'] = {},
        }
        t.assert_equals(box_privs('myrole'), exp)
        t.assert_equals(cfg_privs(config:get('credentials.roles.myrole')), exp)

        -- Intermediate representation of guest privileges.
        exp = {
            ['user'] = {},
            ['role'] = {
                super = {
                    execute = true,
                },
            },
            ['space'] = {},
            ['function'] = {},
            ['sequence'] = {},
            ['universe'] = {},
            ['lua_eval'] = {},
            ['lua_call'] = {},
            ['sql'] = {},
        }
        t.assert_equals(box_privs('guest'), exp)
        t.assert_equals(cfg_privs(config:get('credentials.users.guest')), exp)

        -- Intermediate representation of myuser privileges.
        exp = {
            ['user'] = {},
            ['role'] = {
                myrole = {
                    execute = true,
                },
                replication = {
                    execute = true,
                },
            },
            ['space'] = {
                _func = {
                    create = true,
                    write = true,
                },
                _index = {
                    drop = true,
                    read = true,
                },
                _space = {
                    create = true,
                    write = true,
                    drop = true,
                    read = true,
                },
            },
            ['function'] = {
                LUA = {
                    execute = true,
                },
            },
            ['sequence'] = {
                [''] = {
                    write = true,
                },
            },
            ['universe'] = {},
            ['lua_eval'] = {
                [''] = {
                    execute = true,
                },
            },
            ['lua_call'] = {
                [''] = {
                    execute = true,
                },
            },
            ['sql'] = {
                [''] = {
                    execute = true,
                },
            },
        }
        t.assert_equals(box_privs('myuser'), exp)
        t.assert_equals(cfg_privs(config:get('credentials.users.myuser')), exp)
    end

    helpers.success_case(g, {
        options = {credentials = credentials},
        verify = verify,
    })
end

g.test_privileges_subtract = function()
    local target = {
        ['user'] = {},
        ['role'] = {},
        ['space'] = {
            ['myspace1'] = {
                ['read'] = true,
                ['write'] = true,
            },
            ['myspace2'] = {
                ['read'] = true,
            }
        },
        ['function'] = {
            ['myfunc1'] = {
                ['execute'] = true,
            },
        },
        ['sequence'] = {
            ['myseq1'] = {
                ['read'] = true,
                ['write'] = true,
            }
        },
        ['universe'] = {},
    }

    local current = {
        ['user'] = {},
        ['role'] = {
            ['myrole1'] = {
                ['execute'] = true,
            },
        },
        ['space'] = {
            ['myspace1'] = {
                ['read'] = true,
            },
        },
        ['function'] = {
            ['myfunc1'] = {
                ['execute'] = true,
            },
        },
        ['sequence'] = {},
        ['universe'] = {},
    }

    local lack = {{
            obj_type = 'space',
            obj_name = 'myspace1',
            privs = {'write'},
        }, {
            obj_type = 'space',
            obj_name = 'myspace2',
            privs = {'read'},
        }, {
            obj_type = 'sequence',
            obj_name = 'myseq1',
            privs = {'read', 'write'},
        },
    }

    t.assert_items_equals(internal.privileges_subtract(target, current), lack)
end

g.test_sync_privileges = function(g)
    local box_configuration = {{
            "grant", "read", "universe", ""
        }, {
            "revoke", "session,usage", "universe", ""
        }, {
            "grant", "execute", "function", ""
        },
    }

    local credentials = {
        users = {
            guest = {
                roles = { 'super' }
            },
            myuser = {
                privileges = {
                    {
                        permissions = {
                            'write',
                            'execute',
                        },
                        universe = true,
                    }, {
                        permissions = {
                            'session',
                            'usage',
                        },
                        universe = true,
                    },
                },
                roles = {
                    'public',
                },
            },
        },
    }

    -- Disable hide/show prompt functionality, because it breaks
    -- a command echo check. The reason is that the 'scheduled
    -- next checkpoint' log message is issued from a background
    -- fiber.
    local child = it.new({
        env = {
            TT_CONSOLE_HIDE_SHOW_PROMPT = 'false',
        },
    })
    local dir = treegen.prepare_directory(g, {}, {})
    child:roundtrip(("box.cfg{work_dir = %q}"):format(dir))

    local name = "myuser"
    child:roundtrip(("box.schema.user.create(%q)"):format(name))
    for _, command in ipairs(box_configuration) do
        local action, perm, obj_type, obj_name = unpack(command)
        local opts = "{if_not_exists = true}"
        child:roundtrip(("box.schema.user.%s(%q, %q, %q, %q, %s)"):format(
                         action, name, perm, obj_type, obj_name, opts))
    end
    child:roundtrip("applier = require('internal.config.applier.credentials')")
    child:roundtrip("internal = applier._internal")
    child:roundtrip("aboard = require('internal.config.utils.aboard').new()")
    child:roundtrip("internal.set_config({_aboard = aboard})")
    child:roundtrip("sync_privileges = internal.sync_privileges")
    child:roundtrip("json = require('json')")
    child:roundtrip(("priv = json.decode(%q)"):format(json.encode(credentials)))
    child:roundtrip("sync_privileges(priv)")

    child:execute_command(("internal.privileges_from_box('%s')"):format(name))
    local result_privileges = child:read_response()
    local config_privileges = internal.privileges_from_config(
                                            credentials.users.myuser)

    t.assert_equals(result_privileges, config_privileges)

    child:close()
end

g.test_set_password = function(g)

    local auth_types = {
        'chap-sha1',
        -- TODO: The pap-sha256 authentication method requires
        -- an encrypted connection, so it is left untested for
        -- now.
        --
        -- 'pap-sha256',
    }

    for _, auth_type in ipairs(auth_types) do
        if auth_type == 'pap-sha256' then
            t.tarantool.skip_if_not_enterprise()
        end

        -- Disable hide/show prompt functionality, because it
        -- breaks a command echo check. The reason is that the
        -- 'scheduled next checkpoint' log message is issued from
        -- a background fiber.
        local child = it.new({
            env = {
                TT_CONSOLE_HIDE_SHOW_PROMPT = 'false',
            },
        })

        local dir = treegen.prepare_directory(g, {}, {})
        local socket = "unix/:./test_socket.iproto"

        child:roundtrip(("box.cfg{work_dir = %q, listen = %q, auth_type = %q}")
                        :format(dir, socket, auth_type))
        child:roundtrip("nb = require('net.box')")

        local name = "myuser"
        child:roundtrip(("myuser = %q"):format(name))
        child:roundtrip("box.schema.user.create(myuser)")

        child:roundtrip("set_password = require('internal.config.applier." ..
                        "credentials')._internal.set_password")

        child:roundtrip("set_password(myuser, 'password1')")

        child:roundtrip(("nb.connect(%q, {user = myuser, " ..
                         "password = 'password1'}).state"):format(socket),
                        "active")

        child:roundtrip("set_password(myuser, 'password2')")

        child:roundtrip(("nb.connect(%q, {user = myuser, " ..
                         "password = 'password1'}).state"):format(socket),
                        "error")

        child:roundtrip(("nb.connect(%q, {user = myuser, " ..
                         "password = 'password2'}).state"):format(socket),
                        "active")

        child:roundtrip("set_password(myuser, nil)")

        child:roundtrip(("nb.connect(%q, {user = myuser, " ..
                         "password = 'password2'}).state"):format(socket),
                        "error")
        child:close()
    end
end

g.test_remove_user_role = function(g)
    -- Verify that when user or role is removed from the config,
    -- it is not being deleted.

    -- Whole removed user/role configuration is expected to be left
    -- as is after the reload, so verification functions for before/after
    -- reload are the same.
    local verify = function()
        local ok, err = pcall(box.schema.user.info, 'myuser')
        t.assert(ok, err)
        ok, err = pcall(box.schema.role.info, 'myrole')
        t.assert(ok, err)
        local internal =
                require('internal.config.applier.credentials')._internal

        local guest_perm = internal.privileges_from_box('guest')
        t.assert(guest_perm['role']['super'].execute)

        local user_perm = internal.privileges_from_box('myuser')
        t.assert(user_perm['universe'][''].execute)

        local role_perm = internal.privileges_from_box('myrole')
        t.assert(role_perm['universe'][''].read)
        t.assert(role_perm['universe'][''].write)
    end

    helpers.reload_success_case(g, {
        options = {
            credentials = {
                roles = {
                    myrole = {
                        privileges = {{
                            permissions = {
                                'read',
                                'write',
                            },
                            universe = true,
                        }}
                    },
                },
                users = {
                    guest = {
                        roles = { 'super' }
                    },
                    myuser = {
                        privileges = {{
                            permissions = {
                                'execute',
                            },
                            universe = true,
                        }},
                    },
                }
            }
        },
        verify = verify,
        options_2 = {
            credentials = {
                users = {
                    guest = {
                        roles = { 'super' }
                    },
                }
            }
        },
        verify_2 = verify,
    })
end

g.test_remove_excessive_privs_for_default_user = function(g)
    -- Verify that if the default users and roles are not present in config
    -- their excessive privileges granted by the config are revoked.

    helpers.reload_success_case(g, {
        options = {
            credentials = {
                roles = {
                    dummy = { },
                    super = {
                        roles = { 'dummy' },
                    },
                    public = {
                        roles = { 'dummy' },
                    },
                    replication = {
                        roles = { 'dummy' },
                    },
                },
                users = {
                    guest = {
                        roles = { 'super', 'dummy' }
                    },
                    admin = {
                        roles = { 'dummy' }
                    },
                }
            }
        },
        verify = function()
            local credentials = require('internal.config.applier.credentials')
            local internal = credentials._internal
            local default_identities = {'admin', 'guest', 'super', 'public',
                                        'replication'}
            for _, name in ipairs(default_identities) do
                local perm = internal.privileges_from_box(name)
                t.assert_equals(perm['role']['dummy'], {execute = true})
            end
        end,
        options_2 = {
            credentials = {
                users = {
                    guest = {
                        roles = { 'super' }
                    }
                }
            }
        },
        verify_2 = function()
            local credentials = require('internal.config.applier.credentials')
            local internal = credentials._internal
            local default_identities = {'admin', 'guest', 'super', 'public',
                                        'replication'}
            for _, name in ipairs(default_identities) do
                local perm = internal.privileges_from_box(name)
                t.assert_not_equals(perm['role']['dummy'], {execute = true})
            end
        end,
    })
end

g.test_sync_ro_rw = function(g)
    helpers.reload_success_case(g, {
        options = {
            credentials = {
                users = {
                    guest = {
                        roles = { 'super' }
                    },
                }
            }
        },
        verify = function() end,
        options_2 = {
            database = {
                mode = 'ro',
            },
            credentials = {
                roles = {
                    dummy = {},
                },
                users = {
                    guest = {
                        roles = { 'super', 'dummy' }
                    },
                }
            }
        },
        verify_2 = function()
            t.assert(box.info.ro)

            local internal =
                    require('internal.config.applier.credentials')._internal

            local perm = internal.privileges_from_box('guest')
            t.assert_not_equals(perm['role']['dummy'], {execute = true})

            box.cfg{read_only = false}
            t.assert_not(box.info.ro)

            local retrying = require('luatest.helpers').retrying

            retrying(
                {timeout = 10, delay = 0.5},
                function()
                    local perm = internal.privileges_from_box('guest')
                    t.assert_equals(perm['role']['dummy'], {execute = true})
                end
            )
        end,
    })
end

g.test_postpone_grants_till_creation = function(g)
    local iconfig = {
        credentials = {
            users = {
                guest = {
                    roles = { 'super' }
                },
                myuser1 = {
                    privileges = {
                        {
                            permissions = {
                                'read',
                                'write',
                            },
                            spaces = {
                                'myspace1',
                                'myspace2',
                            },
                            sequences = {
                                'myseq1',
                                'myseq2',
                            }
                        }, {
                            permissions = {
                                'execute',
                            },
                            functions = {
                                'myfunc1',
                                'myfunc2',
                            },
                        },
                    },
                },
                myuser2 = {
                    privileges = {
                        {
                            permissions = {
                                'read',
                                'write',
                            },
                            spaces = {
                                'myspace1',
                                'myspace3',
                            },
                            sequences = {
                                'myseq1',
                                'myseq3',
                            }
                        }, {
                            permissions = {
                                'execute',
                            },
                            functions = {
                                'myfunc1',
                                'myfunc3',
                            },
                        },
                    },
                },
            },
        },
    }

    -- Check that grants are correctly postponed and applied when object
    -- is being created at runtime.
    helpers.reload_success_case(g, {
        options = iconfig,
        verify = function()
            box.schema.space.create('myspace1')
            box.schema.func.create('myfunc2')
            box.schema.sequence.create('myseq3')

            local internal =
                    require('internal.config.applier.credentials')._internal

            local perm_1 = internal.privileges_from_box('myuser1')
            local perm_2 = internal.privileges_from_box('myuser2')

            t.assert_equals(perm_1['space']['myspace1'],
                                {read = true, write = true})
            t.assert_equals(perm_2['space']['myspace1'],
                                {read = true, write = true})
            t.assert_equals(perm_1['function']['myfunc2'],
                                {execute = true})
            t.assert_equals(perm_2['function']['myfunc2'], nil)
            t.assert_equals(perm_1['sequence']['myseq3'], nil)
            t.assert_equals(perm_2['sequence']['myseq3'],
                                {read = true, write = true})

            t.assert_equals(perm_1['space']['myspace2'], nil)
            t.assert_equals(perm_2['space']['myspace3'], nil)
            t.assert_equals(perm_1['function']['myfunc1'], nil)
            t.assert_equals(perm_2['function']['myfunc3'], nil)
            t.assert_equals(perm_1['sequence']['myseq1'], nil)
            t.assert_equals(perm_2['sequence']['myseq1'], nil)
        end,
        options_2 = iconfig,
        verify_2 = function(iconfig)
            box.schema.space.create('myspace2')
            box.schema.space.create('myspace3')
            box.schema.func.create('myfunc1')
            box.schema.func.create('myfunc3')
            box.schema.sequence.create('myseq1')
            box.schema.sequence.create('myseq2')

            local internal =
                    require('internal.config.applier.credentials')._internal

            local perm_1 = internal.privileges_from_box('myuser1')
            local perm_2 = internal.privileges_from_box('myuser2')

            local exp_perm_1 = iconfig.credentials.users.myuser1
            exp_perm_1 = internal.privileges_from_config(exp_perm_1)

            local exp_perm_2 = iconfig.credentials.users.myuser2
            exp_perm_2 = internal.privileges_from_config(exp_perm_2)

            t.assert_equals(perm_1, exp_perm_1)
            t.assert_equals(perm_2, exp_perm_2)

            -- Recheck relevant permissions without privileges_from_config()
            -- and privileges_add_defaults() helper function, in case they
            -- are faulty.
            t.assert_equals(perm_1['space']['myspace1'],
                                {read = true, write = true})
            t.assert_equals(perm_1['space']['myspace2'],
                                {read = true, write = true})
            t.assert_equals(perm_1['sequence']['myseq1'],
                                {read = true, write = true})
            t.assert_equals(perm_1['sequence']['myseq2'],
                                {read = true, write = true})
            t.assert_equals(perm_1['function']['myfunc1'],
                                {execute = true})
            t.assert_equals(perm_1['function']['myfunc2'],
                                {execute = true})

            t.assert_equals(perm_2['space']['myspace1'],
                                {read = true, write = true})
            t.assert_equals(perm_2['space']['myspace3'],
                                {read = true, write = true})
            t.assert_equals(perm_2['sequence']['myseq1'],
                                {read = true, write = true})
            t.assert_equals(perm_2['sequence']['myseq3'],
                                {read = true, write = true})
            t.assert_equals(perm_2['function']['myfunc1'],
                                {execute = true})
            t.assert_equals(perm_2['function']['myfunc3'],
                                {execute = true})

        end,
        verify_args_2 = {iconfig},
    })
end

g.test_postpone_grants_till_rename = function(g)
    -- Check that basic object rename is handled correctly.
    helpers.success_case(g, {
        options = {
            credentials = {
                users = {
                    guest = {
                        roles = { 'super' }
                    },
                    myuser1 = {
                        privileges = {
                            {
                                permissions = {
                                    'read',
                                    'write',
                                },
                                spaces = {
                                    'myspace1',
                                },
                            },
                        },
                    },
                },
            },
        },
        verify = function()
            box.schema.space.create('myspace2')

            local internal =
                    require('internal.config.applier.credentials')._internal

            local perm_1 = internal.privileges_from_box('myuser1')
            t.assert_equals(perm_1['space']['myspace1'], nil)

            box.space['myspace2']:rename('myspace1')

            local perm_2 = internal.privileges_from_box('myuser1')
            t.assert_equals(perm_2['space']['myspace1'],
                                {read = true, write = true})

            t.assert_equals(perm_2['space']['myspace2'], nil)
        end
    })

    -- Test that when one object is renamed to another with colliding
    -- permissions. Both objects should have correct permissions after
    -- each creation/rename, because it is their first appearance after
    -- reload.

    local iconfig = {
        credentials = {
            users = {
                guest = {
                    roles = { 'super' }
                },
                myuser = {
                    privileges = {
                        {
                            permissions = {
                                'read',
                            },
                            spaces = {
                                'myspace1',
                            },
                        }, {
                            permissions = {
                                'read',
                                'write',
                            },
                            spaces = {
                                'myspace2',
                            },
                        },
                    },
                },
            },
        },
    }

    helpers.reload_success_case(g, {
        options = iconfig,
        verify = function()
            box.schema.space.create('myspace1')

            local internal =
                    require('internal.config.applier.credentials')._internal

            local perm_1 = internal.privileges_from_box('myuser')
            t.assert_equals(perm_1['space']['myspace1'],
                                {read = true})

            t.assert_equals(perm_1['space']['myspace2'], nil)

            box.space['myspace1']:rename('myspace2')

            local perm_2 = internal.privileges_from_box('myuser')
            t.assert_equals(perm_2['space']['myspace2'],
                                {read = true, write = true})

            t.assert_equals(perm_2['space']['myspace1'], nil)
        end,
        options_2 = iconfig,
        verify_2 = function(iconfig)
            box.schema.space.create('myspace1')

            local internal =
                    require('internal.config.applier.credentials')._internal

            local perm = internal.privileges_from_box('myuser')
            local exp_perm = iconfig.credentials.users.myuser
            exp_perm = internal.privileges_from_config(exp_perm)
            t.assert_equals(perm, exp_perm)

            -- Recheck relevant permissions without privileges_from_config()
            -- and privileges_add_defaults() helper function, in case they
            -- are faulty.
            t.assert_equals(perm['space']['myspace1'],
                                {read = true})
            t.assert_equals(perm['space']['myspace2'],
                                {read = true, write = true})
        end,
        verify_args_2 = {iconfig},
    })


    -- Switch names of two spaces and check that privileges are
    -- in sync with config.
    iconfig  = {
        credentials = {
            users = {
                guest = {
                    roles = { 'super' }
                },
                myuser = {
                    privileges = {
                        {
                            permissions = {
                                'write',
                            },
                            spaces = {
                                'myspace1',
                            },
                        }, {
                            permissions = {
                                'read',
                                'alter',
                            },
                            spaces = {
                                'myspace2',
                            },
                        },
                    },
                },
            },
        },
    }

    helpers.success_case(g, {
        options = iconfig,
        verify = function(iconfig)
            local internal =
                    require('internal.config.applier.credentials')._internal

            local function check_sync()

                local perm = internal.privileges_from_box('myuser')
                local exp_perm = iconfig.credentials.users.myuser
                exp_perm = internal.privileges_from_config(exp_perm)
                t.assert_equals(perm, exp_perm)

                -- Recheck relevant permissions without privileges_from_config()
                -- and privileges_add_defaults() helper function, in case they
                -- are faulty.
                t.assert_equals(perm['space']['myspace1'],
                                    {write = true})
                t.assert_equals(perm['space']['myspace2'],
                                    {read = true, alter = true})
            end

            box.schema.space.create('myspace1')
            box.schema.space.create('myspace2')

            check_sync()

            box.space.myspace1:rename('myspace_tmp')
            box.space.myspace2:rename('myspace1')
            box.space.myspace_tmp:rename('myspace2')

            check_sync()
        end,
        verify_args = {iconfig},
    })
end

g.test_lua_eval_lua_call_sql = function()
    helpers.reload_success_case(g, {
        options = {
            credentials = {
                users = {
                    guest = {
                        roles = { 'super' }
                    },
                    myuser = {
                        password = 'secret',
                        privileges = {
                            {
                                permissions = {
                                    'execute',
                                },
                                lua_eval = true,
                                lua_call = {
                                    'all',
                                },
                                sql = {
                                    'all',
                                },
                            }
                        },
                    }
                },
            },
            iproto = {
                listen = {{uri = 'unix/:./test.iproto'}},
            },
        },
        verify = function()
            local conn = require('net.box').connect(
                'unix/:./test.iproto',
                {
                    user = 'myuser',
                    password = 'secret',
                }
            )
            t.assert(conn:eval('return true'))

            conn:eval([[function myfunc() return true end]])
            t.assert(conn:call('myfunc'))

            t.assert(conn:execute('SELECT 1'))
        end,
        options_2 = {
            credentials = {
                users = {
                    guest = {
                        roles = { 'super' }
                    },
                    myuser = {
                        password = 'secret',
                    },
                },
            },
            iproto = {
                listen = {{uri = 'unix/:./test.iproto'}},
            },
        },
        verify_2 = function()
            local conn = require('net.box').connect(
                'unix/:./test.iproto',
                {
                    user = 'myuser',
                    password = 'secret',
                }
            )
            t.assert_error(conn.eval, conn, 'return true')

            -- `myfunc` already exists.
            t.assert_error(conn.call, conn, 'myfunc')

            t.assert_error(conn.execute, conn, 'SELECT 1')
        end
    })
end

g.test_consider_auth_type_for_passwods = function(g)
    t.tarantool.skip_if_not_enterprise()

    helpers.reload_success_case(g, {
        options = {
            credentials = {
                users = {
                    guest = {
                        roles = { 'super' }
                    },
                    myuser = {
                        password = 'secret',
                    },
                },
            },
            security = {
                auth_type = 'chap-sha1',
            },
        },
        verify = function()
            t.assert_equals(box.cfg.auth_type, 'chap-sha1')

            local password_def = box.space._user.index.name:get({'myuser'})[5]
            t.assert_equals(type(password_def['chap-sha1']), 'string')
        end,
        options_2 = {
            credentials = {
                users = {
                    guest = {
                        roles = { 'super' }
                    },
                    myuser = {
                        password = 'secret',
                    },
                },
            },
            security = {
                auth_type = 'pap-sha256',
            },
        },
        verify_2 = function()
            t.assert_equals(box.cfg.auth_type, 'pap-sha256')

            local password_def = box.space._user.index.name:get({'myuser'})[5]
            t.assert_equals(type(password_def['pap-sha256']), 'table')
        end,
    })
end

-- Verify that all the missed permissions are reported in
-- config:info().alerts.
--
-- In this case,
--
-- * s read
-- * s write
-- * t read
-- * t write
g.test_delayed_grant_alert = function(g)
    helpers.success_case(g, {
        options = {
            ['credentials.users.guest.privileges'] = {
                {
                    permissions = {'read', 'write'},
                    spaces = {'s', 't'},
                },
            },
        },
        verify = function(warnings)
            local config = require('config')

            t.assert_equals({
                status = config:info().status,
                alerts = loadstring(warnings)(),
            }, {
                status = 'check_warnings',
                alerts = {
                    's read',
                    's write',
                    't read',
                    't write',
                },
            })
        end,
        verify_args = {string.dump(warnings)},
    })
end

-- Verify that an alert regarding a delayed privilege granting
-- (due to lack of a space/function/sequence) is dropped, when the
-- privilege is finally granted.
--
-- This test case starts an instance with a configuration that
-- contains permissions on space 's'. The startup has the
-- following steps.
--
-- 1. The new database is up.
-- 2. The credentials applier sees the privileges and sees that
--    space 's' does not exist. It issues an alert.
-- 3. The application script is started and it creates the space.
-- 4. The credentials applier wakes up (it has a trigger), grants
--    the requested privileges and eliminates the alert that is
--    issued on the step 2.
--
-- This test case verifies that the last step actually eliminates
-- the alert.
g.test_delayed_grant_alert_dropped = function(g)
    helpers.success_case(g, {
        script = string.dump(function()
            box.once('app', function()
                box.schema.space.create('s')
                box.space.s:create_index('pk')
            end)
        end),
        options = {
            ['app.file'] = 'main.lua',
            ['credentials.users.guest.privileges'] = {
                {
                    permissions = {'read', 'write'},
                    spaces = {'s'},
                },
            },
        },
        verify = function()
            local config = require('config')

            local info = config:info()
            t.assert_equals({
                status = info.status,
                alerts = info.alerts,
            }, {
                status = 'ready',
                alerts = {},
            })
        end,
    })
end

-- Verify how alerts are working in a scenario with renaming of a
-- space.
--
-- Create a space and rename it to a space that should have the
-- same privileges.
g.test_space_rename_rw2rw = function(g)
    -- Create a configuration with privileges for two spaces.
    local config = cbuilder.new()
        :add_instance('i-001', {})
        :set_global_option('credentials.users.guest.privileges', {
            {
                permissions = {'read', 'write'},
                spaces = {'src'},
            },
            {
                permissions = {'read', 'write'},
                spaces = {'dest'},
            },
        })
        :config()

    local cluster = cluster.new(g, config)
    cluster:start()
    define_warnings_function(cluster['i-001'])
    define_assert_priv_function(cluster['i-001'])

    -- Verify that alerts are set for delayed privilege grants
    -- for the given spaces.
    cluster['i-001']:exec(function()
        t.assert_equals(_G.warnings(), {
            'dest read',
            'dest write',
            'src read',
            'src write',
        })
    end)

    -- Create space 'src'.
    cluster['i-001']:exec(function()
        box.schema.space.create('src')
        box.space.src:create_index('pk')
    end)

    -- Verify that privileges are granted for the space 'src'.
    cluster['i-001']:call('assert_priv', {'src', 'read,write'})

    -- Verify that the alerts regarding the space 'src' are gone.
    cluster['i-001']:exec(function()
        t.assert_equals(_G.warnings(), {
            'dest read',
            'dest write',
        })
    end)

    -- Rename 'src' to 'dest'.
    cluster['i-001']:exec(function()
        box.space.src:rename('dest')
    end)

    -- Verify that the proper privileges are kept for the space
    -- 'dest'.
    cluster['i-001']:call('assert_priv', {'dest', 'read,write'})

    -- Verify that the alert regarding the 'dest' space is gone.
    --
    -- Also verify that the alerts are set for the space 'src',
    -- which is gone after the rename.
    cluster['i-001']:exec(function()
        t.assert_equals(_G.warnings(), {
            'src read',
            'src write',
        })
    end)
end

-- Verify how alerts are working in a scenario with renaming of a
-- space.
--
-- Create a space with the 'read' privilege and rename it to a
-- space that should have 'read,write' privileges.
g.test_space_rename_r2rw = function(g)
    -- Create a configuration with different privileges for two
    -- spaces.
    local config = cbuilder.new()
        :add_instance('i-001', {})
        :set_global_option('credentials.users.guest.privileges', {
            {
                permissions = {'read'},
                spaces = {'src'},
            },
            {
                permissions = {'read', 'write'},
                spaces = {'dest'},
            },
        })
        :config()

    local cluster = cluster.new(g, config)
    cluster:start()
    define_warnings_function(cluster['i-001'])
    define_assert_priv_function(cluster['i-001'])

    -- Verify that alerts are set for delayed privilege grants
    -- for the given spaces.
    cluster['i-001']:exec(function()
        t.assert_equals(_G.warnings(), {
            'dest read',
            'dest write',
            'src read',
        })
    end)

    -- Create space 'src'.
    cluster['i-001']:exec(function()
        box.schema.space.create('src')
        box.space.src:create_index('pk')
    end)

    -- Verify that privileges are granted for the space 'src'.
    cluster['i-001']:call('assert_priv', {'src', 'read'})

    -- Verify that the alerts regarding the space 'src' are gone.
    cluster['i-001']:exec(function()
        t.assert_equals(_G.warnings(), {
            'dest read',
            'dest write',
        })
    end)

    -- Rename 'src' to 'dest'.
    cluster['i-001']:exec(function()
        box.space.src:rename('dest')
    end)

    -- Verify privileges are adjusted for the space 'dest'.
    cluster['i-001']:call('assert_priv', {'dest', 'read,write'})

    -- Verify that the alerts regarding the 'dest' space are gone.
    --
    -- Also verify that the alert is set for the space 'src',
    -- which is gone after the rename.
    cluster['i-001']:exec(function()
        t.assert_equals(_G.warnings(), {
            'src read',
        })
    end)
end

-- Verify how alerts are working in a scenario with renaming of a
-- space.
--
-- Create a space with 'read,write' privileges and rename it to a
-- space that should have just 'read' privilege.
g.test_space_rename_rw2r = function(g)
    -- Create a configuration with different privileges for two
    -- spaces.
    local config = cbuilder.new()
        :add_instance('i-001', {})
        :set_global_option('credentials.users.guest.privileges', {
            {
                permissions = {'read', 'write'},
                spaces = {'src'},
            },
            {
                permissions = {'read'},
                spaces = {'dest'},
            },
        })
        :config()

    local cluster = cluster.new(g, config)
    cluster:start()
    define_warnings_function(cluster['i-001'])
    define_assert_priv_function(cluster['i-001'])

    -- Verify that alerts are set for delayed privilege grants
    -- for the given spaces.
    cluster['i-001']:exec(function()
        t.assert_equals(_G.warnings(), {
            'dest read',
            'src read',
            'src write',
        })
    end)

    -- Create space 'src'.
    cluster['i-001']:exec(function()
        box.schema.space.create('src')
        box.space.src:create_index('pk')
    end)

    -- Verify that privileges are granted for the space 'src'.
    cluster['i-001']:call('assert_priv', {'src', 'read,write'})

    -- Verify that the alerts regarding the space 'src' are gone.
    cluster['i-001']:exec(function()
        t.assert_equals(_G.warnings(), {
            'dest read',
        })
    end)

    -- Rename 'src' to 'dest'.
    cluster['i-001']:exec(function()
        box.space.src:rename('dest')
    end)

    -- Verify privileges are adjusted for the space 'dest'.
    cluster['i-001']:call('assert_priv', {'dest', 'read'})

    -- Verify that the alert regarding the 'dest' space is gone.
    --
    -- Also verify that the alerts are set for the space 'src',
    -- which is gone after the rename.
    cluster['i-001']:exec(function()
        t.assert_equals(_G.warnings(), {
            'src read',
            'src write',
        })
    end)
end

-- Verify that the config status does not change if there are pending
-- alerts when some missed_privilege alerts are dropped.
g.test_fix_status_change_with_pending_warnings = function(g)
    -- Create a configuration with privileges for two missing spaces.
    local config = cbuilder.new()
        :add_instance('i-001', {})
        :set_global_option('credentials.users.guest.privileges', {
            {
                permissions = {'read'},
                spaces = {'one', 'two'},
            },
        })
        :config()

    local cluster = cluster.new(g, config)
    cluster:start()
    define_warnings_function(cluster['i-001'])

    -- Verify that alerts are set for delayed privilege grants
    -- for the given spaces and status is 'check_warnings'.
    cluster['i-001']:exec(function()
        t.assert_equals(_G.warnings(), {
            'one read',
            'two read',
        })
        t.assert_equals(require('config'):info().status, 'check_warnings')
    end)

    -- Create space 'one'.
    cluster['i-001']:exec(function()
        box.schema.space.create('one')
    end)

    -- Verify that the alerts regarding the space 'one' are gone,
    -- but the status is still 'check_warnings'.
    cluster['i-001']:exec(function()
        t.assert_equals(_G.warnings(), {
            'two read',
        })
        t.assert_equals(require('config'):info().status, 'check_warnings')
    end)

    -- Create space 'two'.
    cluster['i-001']:exec(function()
        box.schema.space.create('two')
    end)

    -- Verify than all alerts are gone and the status is 'ready'.
    cluster['i-001']:exec(function()
        t.assert_equals(_G.warnings(), {})
        t.assert_equals(require('config'):info().status, 'ready')
    end)
end

-- Verify that it is possible to assign a credential role that does not exist.
-- And that this role will be correctly assigned when it is created.
g.test_set_nonexistent_role = function(g)
    local config = cbuilder.new()
        :add_instance('i-001', {})
        :set_global_option('credentials.roles.role_one', {
            roles = {'role_two'},
        })
        :config()

    local cluster = cluster.new(g, config)
    cluster:start()

    -- Verify that an alert is set for delayed privilege grants
    -- for the given role.
    cluster['i-001']:exec(function()
        local info = require('config'):info()
        local exp = 'box.schema.role.grant("role_one", "execute", "role", ' ..
            '"role_two") has failed because either the object has not been ' ..
            'created yet, a database schema upgrade has not been performed, ' ..
            'or the privilege write has failed (separate alert reported)'
        t.assert_equals(info.status, 'check_warnings')
        t.assert_equals(#info.alerts, 1)
        t.assert_equals(info.alerts[1].type, 'warn')
        t.assert_equals(tostring(info.alerts[1].message), exp)
    end)

    -- Verify that the alert is dropped after `role_two` is created.
    cluster['i-001']:exec(function()
        box.schema.role.create('role_two')
        local info = require('config'):info()
        local exp = {{"execute", "role", "role_two"}}
        t.assert_equals(info.status, 'ready')
        t.assert_equals(#info.alerts, 0)
        t.assert_equals(box.schema.role.info('role_one'), exp)
    end)
end

-- Verify that alert does not go away if the user is created with a given name
-- instead of a role.
g.test_set_user_instead_of_role = function(g)
    local config = cbuilder.new()
        :add_instance('i-001', {})
        :set_global_option('credentials.users.user_one', {
            roles = {'role_two'},
        })
        :config()

    local cluster = cluster.new(g, config)
    cluster:start()

    -- Verify that the alert is set.
    cluster['i-001']:exec(function()
        local info = require('config'):info()
        local exp = 'box.schema.user.grant("user_one", "execute", "role", ' ..
            '"role_two") has failed because either the object has not been ' ..
            'created yet, a database schema upgrade has not been performed, ' ..
            'or the privilege write has failed (separate alert reported)'
        t.assert_equals(info.status, 'check_warnings')
        t.assert_equals(#info.alerts, 1)
        t.assert_equals(info.alerts[1].type, 'warn')
        t.assert_equals(tostring(info.alerts[1].message), exp)
    end)

    -- Verify that the alert is still present after creating user 'role_two'
    -- instead of role 'role_two'.
    cluster['i-001']:exec(function()
        box.schema.user.create('role_two')
        local info = require('config'):info()
        local exp = 'box.schema.user.grant("user_one", "execute", "role", ' ..
            '"role_two") has failed because either the object has not been ' ..
            'created yet, a database schema upgrade has not been performed, ' ..
            'or the privilege write has failed (separate alert reported)'
        t.assert_equals(info.status, 'check_warnings')
        t.assert_equals(#info.alerts, 1)
        t.assert_equals(info.alerts[1].type, 'warn')
        t.assert_equals(tostring(info.alerts[1].message), exp)
    end)

    -- Verify that the alert is dropped after 'role_two' is created.
    cluster['i-001']:exec(function()
        box.schema.user.drop('role_two')
        box.schema.role.create('role_two')
        local info = require('config'):info()
        t.assert_equals(info.status, 'ready')
        t.assert_equals(#info.alerts, 0)
    end)
end
