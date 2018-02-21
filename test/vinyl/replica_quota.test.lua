test_run = require('test_run').new()

box.schema.user.grant('guest', 'read,write,execute', 'universe')
box.schema.user.grant('guest', 'replication')

s = box.schema.space.create('test', { engine = 'vinyl' })
_ = s:create_index('pk', {run_count_per_level = 1})

-- Send > 2 MB to replica.
pad = string.rep('x', 1100)
for i = 1,1000 do s:insert{i, pad} end
box.snapshot()
for i = 1001,2000 do s:insert{i, pad} end

-- Replica has memory limit set to 1 MB so replication would hang
-- if the scheduler didn't work on the destination.
--
-- Also check that quota timeout isn't taken into account while
-- the replica is joining (see gh-2873). To do that, we set
-- vinyl_timeout to 1 ms on the replica, which isn't enough for
-- a dump to complete and hence would result in bootstrap failure
-- were the timeout not ignored.
--
_ = test_run:cmd("create server replica with rpl_master=default, script='vinyl/replica_quota.lua'")
_ = test_run:cmd("start server replica")
_ = test_run:wait_lsn('replica', 'default')

-- Check vinyl_timeout is ignored on 'subscribe' (gh-3087).
_ = test_run:cmd("stop server replica")
for i = 2001,3000 do s:insert{i, pad} end
_ = test_run:cmd("start server replica")
_ = test_run:wait_lsn('replica', 'default')

-- During join we remove compacted run files immediately (gh-3162).
-- Check that we don't delete files that are still in use.
_ = test_run:cmd("stop server replica")
_ = test_run:cmd("cleanup server replica")

box.snapshot()
for i = 3001,4000 do s:insert{i, pad} end

_ = test_run:cmd("start server replica") -- join
_ = test_run:cmd("stop server replica")
_ = test_run:cmd("start server replica") -- recovery

_ = test_run:cmd("stop server replica")
_ = test_run:cmd("cleanup server replica")

s:drop()

box.schema.user.revoke('guest', 'replication')
box.schema.user.revoke('guest', 'read,write,execute', 'universe')
