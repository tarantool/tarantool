local server = require('luatest.server')
local t = require('luatest')
local g = t.group()

g.before_all(function(cg)
    cg.server = server:new()
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.after_each(function(cg)
    cg.server:exec(function()
        if box.space.s ~= nil then
            box.space.s:drop()
        end
        box.schema.user.drop('uuu', {if_exists = true})
        box.schema.user.drop('uu', {if_exists = true})
        box.schema.user.drop('u', {if_exists = true})
        box.schema.role.drop('r', {if_exists = true})
        box.schema.func.drop('f', {if_exists = true})
        if box.sequence.seq ~= nil then
            box.sequence.seq:drop()
        end
    end)
end)

-- Test that the dropped grants don't remain on the role.
g.test_dropped_grants = function(cg)
    cg.server:exec(function()
        -- Create a user and make it able to do DDL.
        box.schema.user.create('u')
        box.session.su('admin', box.schema.user.grant,
                       'u', 'read,write', 'universe')

        -- Create a role.
        box.schema.role.create('r')

        -- Allow and disallow the user to drop the role.
        box.schema.user.grant('u', 'drop', 'role', 'r')
        box.schema.user.revoke('u', 'drop', 'role', 'r')

        -- Drop the role by the user (this should be disallowed).
        t.assert_error_msg_equals(
            "Drop access to role 'r' is denied for user 'u'",
            box.session.su, 'u', box.schema.role.drop, 'r')

        t.assert(box.schema.role.exists('r'))
    end)
end

-- Test that it's possible to have both 'execute' and 'grant' access on a role.
g.test_multiple_role_accesses = function(cg)
    cg.server:exec(function()
        -- Create a space and a role that can alter the space and execute DDLs.
        box.session.su('admin', box.schema.space.create, 's')
        box.session.su('admin', box.schema.role.create, 'r')
        box.session.su('admin', box.schema.role.grant,
                       'r', 'alter', 'space', 's')
        box.session.su('admin', box.schema.role.grant,
                       'r', 'read,write,create', 'universe')

        -- Create a user and grant accesses to the role.
        box.schema.user.create('u')
        box.schema.user.grant('u', 'execute,grant', 'role', 'r')

        -- Check the user can alter the space (as the role allows).
        box.session.su('u', box.space.s.alter, box.space.s, {})

        -- Check the user can grant the role (as the privilege allows).
        box.session.su('u', box.schema.user.create, 'uu')
        box.session.su('u', box.schema.user.grant, 'uu', 'r')
    end)
end

-- Test that the 'grant' access on a space works as expected.
g.test_grant_access = function(cg)
    cg.server:exec(function()
        -- A shortcut to run as admin.
        local function sudo(...)
            return box.session.su('admin', ...)
        end

        -- A space, user, role, function and sequence walk into a bar...
        sudo(box.schema.space.create, 's')
        sudo(box.schema.user.create, 'u')
        sudo(box.schema.role.create, 'r')
        sudo(box.schema.func.create, 'f', {
            body = 'function(tuple) return tuple[1] end',
            is_deterministic = true, is_sandboxed = true
        })
        sudo(box.schema.sequence.create, 'seq')
        rawset(_G, 'lf', function() return true end)

        -- Create the test users and make someone able to DDLs required.
        sudo(box.schema.user.create, 'uu')
        sudo(box.schema.user.create, 'uuu')
        sudo(box.schema.user.grant, 'uu', 'read', 'space', '_user')
        sudo(box.schema.user.grant, 'uu', 'write', 'space', '_priv')

        -- Check that the objects are visible.
        local function check_visibility(expected_privs)
            -- No errors reading objects.
            t.assert(pcall(box.session.su, 'uu',
                           box.space.s.format, box.space.s))
            t.assert(pcall(box.session.su, 'uu', box.schema.user.info, 'u'))
            t.assert(pcall(box.session.su, 'uu', box.schema.role.info, 'r'))
            t.assert(pcall(box.session.su, 'uu', box.schema.func.exists, 'f'))
            t.assert_error_msg_equals('Sequence \'seq\' already exists',
                                      box.session.su, 'uu',
                                      box.schema.sequence.create, 'seq')

            -- No errors getting object names.
            t.assert_equals(box.session.su('uu', box.schema.user.info, 'uu'),
                            expected_privs)
        end

        -- Check that the object can be granted.
        local function check_ok(access, object_type, object_name)
            t.assert(pcall(box.session.su, 'uu', box.schema.user.grant,
                           'uuu', access, object_type, object_name))
            t.assert(pcall(box.session.su, 'uu', box.schema.user.revoke,
                           'uuu', access, object_type, object_name))
        end

        -- Check that the object can not be granted.
        local function check_fail(access, object_type, object_name)
            t.assert_not(pcall(box.session.su, 'uu', box.schema.user.grant,
                               'uuu', access, object_type, object_name))
        end

        -- Check object access.
        sudo(box.schema.user.grant, 'uu', 'grant', 'space', 's')
        sudo(box.schema.user.grant, 'uu', 'grant', 'user', 'u')
        sudo(box.schema.user.grant, 'uu', 'grant', 'role', 'r')
        sudo(box.schema.user.grant, 'uu', 'grant', 'function', 'f')
        sudo(box.schema.user.grant, 'uu', 'grant', 'sequence', 'seq')
        sudo(box.schema.user.grant, 'uu', 'grant', 'lua_call', 'lf')
        check_visibility({{'grant', 'function', 'f'},
                          {'grant', 'lua_call', 'lf'},
                          {'execute', 'role', 'public'},
                          {'grant', 'role', 'r'},
                          {'grant', 'sequence', 'seq'},
                          {'read', 'space', '_user'},
                          {'write', 'space', '_priv'},
                          {'grant', 'space', 's'},
                          {'session,usage', 'universe', ''},
                          {'grant', 'user', 'u'},
                          {'alter', 'user', 'uu'}})
        check_ok('read,write,create,alter,drop,grant', 'space', 's')
        check_ok('create,alter,drop,grant', 'user', 'u')
        check_ok('create,drop,execute,usage,grant', 'role', 'r')
        check_ok('create,drop,execute,usage,grant', 'function', 'f')
        check_ok('read,write,usage,create,alter,drop,grant', 'sequence', 'seq')
        check_ok('execute,usage,grant', 'lua_call', 'lf')
        check_fail('read,write,create,alter,drop,grant', 'space')
        check_fail('create,alter,drop,grant', 'user')
        check_fail('create,drop,execute,usage,grant', 'role')
        check_fail('create,drop,execute,usage,grant', 'function')
        check_fail('read,write,usage,create,alter,drop,grant', 'sequence')
        check_fail('execute,usage,grant', 'lua_call')
        check_fail('execute,usage,grant', 'lua_eval')
        check_fail('execute,usage,grant', 'sql')
        check_fail('read,write,execute,session,usage,create,drop,alter,' ..
                   'trigger,insert,update,delete,grant', 'universe')
        sudo(box.schema.user.revoke, 'uu', 'grant', 'space', 's')
        sudo(box.schema.user.revoke, 'uu', 'grant', 'user', 'u')
        sudo(box.schema.user.revoke, 'uu', 'grant', 'role', 'r')
        sudo(box.schema.user.revoke, 'uu', 'grant', 'function', 'f')
        sudo(box.schema.user.revoke, 'uu', 'grant', 'sequence', 'seq')
        sudo(box.schema.user.revoke, 'uu', 'grant', 'lua_call', 'lf')

        -- Check entity access.
        sudo(box.schema.user.grant, 'uu', 'grant', 'space')
        sudo(box.schema.user.grant, 'uu', 'grant', 'user')
        sudo(box.schema.user.grant, 'uu', 'grant', 'role')
        sudo(box.schema.user.grant, 'uu', 'grant', 'function')
        sudo(box.schema.user.grant, 'uu', 'grant', 'sequence')
        sudo(box.schema.user.grant, 'uu', 'grant', 'lua_call')
        sudo(box.schema.user.grant, 'uu', 'grant', 'lua_eval')
        sudo(box.schema.user.grant, 'uu', 'grant', 'sql')
        check_visibility({{'grant', 'function', ''},
                          {'grant', 'lua_call', ''},
                          {'grant', 'lua_eval', ''},
                          {'execute', 'role', 'public'},
                          {'grant', 'role', ''},
                          {'grant', 'sequence', ''},
                          {'read', 'space', '_user'},
                          {'write', 'space', '_priv'},
                          {'grant', 'space', ''},
                          {'grant', 'sql', ''},
                          {'session,usage', 'universe', ''},
                          {'alter', 'user', 'uu'},
                          {'grant', 'user', ''}})
        check_ok('read,write,create,alter,drop,grant', 'space', 's')
        check_ok('create,alter,drop,grant', 'user', 'u')
        check_ok('create,drop,execute,usage,grant', 'role', 'r')
        check_ok('create,drop,execute,usage,grant', 'function', 'f')
        check_ok('read,write,usage,create,alter,drop,grant', 'sequence', 'seq')
        check_ok('execute,usage,grant', 'lua_call', 'lf')
        check_ok('read,write,create,alter,drop,grant', 'space')
        check_ok('create,alter,drop,grant', 'user')
        check_ok('create,drop,execute,usage,grant', 'role')
        check_ok('create,drop,execute,usage,grant', 'function')
        check_ok('read,write,usage,create,alter,drop,grant', 'sequence')
        check_ok('execute,usage,grant', 'lua_call')
        check_ok('execute,usage,grant', 'lua_eval')
        check_ok('execute,usage,grant', 'sql')
        check_fail('read,write,execute,session,usage,create,drop,alter,' ..
                   'trigger,insert,update,delete,grant', 'universe')
        sudo(box.schema.user.revoke, 'uu', 'grant', 'space')
        sudo(box.schema.user.revoke, 'uu', 'grant', 'user')
        sudo(box.schema.user.revoke, 'uu', 'grant', 'role')
        sudo(box.schema.user.revoke, 'uu', 'grant', 'function')
        sudo(box.schema.user.revoke, 'uu', 'grant', 'sequence')
        sudo(box.schema.user.revoke, 'uu', 'grant', 'lua_call')
        sudo(box.schema.user.revoke, 'uu', 'grant', 'lua_eval')
        sudo(box.schema.user.revoke, 'uu', 'grant', 'sql')

        -- Check universal access.
        sudo(box.schema.user.grant, 'uu', 'grant', 'universe')
        check_visibility({{'execute', 'role', 'public'},
                          {'read', 'space', '_user'},
                          {'write', 'space', '_priv'},
                          {'session,usage,grant', 'universe', ''},
                          {'alter', 'user', 'uu'}})
        check_ok('read,write,create,alter,drop,grant', 'space', 's')
        check_ok('create,alter,drop,grant', 'user', 'u')
        check_ok('create,drop,execute,usage,grant', 'role', 'r')
        check_ok('create,drop,execute,usage,grant', 'function', 'f')
        check_ok('read,write,usage,create,alter,drop,grant', 'sequence', 'seq')
        check_ok('execute,usage,grant', 'lua_call', 'lf')
        check_ok('read,write,create,alter,drop,grant', 'space')
        check_ok('create,alter,drop,grant', 'user')
        check_ok('create,drop,execute,usage,grant', 'role')
        check_ok('create,drop,execute,usage,grant', 'function')
        check_ok('read,write,usage,create,alter,drop,grant', 'sequence')
        check_ok('execute,usage,grant', 'lua_call')
        check_ok('execute,usage,grant', 'lua_eval')
        check_ok('execute,usage,grant', 'sql')
        check_ok('read,write,execute,session,usage,create,drop,alter,' ..
                 'trigger,insert,update,delete,grant', 'universe')
        sudo(box.schema.user.revoke, 'uu', 'grant', 'universe')
    end)
end

-- Test a privilege can't be granted to oneself.
g.test_grant_oneself = function(cg)
    cg.server:exec(function()
        -- Create a user able to the DDLs required.
        box.session.su('admin', box.schema.user.create, 'u')
        box.session.su('admin', box.schema.user.grant,
                       'u', 'write', 'space', '_priv')
        box.session.su('admin', box.schema.user.grant,
                       'u', 'grant', 'universe')

        -- Make it attempt to grant a privilege oneself.
        t.assert_error_msg_equals(
            'Granting a privilege to oneself is not allowed',
            box.session.su, 'u', box.schema.user.grant,
            'u', 'execute', 'lua_call')
        box.session.su('admin', box.schema.user.grant,
                       'u', 'execute', 'lua_call')

        -- Make it revoke the privilege oneself.
        box.session.su('u', box.schema.user.revoke,
                       'u', 'execute', 'lua_call')
    end)
end
