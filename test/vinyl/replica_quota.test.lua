test_run = require('test_run').new()

box.schema.user.grant('guest', 'read,write,execute', 'universe')
box.schema.user.grant('guest', 'replication')

s = box.schema.space.create('test', { engine = 'vinyl' })
_ = s:create_index('pk')

-- Send > 2 MB to replica.
pad = string.rep('x', 1100)
for i = 1,1000 do s:insert{i, pad} end
box.snapshot()
for i = 1001,2000 do s:insert{i, pad} end

-- Replica has memory limit set to 1 MB so replication would hang
-- if the scheduler didn't work on the destination.
_ = test_run:cmd("create server replica with rpl_master=default, script='vinyl/join_quota.lua'")
_ = test_run:cmd("start server replica")
_ = test_run:wait_lsn('replica', 'default')
_ = test_run:cmd("stop server replica")
_ = test_run:cmd("cleanup server replica")

s:drop()

box.schema.user.revoke('guest', 'replication')
box.schema.user.revoke('guest', 'read,write,execute', 'universe')
