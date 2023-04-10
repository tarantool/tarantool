local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function()
    g.server = server:new({alias = 'gh-6575'})
    g.server:start()
end)

g.after_all(function()
    g.server:stop()
end)

g.test_assertion_in_modulo = function()
    g.server:exec(function()
        t.assert_equals(box.execute([[SELECT -5 % -1;]]).rows, {{0}})
        t.assert_equals(box.execute([[SELECT -5 % 1;]]).rows, {{0}})
    end)
end
