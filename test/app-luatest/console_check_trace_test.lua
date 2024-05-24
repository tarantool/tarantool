local console = require('console')
local yaml = require('yaml')
local tarantool = require('tarantool')

local t = require('luatest')

local g = t.group()

g.test_error_check_trace = function()
    t.skip_if(not tarantool.build.test_build, 'requires test build')
    local err = yaml.decode(console.eval([[
        require('tarantool')._internal.raise_incorrect_trace()
    ]]))
    t.assert_type(err, 'table')
    t.assert_type(err[1], 'table')
    t.assert_type(err[1].error, 'table')
    err = err[1].error
    t.assert_type(err[3], 'table')
    t.assert_type(err[3].line, 'number')
    err[3].line = nil
    t.assert_equals(err, {
        'Unknown error',
        'Warning, unexpected trace',
        {file = 'builtin/tarantool.lua'},
    })

    local err = yaml.decode(console.eval([[
        require('tarantool')._internal.raise_non_box_error()
    ]]))
    t.assert_type(err, 'table')
    t.assert_type(err[1], 'table')
    t.assert_type(err[1].error, 'table')
    err = err[1].error
    t.assert_str_matches(err[1], 'builtin/tarantool.lua:%d+: foo bar')
    t.assert_equals(err[2], 'Warning, box error expected')
end
