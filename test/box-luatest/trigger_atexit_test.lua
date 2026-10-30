local t = require('luatest')

local justrun = require('luatest.justrun')
local treegen = require('luatest.treegen')

local g = t.group()

g.test_gh_583_trigger_atexit = function()
    local dir = treegen.prepare_directory({}, {})
    local script_name = 'test_gh_583_trigger_atexit.lua'
    treegen.write_file(dir, script_name, [[
box.cfg()

local function test_replace()
end

box.schema.space.create('abc')
box.space.abc:create_index('pk', { type = 'tree' })
box.space.abc:on_replace(test_replace)

os.exit(0)
]])
    local opts = { nojson = true }
    local res = justrun.tarantool(dir, {}, { script_name }, opts)
    t.assert_equals(res.exit_code, 0)
end
