local server = require('test.luatest_helpers.server')
local t = require('luatest')
local g = t.group()

g.before_all = function()
    g.server = server:new{
        alias = 'default',
    }
    g.server:start()
end

g.after_all = function()
    g.server:drop()
end

g.test_result_returns_a_copy_of_the_internal_error = function()
    g.server:exec(function()
        local net = require('net.box')
        local t = require('luatest')
        box.cfg{listen = 3301}
        local c = net.connect(3301)
        local f = c:call('foo', {}, {is_async = true})
        local _, e1 = f:wait_result()
        t.assert_equals(e1.prev, nil)
        e1:set_prev(box.error.new(box.error.UNKNOWN))
        local _, e2 = f:wait_result()
        t.assert_not_equals(e1, e2)
        t.assert_not_equals(e2.prev, e1.prev)
        t.assert_equals(e2.prev, nil)
    end)
end
