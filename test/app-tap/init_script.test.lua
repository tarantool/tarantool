#!/usr/bin/env tarantool
--
-- Testing init script
--
box.cfg{
    listen = os.getenv("LISTEN"),
    pid_file = "box.pid",
    memtx_memory=107374182,
    log="tarantool.log"
}

local yaml = require('yaml')
local fiber = require('fiber')

if box.space.tweedledum ~= nil then
    box.space.space1:drop()
end

local space = box.schema.space.create('tweedledum')
space:create_index('primary', { type = 'hash' })

print[[
--
-- Access to box.cfg from init script
--
]]
local t = {}

for k,v in pairs(box.cfg) do
    if k == 'listen' then
        v = 'port'
    end
    if type(v) ~= 'table' and type(v) ~= 'function' then
        table.insert(t,k..':'..tostring(v))
    end
end
table.sort(t)
print('box.cfg')
for _,v in pairs(t) do print(v) end
--
-- Insert tests
--
local function do_insert()
    space:insert{1, 2, 4, 8}
end

fiber.create(do_insert)

print[[
--
-- Test insert from detached fiber
--
]]
print(yaml.encode(space:select()))

print[[
--
-- Test insert from init script
--
]]
space:insert{2, 4, 8, 16}
print(space:get(1))
print(space:get(2))
--
-- Run a dummy insert to avoid race conditions under valgrind
--
space:insert{4, 8, 16}
print(space:get(4))
print[[
--
-- Check that require function(math.floor) reachable in the init script
--
]]
local floor = require("math").floor
print(floor(0.5))
print(floor(0.9))
print(floor(1.1))

local mod = require('require_mod')
print(mod.test(10, 15))
--
-- A test case for https://github.com/tarantool/tarantool/issues/53
--
assert (require ~= nil)
fiber.sleep(0.0)
assert (require ~= nil)

space:drop()
os.exit()
