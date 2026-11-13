local t = require('luatest')
local treegen = require('luatest.treegen')
local justrun = require('luatest.justrun')

local g_boot = t.group('boot', t.helpers.matrix({
    length = {0, 5, 16381, 16382, 16383, 16384, 32768},
}))

-- gh-13406: Truncated boot log entries must retain their trailing newline.
g_boot.test_truncation = function(cg)
    local dir = treegen.prepare_directory({}, {})
    treegen.write_file(dir, 'main.lua', [[
        local log = require('log')
        local message = string.rep('x', tonumber(arg[1]))
        log.info(message)
        log.info('next message')
    ]])
    local args = {'main.lua', tostring(cg.params.length)}
    local res = justrun.tarantool(dir, {}, args,
                                 {stderr = true, nojson = true})
    t.assert_equals(res.exit_code, 0, res.stderr)
    local lines = res.stderr:split('\n')
    t.assert_equals(#lines, 2)
    local expected = string.rep('x', cg.params.length)
    t.assert_equals(lines[1], expected:sub(1, 16382))
    t.assert_equals(lines[2], 'next message')
end

local g_plain = t.group('plain')

-- gh-13406: Truncated plain log entries must retain their trailing newline.
g_plain.test_truncation = function()
    local dir = treegen.prepare_directory({}, {})
    treegen.write_file(dir, 'main.lua', [[
        local log = require('log')
        log.cfg({format = 'plain'})
        log.info(string.rep('x', 32000))
        log.info(string.rep('x', 32000))
        log.info('next message')
    ]])
    local res = justrun.tarantool(dir, {}, {'main.lua'},
                                 {stderr = true, nojson = true})
    t.assert_equals(res.exit_code, 0, res.stderr)
    local lines = res.stderr:split('\n')
    t.assert_equals(#lines, 3)
    for i = 1, 2 do
        t.assert_equals(#lines[i], 16382)
        t.assert_str_matches(lines[i], '.* I> x+')
    end
    t.assert_str_matches(lines[3], '.* I> next message')
end
