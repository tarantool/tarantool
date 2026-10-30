local t = require('luatest')

local g = t.group('gh-7166')

-- Check that wake up of the shutdown fiber doesn't crash Tarantool
g.test_wakeup_on_shutdown_fiber = function()
    local f = t.tarantool.find_fiber_by_name('on_shutdown')
    f:wakeup()
end
