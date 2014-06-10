#!/usr/bin/env tarantool
--
-- Testing start-up script
--
box.cfg{
    admin_port = 3313,
    primary_port = 3314,
    pid_file = "box.pid",
}

floor = require("math").floor
yaml = require('yaml')

space = box.schema.create_space('tweedledum', { id = 0 })
space:create_index('primary', { type = 'hash' })

--
-- Access to box.cfg from start-up script
--

t = {}

for k,v in pairs(box.cfg) do if type(v) ~= 'table' and type(v) ~= 'function' then table.insert(t,k..':'..tostring(v)) end end

print('box.cfg')
for k,v in pairs(t) do print(k, v) end

--
-- Insert tests
--

fiber = require('fiber')

local function do_insert()
    fiber.detach()
    space:insert{1, 2, 4, 8}
end

fiber1 = fiber.create(do_insert)
fiber.resume(fiber1)

print('\nTest insert from detached fiber')
print(yaml.encode(space:select()))

print('Test insert from start-up script')
space:insert{2, 4, 8, 16}
print(space:get(1))
print(space:get(2))

--
-- Run a dummy insert to avoid race conditions under valgrind
--
space:insert{4, 8, 16}
print(space:get(4))

--
-- A test case for https://github.com/tarantool/tarantool/issues/53
--

assert (require ~= nil)
fiber.sleep(0.0)
assert (require ~= nil)

space:drop()
os.exit()
