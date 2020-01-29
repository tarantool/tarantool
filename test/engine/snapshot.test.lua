
-- write data recover from latest snapshot

env = require('test_run')
test_run = env.new()
test_run:cmd('restart server default')
engine = test_run:get_cfg('engine')

space = box.schema.space.create('test', { engine = engine })
index = space:create_index('primary')

for key = 1, 51 do space:insert({key}) end
box.snapshot()

test_run:cmd('restart server default')

space = box.space['test']
index = space.index['primary']
index:select({}, {iterator = box.index.ALL})
for key = 52, 91 do space:insert({key}) end
box.snapshot()

test_run:cmd('restart server default')

space = box.space['test']
index = space.index['primary']
index:select({}, {iterator = box.index.ALL})

box.space.test:drop()

-- https://github.com/tarantool/tarantool/issues/1899
engine = test_run:get_cfg('engine')
space = box.schema.space.create('test', { engine = engine })
index1 = space:create_index('primary', { parts = {1, 'unsigned'} } )
index2 = space:create_index('secondary', { parts = {2, 'unsigned'} } )
space:insert{1, 11, 21}
space:insert{20, 10, 0}
box.snapshot()
test_run:cmd('restart server default')
box.space.test:select{}
box.space.test.index.primary:select{}
box.space.test.index.secondary:select{}
box.space.test:drop()

-- Hard way to flush garbage slabs in the fiber's region. See
-- gh-4750.
test_run:cmd('restart server default')

-- Check that box.snapshot() doesn't leave garbage one the region.
-- https://github.com/tarantool/tarantool/issues/3732
fiber = require('fiber')
-- Should be 0.
fiber.info()[fiber.self().id()].memory.used
box.snapshot()
-- Should be 0.
fiber.info()[fiber.self().id()].memory.used
box.snapshot()
box.snapshot()
-- Should be 0.
fiber.info()[fiber.self().id()].memory.used
