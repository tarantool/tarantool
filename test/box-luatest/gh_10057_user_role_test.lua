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
        box.schema.user.drop('testuser', {if_exists = true})
        box.schema.role.drop('director', {if_exists = true})
        box.schema.role.drop('manager', {if_exists = true})
        box.schema.role.drop('management', {if_exists = true})
        box.schema.role.drop('infra', {if_exists = true})
        box.schema.role.drop('staff', {if_exists = true})
    end)
end)

g.test_user_role = function(cg)
    cg.server:exec(function()
        box.schema.user.create('testuser')
        box.schema.role.create('manager')
        box.schema.role.create('director')
        box.schema.user.grant('testuser', 'manager')

        local original_user = box.session.effective_user()
        box.session.su('testuser')

        -- Check we don't have a Lua error on box.schema.info().
        local ok = pcall(box.schema.user.info)
        t.assert(ok)

        -- Check the user has an access to the 'manager' role.
        t.assert_equals(#box.space._vuser.index.name:select('manager'), 1)

        -- Check the user doesn't have access to the 'director' role.
        t.assert_equals(#box.space._vuser.index.name:select('director'), 0)

        box.session.su(original_user)
    end)
end

g.test_transitive_role = function(cg)
    cg.server:exec(function()
        -- Level 1 role.
        box.schema.role.create('staff')

        -- Level 2 roles.
        box.schema.role.create('infra')
        box.schema.role.create('management')
        box.schema.role.grant('infra', 'staff')
        box.schema.role.grant('management', 'staff')

        -- Level 3 roles.
        box.schema.role.create('manager')
        box.schema.role.create('director')
        box.schema.role.grant('manager', 'management')
        box.schema.role.grant('director', 'management')

        -- The test subject.
        box.schema.user.create('testuser')
        box.schema.user.grant('testuser', 'manager')

        local original_user = box.session.effective_user()
        box.session.su('testuser')

        -- Check the user has an access to the 'manager' role.
        t.assert_equals(#box.space._vuser.index.name:select('manager'), 1)

        -- Check the user doesn't have access to the 'director' role.
        t.assert_equals(#box.space._vuser.index.name:select('director'), 0)

        -- Check the user has an access to the 'management' role.
        t.assert_equals(#box.space._vuser.index.name:select('management'), 1)

        -- Check the user doesn't have access to the 'infra' role.
        t.assert_equals(#box.space._vuser.index.name:select('infra'), 0)

        -- Check the user has an access to the 'staff' role.
        t.assert_equals(#box.space._vuser.index.name:select('staff'), 1)

        box.session.su(original_user)
    end)
end
