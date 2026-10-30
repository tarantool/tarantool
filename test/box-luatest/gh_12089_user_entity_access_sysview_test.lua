local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_each(function(cg)
    cg.server = server:new({alias = 'master'})
    cg.server:start()
end)

g.after_each(function(cg)
    cg.server:drop()
end)

g.test_entity_access_vuser = function(cg)
    cg.server:exec(function()
        box.schema.user.create('password_updater')
        box.schema.user.create('to_update_password_of')
        box.schema.user.grant('password_updater', 'write', 'space', '_user')
        box.schema.user.grant('password_updater', 'alter', 'user')
        -- Finding the user to change the password of in _vuser must succeed
        -- cause the password_updater has 'alter' access on the 'user' entity.
        box.session.su('password_updater', box.schema.user.passwd,
                       'to_update_password_of', '1234')
    end)
end
