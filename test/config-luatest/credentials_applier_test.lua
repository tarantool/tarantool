local json = require('json')
local it = require('test.interactive_tarantool')
local t = require('luatest')
local treegen = require('test.treegen')
local helpers = require('test.config-luatest.helpers')

local g = helpers.group()

local internal = require('internal.config.applier.credentials')._internal

g.before_all(function(g)
    treegen.init(g)
end)

g.after_all(function(g)
    treegen.clean(g)
end)

g.test_converters = function()
    -- Guest privileges in format provided by box.schema.{user,role}.info()
    local box_guest_privileges = {{
            'execute',
            'role',
            'public',
        }, {
            'session,usage',
            'universe',
        }, {
            'execute',
            'lua_call',
        }, {
            'execute',
            'lua_eval',
        }
    }

    -- Guest privileges in format provided by config schema
    local config_guest_data = {
        privileges = {{
                permissions = {
                    'session',
                    'usage'
                },
                universe = true,
            }, {
                permissions = {
                    'execute',
                },
                lua_eval = true,
                lua_call = {
                    'all'
                },
            }
        },
        roles = {
            'public'
        },
    }

    -- Guest privileges in format of intermediate representation
    local intermediate_guest_privileges = {
        ['user'] = {},
        ['role'] = {
            ['public'] = {
                ['execute'] = true
            }
        },
        ['space'] = {},
        ['function'] = {},
        ['sequence'] = {},
        ['universe'] = {
            [''] = {
                ['session'] = true,
                ['usage'] = true,
            }
        },
        ['lua_eval'] = {
            [''] = {
                ['execute'] = true,
            },
        },
        ['lua_call'] = {
            [''] = {
                ['execute'] = true,
            },
        },
        ['sql'] = {},
    }

    t.assert_equals(internal.privileges_from_box(box_guest_privileges),
                    intermediate_guest_privileges)

    t.assert_equals(internal.privileges_from_config(config_guest_data),
                    intermediate_guest_privileges)


    local box_admin_privileges = {{
            'read,write,execute,session,usage,create,drop,alter,reference,' ..
            'trigger,insert,update,delete',
            'universe'
        },
    }

    local config_admin_data = {
        privileges = {{
                permissions = {
                    'read',
                    'write',
                    'execute',
                    'session',
                    'usage',
                    'create',
                    'drop',
                    'alter',
                    'reference',
                    'trigger',
                    'insert',
                    'update',
                    'delete',
                },
                universe = true,
            },
        }
    }

    local intermediate_admin_privileges = {
        ['user'] = {},
        ['role'] = {},
        ['space'] = {},
        ['function'] = {},
        ['sequence'] = {},
        ['universe'] = {
            [''] = {
                ['read'] = true,
                ['write'] = true,
                ['execute'] = true,
                ['session'] = true,
                ['usage'] = true,
                ['create'] = true,
                ['drop'] = true,
                ['alter'] = true,
                ['reference'] = true,
                ['trigger'] = true,
                ['insert'] = true,
                ['update'] = true,
                ['delete'] = true,
            }
        },
        ['lua_eval'] = {},
        ['lua_call'] = {},
        ['sql'] = {},
    }

    t.assert_equals(internal.privileges_from_box(box_admin_privileges),
                    intermediate_admin_privileges)

    t.assert_equals(internal.privileges_from_config(config_admin_data),
                    intermediate_admin_privileges)

    local box_replication_privileges = {{
            'write',
            'space',
            '_cluster',
        }, {
            'read',
            'universe',
        },
    }

    local config_replication_data = {
        privileges = {{
                permissions = {
                    'write'
                },
                spaces = {
                    '_cluster',
                },
            }, {
                permissions = {
                    'read'
                },
                universe = true,
            },
        }
    }

    local intermediate_replication_privileges = {
        ['user'] = {},
        ['role'] = {},
        ['space'] = {
            ['_cluster'] = {
                ['write'] = true,
            },
        },
        ['function'] = {},
        ['sequence'] = {},
        ['universe'] = {
            [''] = {
                ['read'] = true,
            },
        },
        ['lua_eval'] = {},
        ['lua_call'] = {},
        ['sql'] = {},
    }

    t.assert_equals(internal.privileges_from_box(box_replication_privileges),
                    intermediate_replication_privileges)

    t.assert_equals(internal.privileges_from_config(config_replication_data),
                    intermediate_replication_privileges)


    local box_custom_privileges = {{
            'read,write',
            'space',
        }, {
            'read,write',
            'sequence',
            'myseq1',
        }, {
            'read,write',
            'sequence',
            'myseq2',
        }, {
            'execute',
            'function',
            'myfunc1',
        }, {
            'execute',
            'function',
            'myfunc2',
        }, {
            'read',
            'universe',
        }, {
            'execute',
            'role',
            'myrole1',
        }, {
            'execute',
            'role',
            'myrole2',
        }, {
            'execute',
            'role',
            'public',
        }, {
            'execute',
            'lua_call',
        },
    }

    local config_custom_data = {
        privileges = {{
                permissions = {
                    'read',
                    'write',
                },
                spaces = {
                    'all',
                },
                sequences = {
                    'myseq1',
                    'myseq2',
                },
            }, {
                permissions = {
                    'execute',
                },
                functions = {
                    'myfunc1',
                    'myfunc2',
                },
            }, {
                permissions = {
                    'read',
                },
                universe = true,
            }, {
                permissions = {
                    'execute',
                },
                lua_call = {
                    'all'
                },
            },
        },
        roles = {
            'myrole1',
            'myrole2',
            'public',
        }
    }

    local intermediate_custom_privileges = {
        ['user'] = {},
        ['role'] = {
            ['myrole1'] = {
                ['execute'] = true,
            },
            ['myrole2'] = {
                ['execute'] = true,
            },
            ['public'] = {
                ['execute'] = true,
            },
        },
        ['space'] = {
            [''] = {
                ['read'] = true,
                ['write'] = true,
            },
        },
        ['function'] = {
            ['myfunc1'] = {
                ['execute'] = true,
            },
            ['myfunc2'] = {
                ['execute'] = true,
            },
        },
        ['sequence'] = {
            ['myseq1'] = {
                ['read'] = true,
                ['write'] = true,
            },
            ['myseq2'] = {
                ['read'] = true,
                ['write'] = true,
            },
        },
        ['universe'] = {
            [''] = {
                ['read'] = true,
            },
        },
        ['lua_eval'] = {},
        ['lua_call'] = {
            [''] = {
                ['execute'] = true,
            },
        },
        ['sql'] = {},
    }

    t.assert_equals(internal.privileges_from_box(box_custom_privileges),
                    intermediate_custom_privileges)

    t.assert_equals(internal.privileges_from_config(config_custom_data),
                    intermediate_custom_privileges)
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

