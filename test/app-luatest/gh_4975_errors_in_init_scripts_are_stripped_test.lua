local t = require('luatest')
local justrun = require('test.justrun')

local g = t.group('gh-4975')

--[[
Require a non-existent module with a long enough name to get an error message,
which is longer than 1000 characters, like:

LuajitError: module 'aaa<...>aaa' not found:
    no file '/usr/local/lib/lua/5.1/aaa<...>aaa.so'
    no file '/usr/local/lib/lua/5.1/override/aaa<...>aaa.so'
    <...>
]]
g.test_long_init_error = function()
    local module_name = string.rep('a', 500)
    local args = {'-l', module_name, '-e', '""'}
    local res = justrun.tarantool('.', {}, args, {stderr = true, nojson = true})
    local ref = ("LuajitError: module '%s' not found:.*"):format(module_name)
    t.assert_str_matches(res.stderr, ref)
    t.assert_gt(string.len(res.stderr), 1000, 'error is longer than 1000 chars')
end
