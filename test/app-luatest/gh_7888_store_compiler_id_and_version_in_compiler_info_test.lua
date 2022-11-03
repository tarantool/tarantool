local t = require('luatest')
local tarantool = require('tarantool')

local g = t.group()

g.test_compiler_id_and_version_in_compiler_info = function(_)
    t.assert_str_matches(tarantool.build.compiler, '^%a+-%d+%..+')
    local _, _, id = tarantool.build.compiler:find('^(%a+)')
    t.assert(id == 'Clang' or id == 'GNU')
end
