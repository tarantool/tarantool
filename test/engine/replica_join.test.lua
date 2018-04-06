env = require('test_run')
test_run = env.new()
engine = test_run:get_cfg('engine')
index = test_run:get_cfg('index')
box.schema.user.grant('guest', 'read,write,execute', 'universe')
box.schema.user.grant('guest', 'replication')
space = box.schema.space.create('test', { id = 99999, engine = engine })
_ = space:create_index('primary', { type = index})
_ = space:create_index('secondary', { type = index, unique = false, parts = {2, 'unsigned'}})
space2 = box.schema.space.create('test2', { id = 99998, engine = engine})
_ = space2:create_index('primary', { parts = {1, 'unsigned', 2, 'string'}})
space3 = box.schema.space.create('test3', { id = 99997, engine = engine})
_ = space3:create_index('primary', { parts = {{1, 'string', collation = 'unicode_ci'}}})
box.snapshot()

-- replica join
test_run:cmd("create server replica with rpl_master=default, script='replication/replica.lua'")
test_run:cmd("start server replica")
test_run:wait_lsn('replica', 'default')
test_run:cmd('switch replica')
box.space.test:select()
box.space.test.index.secondary:select()
box.space.test2:select()
box.space.test3:select()
test_run:cmd('switch default')
test_run:cmd("stop server replica")
_ = test_run:cmd("cleanup server replica")

-- new data
for k = 1, 8 do box.space.test:insert{k, 17 - k} end
for k = 16, 9, -1 do box.space.test:insert{k, 17 - k} end
_ = box.space.test2:insert{1, 'test1', 1}
_ = box.space.test2:upsert({1, 'test1', 10}, {{'=', 3, 10}})
_ = box.space.test2:upsert({2, 'test2', 20}, {{'=', 3, 20}})
_ = box.space.test2:insert{3, 'test3', 30}
_ = box.space.test3:insert{'Ёж'}
_ = box.space.test3:insert{'ель'}
_ = box.space.test3:insert{'Юла'}
_ = box.space.test3:insert{'Эль'}
_ = box.space.test3:insert{'ёлка'}
_ = box.space.test3:insert{'йогурт'}

-- replica join
test_run:cmd("deploy server replica")
test_run:cmd("start server replica")
test_run:wait_lsn('replica', 'default')
test_run:cmd('switch replica')
box.space.test:select()
box.space.test.index.secondary:select()
box.space.test2:select()
box.space.test3:select()
test_run:cmd('switch default')
test_run:cmd("stop server replica")
_ = test_run:cmd("cleanup server replica")

-- add snapshot
box.snapshot()

-- replica join
test_run:cmd("deploy server replica")
test_run:cmd("start server replica")
test_run:wait_lsn('replica', 'default')
test_run:cmd('switch replica')
box.space.test:select()
box.space.test.index.secondary:select()
box.space.test2:select()
box.space.test3:select()
test_run:cmd('switch default')
test_run:cmd("stop server replica")
_ = test_run:cmd("cleanup server replica")

-- new data
for k = 8, 1, -1 do box.space.test:update(k, {{'-', 2, 8}}) end
for k = 9, 16 do box.space.test:delete(k) end
box.space.test.index.primary:alter{parts = {1, 'integer'}}
for k = -8, -1 do box.space.test:insert{k, 9 + k} end
_ = box.space.test2:upsert({1, 'test1', 11}, {{'+', 3, 1}})
_ = box.space.test2:update({2, 'test2'}, {{'+', 3, 2}})
_ = box.space.test2:delete{3, 'test3'}
_ = box.space.test3:upsert({'ёж', 123}, {{'!', 2, 123}})
_ = box.space.test3:update('ЭЛЬ', {{'!', 2, 456}})
_ = box.space.test3:delete('ёлка')
box.snapshot()

-- replica join
test_run:cmd("deploy server replica")
test_run:cmd("start server replica")
test_run:wait_lsn('replica', 'default')
test_run:cmd('switch replica')
box.space.test:select()
box.space.test.index.secondary:select()
box.space.test2:select()
box.space.test3:select()
test_run:cmd('switch default')
test_run:cmd("stop server replica")
_ = test_run:cmd("cleanup server replica")

-- recreate space
space:drop()
space = box.schema.space.create('test', { id = 12345, engine = engine })
_ = space:create_index('primary', { type = index})
_ = space:insert{12345}
-- truncate space
space3:truncate()

-- replica join
test_run:cmd("deploy server replica")
test_run:cmd("start server replica")
test_run:wait_lsn('replica', 'default')
test_run:cmd('switch replica')
box.space.test.id
box.space.test:select()
box.space.test2:select()
box.space.test3:select()
test_run:cmd('switch default')
test_run:cmd("stop server replica")
_ = test_run:cmd("cleanup server replica")

space:drop()
space2:drop()
space3:drop()
box.snapshot()

space = box.schema.space.create('test', { id = 99998, engine = engine })
index = space:create_index('primary', { type = test_run:get_cfg('index')})
for i = 0, 9 do space:insert({i, 'test' .. tostring(i)}) end
test_run:cmd("deploy server replica")
test_run:cmd("start server replica")
test_run:wait_lsn('replica', 'default')
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
