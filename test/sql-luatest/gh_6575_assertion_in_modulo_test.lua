local server = require('test.luatest_helpers.server')
local t = require('luatest')
local g = t.group()

g.before_all(function()
    g.server = server:new({alias = 'test_assertion_in_modulo'})
    g.server:start()
end)

g.after_all(function()
    g.server:stop()
end)

g.test_assertion_in_modulo = function()
    g.server:exec(function()
        local t = require('luatest')
        t.assert_equals(box.execute([[SELECT -5 % -1;]]).rows, {{0}})
        t.assert_equals(box.execute([[SELECT -5 % 1;]]).rows, {{0}})
    end)
end
