local t = require('luatest')

local g = t.group('gh-8022')

-- Check that `tarantool.build.flags` contains an optimization level setting.
-- It means that config-specific flags are included into the variable.
g.test_build_target = function()
    local tarantool = require('tarantool')
    t.assert_str_contains(tarantool.build.flags, '-O')
end
