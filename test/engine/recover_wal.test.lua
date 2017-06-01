
-- write data recover from logs only

env = require('test_run')
test_run = env.new()
test_run:cmd('restart server default')

engine = test_run:get_cfg('engine')
space = box.schema.space.create('test', { engine = engine })
index = space:create_index('primary')

space:insert({0})

test_run:cmd('restart server default')

space = box.space['test']
index = space.index['primary']
index:select({}, {iterator = box.index.ALL})

for key = 1, 1000 do space:insert({key}) end

test_run:cmd('restart server default')

space = box.space['test']
index = space.index['primary']
index:select({}, {iterator = box.index.ALL})
space:drop()
test_run:cmd('restart server default with cleanup=1')
