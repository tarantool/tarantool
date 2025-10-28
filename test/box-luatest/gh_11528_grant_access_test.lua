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

-- Test that the 'grant' access works as expected.
g.test_grant_access = function(cg)
    cg.server:exec(function()
        -- A shortcut to run as admin.
        local function sudo(...)
            return box.session.su('admin', ...)
        end

        -- A space, user, role, function and a sequence walk into a bar...
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
        local function check_visibility()
            -- No errors reading objects.
            t.assert(pcall(box.session.su, 'uu',
                           box.space.s.format, box.space.s))
            t.assert(pcall(box.session.su, 'uu', box.schema.user.info, 'u'))
            t.assert(pcall(box.session.su, 'uu', box.schema.role.info, 'r'))
            t.assert(pcall(box.session.su, 'uu', box.schema.func.exists, 'f'))
            t.assert_error_msg_equals('Sequence \'seq\' already exists',
                                      box.session.su, 'uu',
                                      box.schema.sequence.create, 'seq')
        end

        -- Check that the object can be granted.
        local function check_ok(access, object_type, object_name)
            t.assert(pcall(box.session.su, 'uu', box.schema.user.grant,
                           'uuu', access, object_type, object_name))
            t.assert(pcall(box.session.su, 'uu', box.schema.user.revoke,
                           'uuu', access, object_type, object_name))
        end

        -- Check that the object can not be granted.
        local function check_fail(accesses, object_type, object_name)
            -- Check each comma-separated access separately.
            for access in string.gmatch(accesses, "([^,]+)") do
                t.assert_not(pcall(box.session.su, 'uu', box.schema.user.grant,
                                   'uuu', access, object_type, object_name))
            end
        end

        -- Check a user with no access.
        check_fail('read,write,create,alter,drop', 'space', 's')
        check_fail('create,alter,drop', 'user', 'u')
        check_fail('create,drop,execute,usage', 'role', 'r')
        check_fail('create,drop,execute,usage', 'function', 'f')
        check_fail('read,write,usage,create,alter,drop', 'sequence', 'seq')
        check_fail('execute,usage', 'lua_call', 'lf')
        check_fail('read,write,create,alter,drop', 'space')
        check_fail('create,alter,drop', 'user')
        check_fail('create,drop,execute,usage', 'role')
        check_fail('create,drop,execute,usage', 'function')
        check_fail('read,write,usage,create,alter,drop', 'sequence')
        check_fail('execute,usage', 'lua_call')
        check_fail('execute,usage', 'lua_eval')
        check_fail('execute,usage', 'sql')
        check_fail('read,write,execute,session,usage,create,drop,' ..
                   'alter,trigger,insert,update,delete', 'universe')
        check_fail('grant,metagrant', 'universe')

        -- Check a user with "grant" access.
        sudo(box.schema.user.grant, 'uu', 'grant', 'universe')
        check_visibility()
        check_ok('read,write,create,alter,drop', 'space', 's')
        check_ok('create,alter,drop', 'user', 'u')
        check_ok('create,drop,execute,usage', 'role', 'r')
        check_ok('create,drop,execute,usage', 'function', 'f')
        check_ok('read,write,usage,create,alter,drop', 'sequence', 'seq')
        check_ok('execute,usage', 'lua_call', 'lf')
        check_ok('read,write,create,alter,drop', 'space')
        check_ok('create,alter,drop', 'user')
        check_ok('create,drop,execute,usage', 'role')
        check_ok('create,drop,execute,usage', 'function')
        check_ok('read,write,usage,create,alter,drop', 'sequence')
        check_ok('execute,usage', 'lua_call')
        check_ok('execute,usage', 'lua_eval')
        check_ok('execute,usage', 'sql')
        check_ok('read,write,execute,session,usage,create,drop,' ..
                 'alter,trigger,insert,update,delete', 'universe')
        check_fail('grant,metagrant', 'universe')
        sudo(box.schema.user.revoke, 'uu', 'grant', 'universe')

        -- Check a user with "metagrant" access.
        sudo(box.schema.user.grant, 'uu', 'metagrant', 'universe')
        check_fail('read,write,create,alter,drop', 'space', 's')
        check_fail('create,alter,drop', 'user', 'u')
        check_fail('create,drop,execute,usage', 'role', 'r')
        check_fail('create,drop,execute,usage', 'function', 'f')
        check_fail('read,write,usage,create,alter,drop', 'sequence', 'seq')
        check_fail('execute,usage', 'lua_call', 'lf')
        check_fail('read,write,create,alter,drop', 'space')
        check_fail('create,alter,drop', 'user')
        check_fail('create,drop,execute,usage', 'role')
        check_fail('create,drop,execute,usage', 'function')
        check_fail('read,write,usage,create,alter,drop', 'sequence')
        check_fail('execute,usage', 'lua_call')
        check_fail('execute,usage', 'lua_eval')
        check_fail('execute,usage', 'sql')
        check_fail('read,write,execute,session,usage,create,drop,' ..
                   'alter,trigger,insert,update,delete', 'universe')
        check_ok('grant,metagrant', 'universe')
        sudo(box.schema.user.revoke, 'uu', 'metagrant', 'universe')

        -- Check a user with "grant" and "metagrant" accesses.
        sudo(box.schema.user.grant, 'uu', 'grant,metagrant', 'universe')
        check_visibility()
        check_ok('read,write,create,alter,drop', 'space', 's')
        check_ok('create,alter,drop', 'user', 'u')
        check_ok('create,drop,execute,usage', 'role', 'r')
        check_ok('create,drop,execute,usage', 'function', 'f')
        check_ok('read,write,usage,create,alter,drop', 'sequence', 'seq')
        check_ok('execute,usage', 'lua_call', 'lf')
        check_ok('read,write,create,alter,drop', 'space')
        check_ok('create,alter,drop', 'user')
        check_ok('create,drop,execute,usage', 'role')
        check_ok('create,drop,execute,usage', 'function')
        check_ok('read,write,usage,create,alter,drop', 'sequence')
        check_ok('execute,usage', 'lua_call')
        check_ok('execute,usage', 'lua_eval')
        check_ok('execute,usage', 'sql')
        check_ok('read,write,execute,session,usage,create,drop,' ..
                 'alter,trigger,insert,update,delete', 'universe')
        check_ok('grant,metagrant', 'universe')
        sudo(box.schema.user.revoke, 'uu', 'grant,metagrant', 'universe')
    end)