g.test_privileges_add_defaults = function(g)
    local cases = {
        {'user', 'guest'},
        {'user', 'admin'},
        {'user', '<newly_created>'},
        {'role', 'public'},
        {'role', 'replication'},
        {'role', 'super'},
        {'role', '<newly_created>'},
    }

    for _, case in ipairs(cases) do
        local role_or_user, name = unpack(case)

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

        child:execute_command(("box.cfg{work_dir = %q}"):format(dir))
        child:read_response()
        if name == '<newly_created>' then
            name = 'somerandomname'
            child:execute_command(("box.schema.%s.create('%s')"):format(
                                   role_or_user, name))
            child:read_response()
        end
        child:execute_command(("box.schema.%s.info('%s')"):format(role_or_user,
                                                                  name))
        local box_privileges = child:read_response()
        box_privileges = internal.privileges_from_box(box_privileges)

        local defaults = {
            ['user'] = {},
            ['role'] = {},
            ['space'] = {},
            ['function'] = {},
            ['sequence'] = {},
            ['universe'] = {},
            ['lua_eval'] = {},
            ['lua_call'] = {},
            ['sql'] = {},
        }
        defaults = internal.privileges_add_defaults(name, role_or_user,
                                                    defaults)

        t.assert_equals(defaults, box_privileges)

        child:close()
    end
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
    child:roundtrip("sync_privileges = require('internal.config.applier." ..
                    "credentials')._internal.sync_privileges")
    child:roundtrip("json = require('json')")
    child:roundtrip(("credentials = json.decode(%q)"):format(
                     json.encode(credentials)))
    child:roundtrip(("sync_privileges(credentials)")
                    :format(name))

    child:execute_command(("box.schema.user.info('%s')"):format(name))
    local result_privileges = child:read_response()

    result_privileges = internal.privileges_from_box(result_privileges)
    local config_privileges = internal.privileges_from_config(
                                            credentials.users.myuser)

    config_privileges = internal.privileges_add_defaults(name, "user",
                                                         config_privileges)

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

        local guest_perm = box.schema.user.info('guest')
        guest_perm = internal.privileges_from_box(guest_perm)

        t.assert(guest_perm['role']['super'].execute)

        local user_perm = box.schema.user.info('myuser')
        user_perm = internal.privileges_from_box(user_perm)

        t.assert(user_perm['universe'][''].execute)

        local role_perm = box.schema.role.info('myrole')
        role_perm = internal.privileges_from_box(role_perm)

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

