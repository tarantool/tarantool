local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function(cg)
    cg.server = server:new({alias = 'master'})
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.test_box_cfg = function(cg)
    cg.server:exec(function()
        local t = require('luatest')
        t.assert_equals(box.cfg.auth_type, 'chap-sha1')
        t.assert_error_msg_equals(
            "Incorrect value for option 'auth_type': should be of type string",
            box.cfg, {auth_type = 42})
        t.assert_error_msg_equals(
            "Incorrect value for option 'auth_type': chap-sha128",
            box.cfg, {auth_type = 'chap-sha128'})
    end)
end
