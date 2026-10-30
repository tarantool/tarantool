local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function(cg)
    cg.server = server:new({alias = 'master'})
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:stop()
end)

g.test__truncate_insert_as_guest = function(cg)
    cg.server:exec(function()
        box.schema.space.create('test')

        -- Create new user instead of 'guest' because test infrastructure
        -- grants 'super' role to 'guest' with all the privileges. New
        -- user has 'public' role by default.
        box.schema.user.create('test')
        box.session.su('test')

        local errmsg = "Write access to space 'test' is denied for user 'test'"
        t.assert_error_msg_equals(errmsg,
                                  box.space._truncate.insert,
                                  box.space._truncate,
                                  {box.space.test.id, 1})

        box.session.su('admin')
        t.assert_equals(#box.space._truncate:select{}, 0)
    end)
end
