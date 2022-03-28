local t = require('luatest')
local g = t.group()

g.test_multiple_args = function()
    local handle = io.popen('tarantool -ea=10 -e b=20 -ec=30 -e d=40 -e"print(a+b+c+d)"')
    local result = handle:read()
    t.assert_equals(result, '100')
end
