-- The test tests init script.

local t = require('luatest')
local treegen = require('luatest.treegen')
local justrun = require('luatest.justrun')

local g = t.group()

-- Access to box.cfg from init script.
g.test_access_box_cfg_init_script= function()
    local dir = treegen.prepare_directory({}, {})
    local script_name = 'test_access_box_cfg_init_script.lua'
    treegen.write_file(dir, script_name, [[
box.cfg({memtx_memory = 107374182})
assert(box.cfg.memtx_memory == 107374182)
os.exit(0)
    ]])
    local opts = {nojson = true}
    local res = justrun.tarantool(dir, {}, { script_name }, opts)
    t.assert_equals(res.exit_code, 0, 'exit code')
    t.assert_equals(res.stdout, '', 'stdout')
end

-- The testcases tests insert tuples using fiber in init script.
g.test_insert_tuples_fiber_init_script = function()
    local dir = treegen.prepare_directory({}, {})
    local script_name = 'test_insert_tuples_fiber_init_script.lua'
    treegen.write_file(dir, script_name, [[
local fiber = require('fiber')
local json = require('json')

box.cfg()
local space = box.schema.space.create('tweedledum')
space:create_index('primary', { type = 'hash' })

function do_insert()
    space:insert({1, 2, 4, 8})
end

fiber.create(do_insert)

assert(json.encode(space:select(nil, { limit = 4 })[1] == '[1,2,4,8]'))

os.exit(0)
    ]])
    local opts = {nojson = true}
    local res = justrun.tarantool(dir, {}, { script_name }, opts)
    t.assert_equals(res.exit_code, 0, 'exit code')
    t.assert_equals(res.stdout, '', 'stdout')
end

-- The testcase tests insert from detached fiber.
g.test_insert_tuples_and_select_init_script = function()
    local dir = treegen.prepare_directory({}, {})
    local script_name = 'test_insert_tuples_and_select_init_script.lua'
    treegen.write_file(dir, script_name, [[
local json = require('json')
local fiber = require('fiber')

box.cfg()
local space = box.schema.space.create('tweedledum')
space:create_index('primary', { type = 'hash' })

fiber.create(function()
    space:insert({1, 2, 4, 8})
end)

local str = json.encode(space:select(nil, { limit = 4 })[1])
assert(str == '[1,2,4,8]')

os.exit(0)
    ]])
    local opts = {nojson = true}
    local res = justrun.tarantool(dir, {}, { script_name }, opts)
    t.assert_equals(res.exit_code, 0, 'exit code')
    t.assert_equals(res.stdout, '', 'stdout')
end

-- The testcase tests insert from an init script.
g.test_insert_tuples_init_script = function()
    local dir = treegen.prepare_directory({}, {})
    local script_name = 'test_insert_tuples_and_get_init_script.lua'
    treegen.write_file(dir, script_name, [[
local json = require('json')

box.cfg()
local space = box.schema.space.create('tweedledum')
space:create_index('primary', { type = 'hash' })

space:insert({2, 4, 8, 16})
assert(space:get(1) == nil)
assert(json.encode(space:get(2)) == '[2,4,8,16]')
assert(space:get(3) == nil)
assert(space:get(4) == nil)

os.exit(0)
    ]])
    local opts = {nojson = true}
    local res = justrun.tarantool(dir, {}, { script_name }, opts)
    t.assert_equals(res.exit_code, 0, 'exit code')
    t.assert_equals(res.stdout, '', 'stdout')
end

-- The testcase checks that `math.floor` is reachable in the init
-- script.
g.test_require_math_floor_init_script = function()
    local dir = treegen.prepare_directory({}, {})
    local script_name = 'test_require_math_floor_init_script.lua'
    treegen.write_file(dir, script_name, [[
local floor = math.floor
assert(floor(0.9) == 0)
assert(floor(1.1) == 1)

os.exit(0)
    ]])
    local opts = {nojson = true}
    local res = justrun.tarantool(dir, {}, { script_name }, opts)
    t.assert_equals(res.exit_code, 0, 'exit code')
    t.assert_equals(res.stdout, '', 'stdout')
end

-- A test case for tarantool/tarantool#53.
g.test_gh_53 = function()
    local dir = treegen.prepare_directory({}, {})
    local script_name = 'test_gh_53.lua'
    treegen.write_file(dir, script_name, [[
assert(require ~= nil)
local fiber = require('fiber')
fiber.sleep(0.0)
assert(require ~= nil)

os.exit(0)
    ]])
    local opts = {nojson = true}
    local res = justrun.tarantool(dir, {}, { script_name }, opts)
    t.assert_equals(res.exit_code, 0, 'exit code')
end
