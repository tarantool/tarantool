local t = require('luatest')
local tarantool = require('tarantool')

local g = t.group()

g.test_build_asan = function()
    local b = tarantool.build
    local asan = b.flags:match('-fsanitize=[%a,]*address') ~= nil
    t.assert_equals(b.asan, asan)
end
