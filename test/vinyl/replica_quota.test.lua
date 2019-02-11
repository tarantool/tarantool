test_run = require('test_run').new()

box.schema.user.grant('guest', 'replication')

s = box.schema.space.create('test', { engine = 'vinyl' })
_ = s:create_index('pk', {run_count_per_level = 1})
_ = s:create_index('sk', {unique = false, parts = {2, 'unsigned'}})

test_run:cmd("setopt delimiter ';'")
pad = string.rep('x', 10000);
function fill()
    for i = 1, 50 do
        box.begin()
        for j = 1, 10 do
            s:replace{math.random(100), math.random(100), pad}
        end
        box.commit()
    end
end;
test_run:cmd("setopt delimiter ''");

-- Send > 1 MB to replica.
fill()
box.snapshot()
fill()

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
fill()
_ = test_run:cmd("start server replica")
_ = test_run:wait_lsn('replica', 'default')

-- During join we remove compacted run files immediately (gh-3162).
-- Check that we don't delete files that are still in use.
_ = test_run:cmd("stop server replica")
_ = test_run:cmd("cleanup server replica")

box.snapshot()
fill()

_ = test_run:cmd("start server replica") -- join
_ = test_run:cmd("stop server replica")
_ = test_run:cmd("start server replica") -- recovery

_ = test_run:cmd("stop server replica")
_ = test_run:cmd("cleanup server replica")

s:drop()

box.schema.user.revoke('guest', 'replication')
