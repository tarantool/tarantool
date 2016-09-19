env = require('test_run')
test_run = env.new()
-- write data recover from latest snapshot and logs

test_run:cmd("restart server default")

engine = test_run:get_cfg('engine')
space = box.schema.space.create('test', { engine = engine })
index = space:create_index('primary')

space:insert({0})
box.snapshot()
space:insert({1001})

test_run:cmd("restart server default")

space = box.space['test']
index = space.index['primary']
index:select({}, {iterator = box.index.ALL})

for key = 1, 351 do space:insert({key}) end
box.snapshot()
for key = 352, 1000 do space:insert({key}) end

test_run:cmd("restart server default")

space = box.space['test']
index = space.index['primary']
index:select({}, {iterator = box.index.ALL})

space:drop()
test_run:cmd("restart server default with cleanup=1")
