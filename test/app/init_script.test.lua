#!/usr/bin/env tarantool
--
-- Testing init script
--
box.cfg{
    admin_port = 3313,
    primary_port = 3314,
    pid_file = "box.pid",
}

yaml = require('yaml')
fiber = require('fiber')

space = box.schema.create_space('tweedledum', { id = 0 })
space:create_index('primary', { type = 'hash' })

print[[
--
-- Access to box.cfg from init script
--
]]
t = {}

for k,v in pairs(box.cfg) do if type(v) ~= 'table' and type(v) ~= 'function' then table.insert(t,k..':'..tostring(v)) end end

print('box.cfg')
for k,v in pairs(t) do print(k, v) end
--
-- Insert tests
--
local function do_insert()
    fiber.detach()
    space:insert{1, 2, 4, 8}
end

fiber1 = fiber.create(do_insert)
fiber.resume(fiber1)

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
floor = require("math").floor
print(floor(0.5))
print(floor(0.9))
print(floor(1.1))

mod = dofile('../app/require_mod.lua')
print(mod.test(10, 15))
--
-- A test case for https://github.com/tarantool/tarantool/issues/53
--
assert (require ~= nil)
fiber.sleep(0.0)
assert (require ~= nil)

space:drop()
os.exit()
