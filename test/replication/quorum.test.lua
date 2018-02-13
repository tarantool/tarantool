test_run = require('test_run').new()

SERVERS = {'quorum1', 'quorum2', 'quorum3'}

-- Deploy a cluster.
test_run:create_cluster(SERVERS)
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

test_run:cmd('restart server quorum2')
box.info.status -- orphan
box.ctl.wait_rw(0.001) -- timeout
box.info.ro -- true
box.space.test:replace{100} -- error
box.cfg{replication={}}
box.info.status -- running

test_run:cmd('restart server quorum2')
box.info.status -- orphan
box.ctl.wait_rw(0.001) -- timeout
box.info.ro -- true
box.space.test:replace{100} -- error
box.cfg{replication_connect_quorum = 2}
box.ctl.wait_rw()
box.info.ro -- false
box.info.status -- running

test_run:cmd('restart server quorum2')
box.info.status -- orphan
box.ctl.wait_rw(0.001) -- timeout
box.info.ro -- true
box.space.test:replace{100} -- error
test_run:cmd('start server quorum1')
box.ctl.wait_rw()
box.info.ro -- false
box.info.status -- running

-- Check that the replica follows all masters.
box.info.id == 1 or box.info.replication[1].upstream.status == 'follow'
box.info.id == 2 or box.info.replication[2].upstream.status == 'follow'
box.info.id == 3 or box.info.replication[3].upstream.status == 'follow'

-- Check that box.cfg() doesn't return until the instance
-- catches up with all configured replicas.
test_run:cmd('switch quorum3')
box.error.injection.set("ERRINJ_RELAY_TIMEOUT", 0.01)
test_run:cmd('switch quorum2')
box.error.injection.set("ERRINJ_RELAY_TIMEOUT", 0.01)
test_run:cmd('stop server quorum1')

for i = 1, 10 do box.space.test:insert{i} end
fiber = require('fiber')
fiber.sleep(0.1)

test_run:cmd('start server quorum1')
test_run:cmd('switch quorum1')
box.space.test:count() -- 10

-- Cleanup.
test_run:cmd('switch default')
test_run:drop_cluster(SERVERS)
