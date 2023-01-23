local t = require('luatest')

local g = t.group('gh-7495')

-- Check that `tarantool.build.target` contains the same CPU architecture as
-- reported by `uname -m`.
g.test_build_target = function()
    local tarantool = require('tarantool')
    local arch_name = io.popen('uname -m'):read()
    t.assert_str_contains(tarantool.build.target, '-' .. arch_name .. '-')
end
