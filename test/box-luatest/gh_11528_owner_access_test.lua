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
