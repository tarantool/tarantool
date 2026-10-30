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

g.after_test('test_space_execute_priv', function(cg)
    cg.server:exec(function()
        local compat = require('compat')
        box.schema.user.drop('test', {if_exists = true})
        compat.box_space_execute_priv = 'default'
    end)
end)

g.test_space_execute_priv = function(cg)
    cg.server:exec(function()
        local compat = require('compat')
        t.assert_equals(compat.box_space_execute_priv.current, 'default')
        t.assert_equals(compat.box_space_execute_priv.default, 'new')
        box.schema.user.create('test')
        t.assert_error_msg_equals(
            "Unsupported space privilege 'execute'",
            box.session.su, 'admin',
            box.schema.user.grant, 'test', 'execute', 'space')
        compat.box_space_execute_priv = 'old'
        box.session.su('admin', box.schema.user.grant,
                       'test', 'execute', 'space')
    end)
end
