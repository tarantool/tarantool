local t = require('luatest')
local server = require('luatest.server')

local g = t.group()

g.before_all(function(cg)
    cg.server = server:new({alias = 'master'})
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.test_gh_8196 = function(cg)
    cg.server:exec(function()
        t.assert_error_msg_equals("Space '1' does not exist",
                                  box.internal.compact, 1, 1)
    end)
end
