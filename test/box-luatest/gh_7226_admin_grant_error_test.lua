local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function()
    g.server = server:new{alias = 'default'}
    g.server:start()
end)

g.after_all(function()
    g.server:drop()
end)

g.test_grainting_to_admin = function()
    g.server:exec(function()
        local function grant()
            box.schema.user.grant('admin', 'read', 'universe', nil, nil)
        end
        local msg = "User 'admin' already has read access on universe"
        t.assert_error_msg_content_equals(msg, grant)
    end)
end