end

-- Test the "grant" privilege grantability.
g.test_grant_oneself = function(cg)
    cg.server:exec(function()
        -- Create a user able to the DDLs required.
        box.session.su('admin', box.schema.user.create, 'u')
        box.session.su('admin', box.schema.user.create, 'uu')
        box.session.su('admin', box.schema.user.grant,
                       'u', 'write', 'space', '_priv')

        -- Can't grant a privilege oneself with the "grant" access.
        local cant_grant_oneself = 'Incorrect grant arguments: only object' ..
                                   ' owner or admin can grant oneself'
        box.session.su('admin', box.schema.user.grant,
                       'u', 'grant', 'universe')
        t.assert_error_msg_equals(cant_grant_oneself, box.session.su, 'u',
                                  box.schema.user.grant, 'u',
                                  'execute', 'lua_call')

        -- Can't revoke the privilege if it's granted by someone else either.
        box.session.su('admin', box.schema.user.grant,
                       'u', 'execute', 'lua_call')
        t.assert_error_msg_equals(cant_grant_oneself, box.session.su, 'u',
                                  box.schema.user.revoke, 'u',
                                  'execute', 'lua_call')

        -- The "metagrant" access required to grant "grant" or "metagrant".
        local need_metagrant = "Metagrant access to universe" ..
                               " '' is denied for user 'u'"
        t.assert_error_msg_equals(need_metagrant, box.session.su, 'u',
                                  box.schema.user.grant, 'uu',
                                  'grant', 'universe')
        t.assert_error_msg_equals(need_metagrant, box.session.su, 'u',
                                  box.schema.user.grant, 'uu',
                                  'metagrant', 'universe')

        -- Check it with no "grant" privilege (no view visibility).
        box.session.su('admin', box.schema.user.revoke,
                       'u', 'grant', 'universe')
        t.assert_error_msg_equals("User 'uu' is not found", box.session.su, 'u',
                                  box.schema.user.grant, 'uu',
                                  'grant', 'universe')
        t.assert_error_msg_equals("User 'uu' is not found", box.session.su, 'u',
                                  box.schema.user.grant, 'uu',
                                  'metagrant', 'universe')

        -- Check with the accesses required to get _vuser space data.
        box.session.su('admin', box.schema.user.grant, 'u', 'alter', 'user')
        t.assert_error_msg_equals(need_metagrant, box.session.su, 'u',
                                  box.schema.user.grant, 'uu',
                                  'grant', 'universe')
        t.assert_error_msg_equals(need_metagrant, box.session.su, 'u',
                                  box.schema.user.grant, 'uu',
                                  'metagrant', 'universe')

        -- Succeed by having the "metagrant" privilege.
        box.session.su('admin', box.schema.user.grant,
                       'u', 'metagrant', 'universe')
        box.session.su('u', box.schema.user.grant, 'uu',
                       'grant,metagrant', 'universe')
        box.session.su('u', box.schema.user.revoke, 'uu',
                       'grant,metagrant', 'universe')

        -- The "grant" privilege does not affect the result.
        box.session.su('admin', box.schema.user.grant,
                       'u', 'grant', 'universe')
        box.session.su('u', box.schema.user.grant, 'uu',
                       'grant,metagrant', 'universe')
        box.session.su('u', box.schema.user.revoke, 'uu',
                       'grant,metagrant', 'universe')
    end)
end
