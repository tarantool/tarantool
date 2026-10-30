local t = require('luatest')
local treegen = require('luatest.treegen')
local justrun = require('luatest.justrun')

local g = t.group()

g.test_log_tables_in_json = function()
    local dir = treegen.prepare_directory({}, {})
    treegen.write_file(dir, 'main.lua', [[
        local log = require('log')
        log.info('%s, %s, %s, %s', 123, {foo = 'bar'}, 'abc', {1, 2, 3})
        log.info('%s, %s, %s, %s, %s', nil, nil, nil, nil, {1, 2, 3})
        log.info('%s', setmetatable({foo = 'bar'}, {
            __index = {fuzz = 'buzz'},
        }))
        log.info('%s', setmetatable({foo = 'bar'}, {
            __tostring = function() return 'foobar' end,
        }))
    ]])
    local res = justrun.tarantool(dir, {}, {'main.lua'},
                                  {stderr = true, nojson = true})
    t.assert_equals(res.exit_code, 0)
    t.assert_equals(res.stderr,
[[123, {"foo":"bar"}, abc, [1,2,3]
nil, nil, nil, nil, [1,2,3]
{"foo":"bar"}
foobar]])
end
