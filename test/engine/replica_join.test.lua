env = require('test_run')
test_run = env.new()
engine = test_run:get_cfg('engine')
index = test_run:get_cfg('index')
box.schema.user.grant('guest', 'read,write,execute', 'universe')
box.schema.user.grant('guest', 'replication')
space = box.schema.space.create('test', { id = 99999, engine = engine })
_ = space:create_index('primary', { type = index})
_ = space:create_index('secondary', { type = index, unique = false, parts = {2, 'unsigned'}})
box.snapshot()

-- replica join
test_run:cmd("create server replica with rpl_master=default, script='replication/replica.lua'")
test_run:cmd("start server replica")
test_run:cmd('wait_lsn replica default')
test_run:cmd('switch replica')
box.space.test:select()
box.space.test.index.secondary:select()
test_run:cmd('switch default')
test_run:cmd("stop server replica")
_ = test_run:cmd("cleanup server replica")

-- new data
for k = 1, 8 do box.space.test:insert{k, 17 - k} end
for k = 16, 9, -1 do box.space.test:insert{k, 17 - k} end

-- replica join
test_run:cmd("deploy server replica")
test_run:cmd("start server replica")
test_run:cmd('wait_lsn replica default')
test_run:cmd('switch replica')
box.space.test:select()
box.space.test.index.secondary:select()
test_run:cmd('switch default')
test_run:cmd("stop server replica")
_ = test_run:cmd("cleanup server replica")

-- add snapshot
box.snapshot()

-- replica join
test_run:cmd("deploy server replica")
test_run:cmd("start server replica")
test_run:cmd('wait_lsn replica default')
test_run:cmd('switch replica')
box.space.test:select()
box.space.test.index.secondary:select()
test_run:cmd('switch default')
test_run:cmd("stop server replica")
_ = test_run:cmd("cleanup server replica")

-- new data
for k = 8, 1, -1 do box.space.test:update(k, {{'-', 2, 8}}) end
for k = 9, 16 do box.space.test:delete(k) end
box.snapshot()

-- replica join
test_run:cmd("deploy server replica")
test_run:cmd("start server replica")
test_run:cmd('wait_lsn replica default')
test_run:cmd('switch replica')
box.space.test:select()
box.space.test.index.secondary:select()
test_run:cmd('switch default')
test_run:cmd("stop server replica")
_ = test_run:cmd("cleanup server replica")

-- recreate space
space:drop()
space = box.schema.space.create('test', { id = 12345, engine = engine })
_ = space:create_index('primary', { type = index})
_ = space:insert{12345}

-- replica join
test_run:cmd("deploy server replica")
test_run:cmd("start server replica")
test_run:cmd('wait_lsn replica default')
test_run:cmd('switch replica')
box.space.test.id
box.space.test:select()
test_run:cmd('switch default')
test_run:cmd("stop server replica")
_ = test_run:cmd("cleanup server replica")

space:drop()
box.snapshot()

space = box.schema.space.create('test', { id = 99998, engine = engine })
index = space:create_index('primary', { type = test_run:get_cfg('index')})
for i = 0, 9 do space:insert({i, 'test' .. tostring(i)}) end
test_run:cmd("deploy server replica")
test_run:cmd("start server replica")
test_run:cmd('wait_lsn replica default')
test_run:cmd('restart server replica')
test_run:cmd('switch replica')
box.space.test:select()
test_run:cmd('switch default')
test_run:cmd("stop server replica")
_ = test_run:cmd("cleanup server replica")
space:drop()
box.snapshot()

box.schema.user.revoke('guest', 'replication')
box.schema.user.revoke('guest', 'read,write,execute', 'universe')
