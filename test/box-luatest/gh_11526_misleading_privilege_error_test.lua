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

g.test_privilege_revoke_from_admin = function(cg)
    cg.server:exec(function()
        local errmsg = "can't revoke universe from the admin user"
        t.assert_error_msg_contains(errmsg, function()
            box.schema.user.revoke('admin', 'read', 'universe')
        end)
        local ok, _ = pcall(function()
            box.schema.role.revoke('super', 'read', 'universe')
        end)
        t.assert(ok)
    end)
end
