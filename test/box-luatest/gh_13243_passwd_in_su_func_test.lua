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

g.test_passwd_in_su_func = function(cg)
    cg.server:exec(function()
        box.schema.user.create('test', {password = 'x'})
        local auth = box.space._user.index.name:get('test')[5]
        box.session.su('test', box.schema.user.passwd, 'y')
        t.assert_not_equals(box.space._user.index.name:get('test')[5], auth)
        t.assert_equals(box.space._user.index.name:get('admin')[5], {})
        box.schema.user.drop('test')
    end)
end
