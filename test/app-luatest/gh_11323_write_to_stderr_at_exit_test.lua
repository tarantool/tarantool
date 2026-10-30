local popen = require('popen')
local t = require('luatest')

local g = t.group()

g.test_write_to_stderr_at_exit = function()
    t.tarantool.skip_if_not_debug()
    local ph = popen.new({
        arg[-1], '-e', [[
            box.error.injection.set('ERRINJ_WRITE_EXIT_CODE_TO_STDERR', true)
            require('log').cfg()
            os.exit(42)
        ]]
    }, {
        stderr = popen.opts.PIPE,
    })
    local output = {}
    repeat
        local s = ph:read({stderr = true})
        table.insert(output, s)
    until s == ''
    output = table.concat(output)
    ph:close()
    t.assert_str_contains(output, 'Tarantool exited with code 42')
end
