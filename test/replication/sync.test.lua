fiber = require('fiber')
test_run = require('test_run').new()
engine = test_run:get_cfg('engine')

test_run:cleanup_cluster()

box.schema.user.grant('guest', 'replication')
_ = box.schema.space.create('test', {engine = engine})
_ = box.space.test:create_index('pk')

-- A helper function to check that replication sync actually works.
-- It inserts some records into the test space, then starts a fiber
-- that performs the following steps in the background:
--
-- 1. Stall replication with the aid of error injection.
-- 2. Wait for the test replica to subscribe.
-- 3. Insert some more records into the test space.
-- 4. Turn off replication delay.
--
-- We need the asynchronous part, because a replica doesn't stop
-- syncing until it receives all data that existed on the master
-- when it subscribed. So to check that it doesn't stop syncing
-- until the time lag is less than configured, we have to insert
-- some records after the replica subscribes, but before it leaves
-- box.cfg() hence the need of replication delay.
test_run:cmd("setopt delimiter ';'")
count = 0;
function fill()
    for i = count + 1, count + 100 do
        box.space.test:replace{i}
    end
    fiber.create(function()
        box.error.injection.set('ERRINJ_RELAY_TIMEOUT', 0.0025)
        test_run:wait_cond(function()
            local r = box.info.replication[2]
            return r ~= nil and r.downstream ~= nil and
                   r.downstream.status ~= 'stopped'
        end, 10)
        for i = count + 101, count + 200 do
            box.space.test:replace{i}
        end
    end)
    count = count + 200
end;
test_run:cmd("setopt delimiter ''");

-- Deploy a replica.
test_run:cmd("create server replica with rpl_master=default, script='replication/replica.lua'")
test_run:cmd("start server replica")
test_run:cmd("switch replica")

-- Stop replication.
replication = box.cfg.replication
box.cfg{replication = {}}

-- Fill the space.
test_run:cmd("switch default")
fill()
test_run:cmd("switch replica")

-- Resume replication.
--
-- Since max allowed lag is small, all records should arrive
-- by the time box.cfg() returns.
--
box.cfg{replication_sync_lag = 0.001}
box.cfg{replication = replication}
box.space.test:count()
box.info.status -- running
box.info.ro -- false

-- Stop replication.
replication = box.cfg.replication
box.cfg{replication = {}}

-- Fill the space.
test_run:cmd("switch default")
fill()
test_run:cmd("switch replica")

-- Resume replication
--
-- Since max allowed lag is big, not all records will arrive
-- upon returning from box.cfg() but the instance won't enter
-- orphan state.
--
box.cfg{replication_sync_lag = 1}
box.cfg{replication = replication}
box.space.test:count() < 400
box.info.status -- running
box.info.ro -- false

-- Wait for remaining rows to arrive.
test_run:wait_cond(function() return box.space.test:count() == 400 end, 10)

-- Stop replication.
replication = box.cfg.replication
box.cfg{replication = {}}

-- Fill the space.
test_run:cmd("switch default")
fill()
test_run:cmd("switch replica")

-- Resume replication
--
-- Although max allowed lag is small, box.cfg() will fail to
-- synchronize and leave the instance in orphan state, because
-- the timeout is too small to receive all records in time.
--
box.cfg{replication_sync_lag = 0.001, replication_sync_timeout = 0.001}
box.cfg{replication = replication}
box.space.test:count() < 600
box.info.status -- orphan
box.info.ro -- true

-- Wait for remaining rows to arrive.
test_run:wait_cond(function() return box.space.test:count() == 600 end, 10)

-- Make sure replica leaves oprhan state.
test_run:wait_cond(function() return box.info.status ~= 'orphan' end, 10)
box.info.status -- running
box.info.ro -- false

-- gh-3636: Check that replica set sync doesn't stop on cfg errors.
-- To do that we inject an error on the master to temporarily block
-- the relay thread from exiting, then reconfigure replication on
-- the slave using the same configuration. Since the relay is still
-- running when replication is reconfigured, the replica will get
-- ER_CFG "duplicate connection with the same replica UUID" error.
-- It should print it to the log, but keep trying to synchronize.
-- Eventually, it should leave box.cfg() following the master.
box.cfg{replication_timeout = 0.1}
box.cfg{replication_sync_lag = 1}
box.cfg{replication_sync_timeout = 10}

test_run:cmd("switch default")
box.error.injection.set('ERRINJ_RELAY_TIMEOUT', 0)
box.error.injection.set('ERRINJ_WAL_DELAY', true)
test_run:cmd("setopt delimiter ';'")
_ = fiber.create(function()
    box.space.test:replace{123456789}
end);
_ = fiber.create(function()
    fiber.sleep(0.1)
    box.error.injection.set('ERRINJ_WAL_DELAY', false)
end);
test_run:cmd("setopt delimiter ''");
test_run:cmd("switch replica")

replication = box.cfg.replication
box.cfg{replication = {}}
box.cfg{replication = replication}
box.info.status -- running
box.info.ro -- false
box.info.replication[1].upstream.status -- follow
test_run:wait_log("replica", "ER_CFG.*", nil, 200)

test_run:cmd("switch default")
test_run:cmd("stop server replica")

-- gh-3830: Sync fails if there's a gap at the end of the master's WAL.
box.error.injection.set('ERRINJ_WAL_WRITE_DISK', true)
box.space.test:replace{123456789}
box.error.injection.set('ERRINJ_WAL_WRITE_DISK', false)
test_run:cmd("start server replica")
test_run:cmd("switch replica")
box.info.status -- running
box.info.ro -- false

test_run:cmd("switch default")
test_run:cmd("stop server replica")
test_run:cmd("cleanup server replica")
test_run:cmd("delete server replica")
test_run:cleanup_cluster()

box.space.test:drop()
box.schema.user.revoke('guest', 'replication')
