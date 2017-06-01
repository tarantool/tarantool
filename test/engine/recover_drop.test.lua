-- recover dropped spaces

env = require('test_run')
test_run = env.new()
test_run:cmd('restart server default')
engine = test_run:get_cfg('engine')

space = box.schema.space.create('test', { engine = engine })
index = space:create_index('primary')
for key = 1, 351 do space:insert({key}) end
space:drop()

space = box.schema.space.create('test', { engine = engine })
index = space:create_index('primary')
for key = 500, 1000 do space:insert({key}) end

test_run:cmd('restart server default')

space = box.space['test']
index = space.index['primary']
index:select({}, {iterator = box.index.ALL})
space:drop()

test_run:cmd('restart server default with cleanup=1')
