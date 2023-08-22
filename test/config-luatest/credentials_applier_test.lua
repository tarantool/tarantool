local json = require('json')
local it = require('test.interactive_tarantool')
local t = require('luatest')
local treegen = require('test.treegen')

local g = t.group()

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
        },
    }

    -- Guest privileges in format provided by config schema
    local config_guest_data = {
        privileges = {{
                permissions = {
                    'session',
                    'usage'
                },
                universe = true,
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

    local config_privileges = {
        privileges = {{
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
            }
        },
        roles = {
            'public'
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
    child:roundtrip(("config_privileges = json.decode(%q)"):format(
                     json.encode(config_privileges)))
    child:roundtrip(("sync_privileges(%q, config_privileges, 'user')")
                    :format(name))

    child:execute_command(("box.schema.user.info('%s')"):format(name))
    local result_privileges = child:read_response()

    result_privileges = internal.privileges_from_box(result_privileges)
    config_privileges = internal.privileges_from_config(config_privileges)

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
