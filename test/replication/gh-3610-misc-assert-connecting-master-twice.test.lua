test_run = require('test_run').new()
test_run:cmd("restart server default")
fiber = require('fiber')

--
-- Test case for gh-3610. Before the fix replica would fail with the assertion
-- when trying to connect to the same master twice.
--
box.schema.user.grant('guest', 'replication')
test_run:cmd("create server replica with rpl_master=default, script='replication/replica.lua'")
test_run:cmd("start server replica")
test_run:cmd("switch replica")
replication = box.cfg.replication[1]
box.cfg{replication = {replication, replication}}

-- Check the case when duplicate connection is detected in the background.
test_run:cmd("switch default")
listen = box.cfg.listen
box.cfg{listen = ''}

test_run:cmd("switch replica")
box.cfg{replication_connect_quorum = 0, replication_connect_timeout = 0.01}
box.cfg{replication = {replication, replication}}

test_run:cmd("switch default")
box.cfg{listen = listen}
while test_run:grep_log('replica', 'duplicate connection') == nil do fiber.sleep(0.01) end

test_run:cmd("stop server replica")
test_run:cmd("cleanup server replica")
test_run:cmd("delete server replica")
test_run:cleanup_cluster()
box.schema.user.revoke('guest', 'replication')