g.test_restore_defaults_for_default_user = function(g)
    -- Verify that if the default users and roles are not present in config
    -- their excessive privileges are revoked (restored to built-in defaults).

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
            local internal =
                    require('internal.config.applier.credentials')._internal

            local default_identities = {{
                'user', 'admin',
            }, {
                'user', 'guest',
            }, {
                'role', 'super',
            }, {
                'role', 'public',
            }, {
                'role', 'replication',
            },}

            for _, id in ipairs(default_identities) do
                local user_or_role, name = unpack(id)

                local perm = box.schema[user_or_role].info(name)
                perm = internal.privileges_from_box(perm)

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
            local internal =
                    require('internal.config.applier.credentials')._internal

            local default_identities = {{
                'user', 'admin',
            }, {
                'user', 'guest',
            }, {
                'role', 'super',
            }, {
                'role', 'public',
            }, {
                'role', 'replication',
            },}

            for _, id in ipairs(default_identities) do
                local user_or_role, name = unpack(id)

                local perm = box.schema[user_or_role].info(name)
                perm = internal.privileges_from_box(perm)

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

            local perm = box.schema.user.info('guest')
            perm = internal.privileges_from_box(perm)

            t.assert_not_equals(perm['role']['dummy'], {execute = true})

            box.cfg{read_only = false}
            t.assert_not(box.info.ro)

            local retrying = require('luatest.helpers').retrying

            retrying(
                {timeout = 10, delay = 0.5},
                function()
                    local perm = box.schema.user.info('guest')
                    perm = internal.privileges_from_box(perm)

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

            local perm_1 = box.schema.user.info('myuser1')
            perm_1 = internal.privileges_from_box(perm_1)

            local perm_2 = box.schema.user.info('myuser2')
            perm_2 = internal.privileges_from_box(perm_2)

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

            local perm_1 = box.schema.user.info('myuser1')
            perm_1 = internal.privileges_from_box(perm_1)

            local perm_2 = box.schema.user.info('myuser2')
            perm_2 = internal.privileges_from_box(perm_2)

            local exp_perm_1 = iconfig.credentials.users.myuser1
            exp_perm_1 = internal.privileges_from_config(exp_perm_1)
            exp_perm_1 = internal.privileges_add_defaults('myuser1', 'user',
                                                          exp_perm_1)

            local exp_perm_2 = iconfig.credentials.users.myuser2
            exp_perm_2 = internal.privileges_from_config(exp_perm_2)
            exp_perm_2 = internal.privileges_add_defaults('myuser2', 'user',
                                                          exp_perm_2)

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

            local perm_1 = box.schema.user.info('myuser1')
            perm_1 = internal.privileges_from_box(perm_1)

            t.assert_equals(perm_1['space']['myspace1'], nil)

            box.space['myspace2']:rename('myspace1')

            local perm_2 = box.schema.user.info('myuser1')
            perm_2 = internal.privileges_from_box(perm_2)

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

            local perm_1 = box.schema.user.info('myuser')
            perm_1 = internal.privileges_from_box(perm_1)

            t.assert_equals(perm_1['space']['myspace1'],
                                {read = true})

            t.assert_equals(perm_1['space']['myspace2'], nil)

            box.space['myspace1']:rename('myspace2')

            local perm_2 = box.schema.user.info('myuser')
            perm_2 = internal.privileges_from_box(perm_2)

            t.assert_equals(perm_2['space']['myspace2'],
                                {read = true, write = true})

            t.assert_equals(perm_2['space']['myspace1'], nil)
        end,
        options_2 = iconfig,
        verify_2 = function(iconfig)
            box.schema.space.create('myspace1')

            local internal =
                    require('internal.config.applier.credentials')._internal

            local perm = box.schema.user.info('myuser')
            perm = internal.privileges_from_box(perm)

            local exp_perm = iconfig.credentials.users.myuser
            exp_perm = internal.privileges_from_config(exp_perm)
            exp_perm = internal.privileges_add_defaults('myuser', 'user',
                                                        exp_perm)
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

                local perm = box.schema.user.info('myuser')
                perm = internal.privileges_from_box(perm)

                local exp_perm = iconfig.credentials.users.myuser
                exp_perm = internal.privileges_from_config(exp_perm)
                exp_perm = internal.privileges_add_defaults('myuser', 'user',
                                                            exp_perm)
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
                listen = 'unix/:./test.iproto',
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
                listen = 'unix/:./test.iproto',
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
