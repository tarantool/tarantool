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
        box.schema.user.drop('u', {if_exists = true})
        box.schema.role.drop('r', {if_exists = true})
        box.schema.role.drop('r2', {if_exists = true})
    end)
end)

-- Test that grant rollback does not keep unwanted artifacts.
g.test_fail_on_rebuild_effective_grants = function(cg)
    cg.server:exec(function()
        t.tarantool.skip_if_not_debug()

        -- Create a user and two roles.
        box.schema.user.create('u')
        box.schema.role.create('r')
        box.schema.role.create('r2')

        -- Make the first role able to create a space.
        box.session.su('admin', box.schema.role.grant,
                       'r', 'create,write', 'universe')

        -- Fail to grant the first role.
        box.error.injection.set('ERRINJ_INDEX_ITERATOR_NEW_ONCE', true)
        t.assert_error_msg_equals(
            "Error injection 'iterator fail once'",
            box.schema.user.grant, 'u', 'execute', 'role', 'r')

        -- Successfully grant the second one.
        box.schema.user.grant('u', 'execute', 'role', 'r2')

        -- Check the first role is not assigned.
        t.assert_equals(box.schema.user.info('u'), {
            {'execute', 'role', 'public'},
            {'execute', 'role', 'r2'},
            {'session,usage', 'universe', ''},
            {'alter', 'user', 'u'},
        })

        -- Check the user can't create a space as the first role would allow.
        t.assert_error_msg_equals(
            "Write access to space '_space' is denied for user 'u'",
            box.session.su, 'u', box.schema.space.create, 's')
    end)
end

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
