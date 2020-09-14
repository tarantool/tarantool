test_run = require('test_run').new()

--
-- gh-5287: when a cluster contained an anonymous replica during bootstrap, it
-- could be registered anyway.
--

test_run:cmd("create server replica1 with script='replication/autobootstrap_anon1.lua'")
test_run:cmd("start server replica1 with wait=False")

test_run:cmd("create server replica2 with script='replication/autobootstrap_anon2.lua'")
test_run:cmd("start server replica2 with args='true', wait=False")

test_run:switch('replica2')
-- Without box.info.replication test-run fails to wait a cond.
test_run:wait_cond(function() return next(box.info.replication) ~= nil end)
test_run:wait_upstream(1, {status = 'follow'})

test_run:switch('replica1')
-- The anonymous replica wasn't registered.
assert(box.space._cluster:len() == 1)
box.info.gc().consumers
box.info.replication_anon.count == 1

test_run:switch('default')

test_run:cmd("stop server replica1")
test_run:cmd("delete server replica1")
test_run:cmd("stop server replica2")
test_run:cmd("delete server replica2")
