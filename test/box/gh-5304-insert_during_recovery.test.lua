
-- write data recover from latest snapshot
env = require('test_run')
test_run = env.new()

test_run:cmd('create server test with script="box/insert_during_recovery.lua"')
test_run:cmd('start server test with args="none"')
test_run:cmd('switch test')
s1 = box.schema.space.create('temp', {temporary=true})
i1 = s1:create_index('test')
s2 = box.schema.space.create('loc', {is_local=true})
i2 = s2:create_index('test')
box.snapshot()

test_run:cmd('switch default')
test_run:cmd('stop server test')
test_run:cmd('start server test with args="replace" with crash_expected=True')
test_run:cmd('start server test with args="insert" with crash_expected=True')
test_run:cmd('start server test with args="upsert" with crash_expected=True')
test_run:cmd('start server test with args="replace is_recovery_finished"')
test_run:cmd('switch test')
box.space.temp:select()
box.space.loc:select()
-- Creating a new space and index invokes on_replace trigger in _index space,
-- which inserts tuples in temp and loc spaces (see gh-5304-insert_during_recovery.lua)!
s0 = box.schema.space.create('test')
i0 = s0:create_index('test')
box.space.temp:select()
box.space.loc:select()
s0:drop()

test_run:cmd('switch default')
test_run:cmd('stop server test')
test_run:cmd('start server test with args="insert is_recovery_finished"')
test_run:cmd('switch test')
box.space.temp:select()
box.space.loc:select()
-- Creating a new space and index invoke on_replace trigger in _index space.
-- Here we get error during insert tuple in loc space, because the tuple
-- with the same primary key is already in the loc space
s0 = box.schema.space.create('test')
i0 = s0:create_index('test')
box.space.temp:select()
box.space.loc:select()
s0:drop()

test_run:cmd('switch default')
test_run:cmd('stop server test')
test_run:cmd('start server test with args="upsert is_recovery_finished"')
test_run:cmd('switch test')
box.space.temp:select()
box.space.loc:select()
s0 = box.schema.space.create('test')
i0 = s0:create_index('test')
box.space.temp:select()
box.space.loc:select()
s0:drop()

test_run:cmd('switch default')
test_run:cmd('stop server test')
test_run:cmd('cleanup server test')
test_run:cmd('delete server test')
