local t = require('luatest')
local fio = require('fio')
local justrun = require('test.justrun').tarantool

local g = t.group()

local function simplerun(args)
    return justrun(fio.tempdir(), {}, args, {nojson = true, stderr = true})
end

g.test_tarantool_execute = function()
    local result = simplerun({'-e \'box.watch("box.ballot", function() end)\''})
    t.assert_equals(result.exit_code, 0, 'Execute Lua one-liner is fine')
    t.assert_equals(result.stdout, '', 'Lua one-liner output is fine')
    t.assert_equals(result.stderr, '', 'Lua one-liner spawns no errors')
end
