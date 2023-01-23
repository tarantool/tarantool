local fiber = require('fiber')
local t = require('luatest')

local g = t.group()

g.test_backtrace_option_for_fiber_obj = function()
    t.assert_equals(fiber.self():info({backtrace = false}).backtrace, nil)
    t.assert_equals(fiber.self():info({bt = false}).backtrace, nil)
end
