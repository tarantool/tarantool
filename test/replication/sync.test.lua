fiber = require('fiber')
test_run = require('test_run').new()
engine = test_run:get_cfg('engine')

box.schema.user.grant('guest', 'replication')
_ = box.schema.space.create('test', {engine = engine})
_ = box.space.test:create_index('pk')

-- Slow down replication a little to test replication_sync_lag.
box.error.injection.set("ERRINJ_RELAY_TIMEOUT", 0.001)

-- Helper that adds some records to the space and then starts
-- a fiber to add more records in the background.
test_run:cmd("setopt delimiter ';'")
count = 0;
function fill()
    for i = count + 1, count + 100 do
        box.space.test:replace{i}
    end
    fiber.create(function()
        for i = count + 101, count + 200 do
            box.space.test:replace{i}
            fiber.sleep(0.0001)
        end
    end)
    fiber.sleep(0.001)
    count = count + 200
end;
test_run:cmd("setopt delimiter ''");

-- Deploy a replica.
test_run:cmd("create server replica with rpl_master=default, script='replication/replica.lua'")
test_run:cmd("start server replica")
test_run:cmd("switch replica")

fiber = require('fiber')

-- Stop replication.
replication = box.cfg.replication
box.cfg{replication = {}}

-- Fill the space.
test_run:cmd("switch default")
fill()
test_run:cmd("switch replica")

-- Resume replication.
--
-- Since max allowed lag is small, all recoreds should arrive
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
repeat fiber.sleep(0.01) until box.space.test:count() == 400

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
box.cfg{replication_sync_lag = 0.001, replication_sync_timeout = 0.001}
box.cfg{replication = replication}
box.space.test:count() < 600
box.info.status -- orphan
box.info.ro -- true

-- Wait for remaining rows to arrive.
repeat fiber.sleep(0.01) until box.space.test:count() == 600

-- Make sure replica leaves oprhan state.
repeat fiber.sleep(0.01) until box.info.status ~= 'orphan'
box.info.status -- running
box.info.ro -- false

test_run:cmd("switch default")
test_run:cmd("stop server replica")
test_run:cmd("cleanup server replica")

box.error.injection.set("ERRINJ_RELAY_TIMEOUT", 0)
box.space.test:drop()
box.schema.user.revoke('guest', 'replication')
