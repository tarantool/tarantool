env = require('test_run')
test_run = env.new()

SERVERS = { 'autobootstrap1', 'autobootstrap2', 'autobootstrap3' }
-- Start servers
test_run:create_cluster(SERVERS)
-- Wait for full mesh
test_run:wait_fullmesh(SERVERS)

test_run:cmd("switch autobootstrap1")
for i = 0, 9 do box.space.test:insert{i, 'test' .. i} end
box.space.test:count()

test_run:cmd('switch default')
vclock1 = test_run:get_vclock('autobootstrap1')
vclock2 = test_run:wait_cluster_vclock(SERVERS, vclock1)

test_run:cmd("switch autobootstrap2")
box.space.test:count()
box.error.injection.set("ERRINJ_RELAY_TIMEOUT", 0.01)
test_run:cmd("stop server autobootstrap1")
fio = require('fio')
-- This test checks ability to recover missing local data
-- from remote replica. See #3210.
-- Delete data on first master and test that after restart,
-- due to difference in vclock it will be able to recover
-- all missing data from replica.
-- Also check that there is no concurrency, i.e. master is
-- in 'read-only' mode unless it receives all data.
fio.unlink(fio.pathjoin(fio.abspath("."), string.format('autobootstrap1/%020d.xlog', 8)))
test_run:cmd("start server autobootstrap1")

test_run:cmd("switch autobootstrap1")
for i = 10, 19 do box.space.test:insert{i, 'test' .. i} end
fiber = require('fiber')
box.space.test:select()

-- Cleanup.
test_run:cmd('switch default')
test_run:drop_cluster(SERVERS)
