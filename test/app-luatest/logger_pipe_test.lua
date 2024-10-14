local t = require('luatest')
local treegen = require('luatest.treegen')
local justrun = require('luatest.justrun')

local g = t.group()

g.test_option = function()
    local dir = treegen.prepare_directory({}, {})
    treegen.write_file(dir, 'main.lua', [[
        box.cfg { log = '|echo $TEST_VAR; cat > /dev/null' }
        os.exit(0)
    ]])
    local magic_number = 48
    local res = justrun.tarantool(dir,
        { TEST_VAR = tostring(magic_number) },
        { 'main.lua' }
    )
    t.assert_equals(res.exit_code, 0)
    t.assert_equals(res.stdout[1], magic_number)
end
