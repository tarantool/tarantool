test_run = require('test_run').new()

SERVERS = { 'recover_missing_xlog1', 'recover_missing_xlog2', 'recover_missing_xlog3' }
test_run:init_cluster(SERVERS, "replication", {args="0.1"})

test_run:cmd("switch recover_missing_xlog1")
for i = 0, 9 do box.space.test:insert{i, 'test' .. i} end
box.space.test:count()

test_run:cmd('switch default')
vclock1 = test_run:get_vclock('recover_missing_xlog1')
vclock2 = test_run:wait_cluster_vclock(SERVERS, vclock1)

test_run:cmd("switch recover_missing_xlog2")
box.space.test:count()
box.error.injection.set("ERRINJ_RELAY_TIMEOUT", 0.01)
test_run:cmd("stop server recover_missing_xlog1")
fio = require('fio')
-- This test checks ability to recover missing local data
-- from remote replica. See #3210.
-- Delete data on first master and test that after restart,
-- due to difference in vclock it will be able to recover
-- all missing data from replica.
-- Also check that there is no concurrency, i.e. master is
-- in 'read-only' mode unless it receives all data.
list = fio.glob(fio.pathjoin(fio.abspath("."), 'recover_missing_xlog1/*.xlog'))
fio.unlink(list[#list])
test_run:cmd('start server recover_missing_xlog1 with args="0.1 0.5"')

test_run:cmd("switch recover_missing_xlog1")
for i = 10, 19 do box.space.test:insert{i, 'test' .. i} end
fiber = require('fiber')
box.space.test:select()

-- Cleanup.
test_run:cmd('switch default')
test_run:drop_cluster(SERVERS)
