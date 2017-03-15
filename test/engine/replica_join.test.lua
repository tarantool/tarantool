env = require('test_run')
test_run = env.new()
engine = test_run:get_cfg('engine')
index = test_run:get_cfg('index')
box.schema.user.grant('guest', 'read,write,execute', 'universe')
box.schema.user.grant('guest', 'replication')
space = box.schema.space.create('test', { id = 99999, engine = engine })
index = space:create_index('primary', { type = index})
box.snapshot()

-- replica join
test_run:cmd("create server replica with rpl_master=default, script='replication/replica.lua'")
test_run:cmd("start server replica")
test_run:cmd('wait_lsn replica default')
test_run:cmd('switch replica')
box.space.test:select()
test_run:cmd('switch default')
test_run:cmd("stop server replica")
_ = test_run:cmd("cleanup server replica")

-- new data
box.space.test:insert{1, 1}

-- replica join
test_run:cmd("deploy server replica")
test_run:cmd("start server replica")
test_run:cmd('wait_lsn replica default')
test_run:cmd('switch replica')
box.space.test:select()
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
test_run:cmd('switch default')
test_run:cmd("stop server replica")
_ = test_run:cmd("cleanup server replica")

-- new data
for k = 2, 123 do box.space.test:insert{k, k*k} end
box.snapshot()

-- replica join
test_run:cmd("deploy server replica")
test_run:cmd("start server replica")
test_run:cmd('wait_lsn replica default')
test_run:cmd('switch replica')
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
