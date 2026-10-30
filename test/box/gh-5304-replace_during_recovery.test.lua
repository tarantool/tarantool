
-- write data recover from latest snapshot
env = require('test_run')
test_run = env.new()

test_run:cmd('create server test with script="box/replace_during_recovery.lua"')
test_run:cmd('start server test')
test_run:cmd('switch test')
s0 = box.schema.space.create('test')
i0 = s0:create_index('test')
s1 = box.schema.space.create('temp', {temporary=true})
i1 = s1:create_index('test')
s2 = box.schema.space.create('loc', {is_local=true})
i2 = s2:create_index('test')
s0:replace{1}
box.snapshot()

test_run:cmd('switch default')
test_run:cmd('stop server test')
test_run:cmd('start server test with args="replace" with crash_expected=True')
test_run:cmd('cleanup server test')
test_run:cmd('delete server test')
