#!/usr/bin/env tarantool
--
-- vim: ts=4 sw=4 et
--

test_run = require('test_run').new()

--
-- Allow replica to connect to us
box.schema.user.grant('guest', 'replication')

--
-- Create replica instance, we're the master and
-- start it, no data to sync yet though
test_run:cmd("create server replica_slave with rpl_master=default, script='replication/replica.lua'")
test_run:cmd("start server replica_slave")

--
-- Fill initial data on the master instance
test_run:cmd('switch default')

_ = box.schema.space.create('test')
s = box.space.test

s:format({{name = 'id', type = 'unsigned'}, {name = 'band_name', type = 'string'}})

_ = s:create_index('primary', {type = 'tree', parts = {'id'}})
s:insert({1, '1'})
s:insert({2, '2'})
s:insert({3, '3'})

--
-- Wait for data from master get propagated
test_run:wait_lsn('replica_slave', 'default')

--
-- Now inject error into slave instance
test_run:cmd('switch replica_slave')

--
-- To make sure we're running
box.info.status

--
-- To fail inserting new record.
errinj = box.error.injection
errinj.set('ERRINJ_TXN_COMMIT_ASYNC', true)

--
-- Jump back to master node and write new
-- entry which should cause error to happen
-- on slave instance
test_run:cmd('switch default')
s:insert({4, '4'})

--
-- Wait for error to trigger
test_run:cmd('switch replica_slave')
fiber = require('fiber')
while test_run:grep_log('replica_slave', 'INJECTION') == nil do fiber.sleep(0.1) end

----
---- Such error cause the applier to be
---- cancelled and reaped, thus stop the
---- slave node and cleanup
test_run:cmd('switch default')

--
-- Cleanup
test_run:cmd("stop server replica_slave")
test_run:cmd("delete server replica_slave")
box.space.test:drop()
box.schema.user.revoke('guest', 'replication')
