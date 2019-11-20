test_run = require('test_run').new()

SERVERS = {'quorum1', 'quorum2', 'quorum3'}

-- Deploy a cluster.
test_run:create_cluster(SERVERS, "replication", {args="0.1"})
test_run:wait_fullmesh(SERVERS)

-- Stop one replica and try to restart another one.
-- It should successfully restart, but stay in the
-- 'orphan' mode, which disables write accesses.
-- There are three ways for the replica to leave the
-- 'orphan' mode:
-- * reconfigure replication
-- * reset box.cfg.replication_connect_quorum
-- * wait until a quorum is formed asynchronously
test_run:cmd('stop server quorum1')

test_run:cmd('switch quorum2')

test_run:cmd('restart server quorum2 with args="0.1"')
box.info.status -- orphan
box.ctl.wait_rw(0.001) -- timeout
box.info.ro -- true
test_run:wait_cond(function() return box.space.test ~= nil end, 20)
box.space.test:replace{100} -- error
box.cfg{replication={}}
box.info.status -- running

test_run:cmd('restart server quorum2 with args="0.1"')
box.info.status -- orphan
box.ctl.wait_rw(0.001) -- timeout
box.info.ro -- true
box.space.test:replace{100} -- error
box.cfg{replication_connect_quorum = 2}
box.ctl.wait_rw()
box.info.ro -- false
box.info.status -- running

test_run:cmd('restart server quorum2 with args="0.1"')
box.info.status -- orphan
box.ctl.wait_rw(0.001) -- timeout
box.info.ro -- true
box.space.test:replace{100} -- error
test_run:cmd('start server quorum1 with args="0.1"')
box.ctl.wait_rw()
box.info.ro -- false
box.info.status -- running

-- Check that the replica follows all masters.
box.info.id == 1 or test_run:wait_cond(function() return box.info.replication[1].upstream.status == 'follow' end, 20)
box.info.id == 2 or test_run:wait_cond(function() return box.info.replication[2].upstream.status == 'follow' end, 20)
box.info.id == 3 or test_run:wait_cond(function() return box.info.replication[3].upstream.status == 'follow' end, 20)

-- Check that box.cfg() doesn't return until the instance
-- catches up with all configured replicas.
test_run:cmd('switch quorum3')
box.error.injection.set("ERRINJ_RELAY_TIMEOUT", 0.001)
test_run:cmd('switch quorum2')
box.error.injection.set("ERRINJ_RELAY_TIMEOUT", 0.001)
test_run:cmd('stop server quorum1')

test_run:wait_cond(function() return box.space.test.index.primary ~= nil end, 20)
for i = 1, 100 do box.space.test:insert{i} end
fiber = require('fiber')
fiber.sleep(0.1)

test_run:cmd('start server quorum1 with args="0.1"')
test_run:cmd('switch quorum1')
test_run:wait_cond(function() return box.space.test:count() == 100 end, 20)

-- Rebootstrap one node of the cluster and check that others follow.
-- Note, due to ERRINJ_RELAY_TIMEOUT there is a substantial delay
-- between the moment the node starts listening and the moment it
-- completes bootstrap and subscribes. Other nodes will try and
-- fail to subscribe to the restarted node during this period.
-- This is OK - they have to retry until the bootstrap is complete.
test_run:cmd('switch quorum3')
box.snapshot()
test_run:cmd('switch quorum2')
box.snapshot()

test_run:cmd('switch quorum1')
test_run:cmd('restart server quorum1 with cleanup=1, args="0.1"')

test_run:wait_cond(function() return box.space.test:count() == 100 end, 20)

-- The rebootstrapped replica will be assigned id = 4,
-- because ids 1..3 are busy.
test_run:cmd('switch quorum2')
test_run:wait_cond(function() return box.info.replication[4].upstream.status == 'follow' end, 20)
box.info.replication[4].upstream.status
test_run:cmd('switch quorum3')
test_run:wait_cond(function() return box.info.replication ~= nil end, 20)
test_run:wait_cond(function() return box.info.replication[4].upstream.status == 'follow' end, 20)
box.info.replication[4].upstream.status

-- Cleanup.
test_run:cmd('switch default')
test_run:drop_cluster(SERVERS)

--
-- gh-3278: test different replication and replication_connect_quorum configs.
--

box.schema.user.grant('guest', 'replication')
space = box.schema.space.create('test', {engine = test_run:get_cfg('engine')});
index = box.space.test:create_index('primary')
-- Insert something just to check that replica with quorum = 0 works as expected.
space:insert{1}
test_run:cmd("create server replica with rpl_master=default, script='replication/replica_no_quorum.lua'")
test_run:cmd("start server replica")
test_run:cmd("switch replica")
box.info.status -- running
box.space.test:select()
test_run:cmd("switch default")
test_run:cmd("stop server replica")
listen = box.cfg.listen
box.cfg{listen = ''}
test_run:cmd("start server replica")
test_run:cmd("switch replica")
box.info.status -- running
test_run:cmd("switch default")
-- Check that replica is able to reconnect, case was broken with earlier quorum "fix".
box.cfg{listen = listen}
space:insert{2}
vclock = test_run:get_vclock("default")
_ = test_run:wait_vclock("replica", vclock)
test_run:cmd("switch replica")
box.info.status -- running
box.space.test:select()
test_run:cmd("switch default")
test_run:cmd("stop server replica")
test_run:cmd("cleanup server replica")
space:drop()
box.schema.user.revoke('guest', 'replication')
-- Second case, check that master-master works.
SERVERS = {'master_quorum1', 'master_quorum2'}
-- Deploy a cluster.
test_run:create_cluster(SERVERS, "replication", {args="0.1"})
test_run:wait_fullmesh(SERVERS)
test_run:cmd("switch master_quorum1")
repl = box.cfg.replication
box.cfg{replication = ""}
box.space.test:insert{1}
box.cfg{replication = repl}
vclock = test_run:get_vclock("master_quorum1")
_ = test_run:wait_vclock("master_quorum2", vclock)
test_run:cmd("switch master_quorum2")
box.space.test:select()
test_run:cmd("switch default")
-- Cleanup.
test_run:drop_cluster(SERVERS)

-- Test that quorum is not ignored neither during bootstrap, nor
-- during reconfiguration.
box.schema.user.grant('guest', 'replication')
test_run:cmd('create server replica_quorum with script="replication/replica_quorum.lua"')
-- Arguments are: replication_connect_quorum, replication_timeout
-- If replication_connect_quorum was ignored here, the instance
-- would exit with an error.
test_run:cmd('start server replica_quorum with wait=True, wait_load=True, args="1 0.05"')
test_run:cmd('switch replica_quorum')
-- If replication_connect_quorum was ignored here, the instance
-- would exit with an error.
box.cfg{replication={INSTANCE_URI, nonexistent_uri(1)}}
box.info.id
test_run:cmd('switch default')
test_run:cmd('stop server replica_quorum')
test_run:cmd('cleanup server replica_quorum')
test_run:cmd('delete server replica_quorum')
test_run:cleanup_cluster()
box.schema.user.revoke('guest', 'replication')
