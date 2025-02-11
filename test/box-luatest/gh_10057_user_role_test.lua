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

g.test_origins_in_privs = function(cg)
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
