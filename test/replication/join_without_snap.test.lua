test_run = require('test_run').new()

--
-- gh-1271: check that replica join works off the current read view,
-- not the last checkpoint. To do that, delete the last snapshot file
-- and check that a replica can still join.
--
_ = box.schema.space.create('test')
_ = box.space.test:create_index('pk')
for i = 1, 5 do box.space.test:insert{i} end
box.snapshot()

fio = require('fio')
fio.unlink(fio.pathjoin(box.cfg.memtx_dir, string.format('%020d.snap', box.info.signature)))

box.schema.user.grant('guest', 'replication')

test_run:cmd('create server replica with rpl_master=default, script="replication/replica.lua"')
test_run:cmd('start server replica')
test_run:cmd('switch replica')

box.space.test:select()

test_run:cmd('switch default')
test_run:cmd('stop server replica')
test_run:cmd('cleanup server replica')
test_run:cmd('delete server replica')
test_run:cleanup_cluster()

box.schema.user.revoke('guest', 'replication')
box.space.test:drop()
box.snapshot()
