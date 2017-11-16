test_run = require('test_run').new()
engine = test_run:get_cfg('engine')
replica_set = require('fast_replica')
fiber = require('fiber')

test_run:cleanup_cluster()

-- Make each snapshot trigger garbage collection.
default_checkpoint_count = box.cfg.checkpoint_count
box.cfg{checkpoint_count = 1}

function wait_gc(n) while #box.internal.gc.info().checkpoints > n do fiber.sleep(0.01) end end

-- Grant permissions needed for replication.
box.schema.user.grant('guest', 'read,write,execute', 'universe')
box.schema.user.grant('guest', 'replication')

-- By default, relay thread reports status to tx once a second.
-- To reduce the test execute time, let's set it to 50 ms.
box.error.injection.set("ERRINJ_RELAY_REPORT_INTERVAL", 0.05)

-- Create and populate the space we will replicate.
s = box.schema.space.create('test', {engine = engine});
_ = s:create_index('pk')
for i = 1, 100 do s:auto_increment{} end
box.snapshot()
for i = 1, 100 do s:auto_increment{} end

-- Make sure replica join will take long enough for us to
-- invoke garbage collection.
box.error.injection.set("ERRINJ_RELAY_TIMEOUT", 0.05)

-- While the replica is receiving the initial data set,
-- make a snapshot and invoke garbage collection, then
-- remove the timeout injection so that we don't have to
-- wait too long for the replica to start.
test_run:cmd("setopt delimiter ';'")
fiber.create(function()
    fiber.sleep(0.1)
    box.snapshot()
    box.error.injection.set("ERRINJ_RELAY_TIMEOUT", 0)
end)
test_run:cmd("setopt delimiter ''");

-- Start the replica.
test_run:cmd("create server replica with rpl_master=default, script='replication/replica.lua'")
test_run:cmd("start server replica")

-- Despite the fact that we invoked garbage collection that
-- would have normally removed the snapshot the replica was
-- bootstrapped from, the replica should still receive all
-- data from the master. Check it.
test_run:cmd("switch replica")
fiber = require('fiber')
while box.space.test:count() < 200 do fiber.sleep(0.01) end
box.space.test:count()
test_run:cmd("switch default")

-- Check that garbage collection removed the snapshot once
-- the replica released the corresponding checkpoint.
wait_gc(1)
#box.internal.gc.info().checkpoints == 1 or box.internal.gc.info()

-- Make sure the replica will receive data it is subscribed
-- to long enough for us to invoke garbage collection.
box.error.injection.set("ERRINJ_RELAY_TIMEOUT", 0.05)

-- Send more data to the replica.
for i = 1, 100 do s:auto_increment{} end

-- Invoke garbage collection. Check that it doesn't remove
-- xlogs needed by the replica.
box.snapshot()
#box.internal.gc.info().checkpoints == 2 or box.internal.gc.info()

-- Remove the timeout injection so that the replica catches
-- up quickly.
box.error.injection.set("ERRINJ_RELAY_TIMEOUT", 0)

-- Check that the replica received all data from the master.
test_run:cmd("switch replica")
while box.space.test:count() < 300 do fiber.sleep(0.01) end
box.space.test:count()
test_run:cmd("switch default")

-- Now garbage collection should resume and delete files left
-- from the old checkpoint.
wait_gc(1)
#box.internal.gc.info().checkpoints == 1 or box.internal.gc.info()

--
-- Check that the master doesn't delete xlog files sent to the
-- replica until it receives a confirmation that the data has
-- been applied (gh-2825).
--
test_run:cmd("switch replica")
-- Prevent the replica from applying any rows.
box.error.injection.set("ERRINJ_WAL_DELAY", true)
test_run:cmd("switch default")
-- Generate some data on the master.
for i = 1, 5 do s:auto_increment{} end
box.snapshot() -- rotate xlog
for i = 1, 5 do s:auto_increment{} end
fiber.sleep(0.1) -- wait for master to relay data
-- Garbage collection must not delete the old xlog file
-- (and the corresponding snapshot), because it is still
-- needed by the replica.
#box.internal.gc.info().checkpoints == 2 or box.internal.gc.info()
test_run:cmd("switch replica")
-- Unblock the replica and make it fail to apply a row.
box.info.replication[1].upstream.message == nil
box.error.injection.set("ERRINJ_WAL_WRITE", true)
box.error.injection.set("ERRINJ_WAL_DELAY", false)
while box.info.replication[1].upstream.message == nil do fiber.sleep(0.01) end
box.info.replication[1].upstream.message
test_run:cmd("switch default")
-- Restart the replica to reestablish replication.
test_run:cmd("restart server replica")
-- Wait for the replica to catch up.
test_run:cmd("switch replica")
fiber = require('fiber')
while box.space.test:count() < 310 do fiber.sleep(0.01) end
box.space.test:count()
test_run:cmd("switch default")
-- Now it's safe to drop the old xlog.
wait_gc(1)
#box.internal.gc.info().checkpoints == 1 or box.internal.gc.info()

-- Stop the replica.
test_run:cmd("stop server replica")
test_run:cmd("cleanup server replica")

-- Invoke garbage collection. Check that it doesn't remove
-- the checkpoint last used by the replica.
_ = s:auto_increment{}
box.snapshot()
#box.internal.gc.info().checkpoints == 2 or box.internal.gc.info()

-- The checkpoint should only be deleted after the replica
-- is unregistered.
test_run:cleanup_cluster()
#box.internal.gc.info().checkpoints == 1 or box.internal.gc.info()

--
-- Test that concurrent invocation of the garbage collector works fine.
--
s:truncate()
for i = 1, 10 do s:replace{i} end
box.snapshot()

replica_set.join(test_run, 3)
replica_set.stop_all(test_run)

for i = 11, 50 do s:replace{i} if i % 10 == 0 then box.snapshot() end end

replica_set.start_all(test_run)
replica_set.wait_all(test_run)
replica_set.drop_all(test_run)

-- Cleanup.
s:drop()
box.error.injection.set("ERRINJ_RELAY_REPORT_INTERVAL", 0)
box.schema.user.revoke('guest', 'replication')
box.schema.user.revoke('guest', 'read,write,execute', 'universe')

box.cfg{checkpoint_count = default_checkpoint_count}
