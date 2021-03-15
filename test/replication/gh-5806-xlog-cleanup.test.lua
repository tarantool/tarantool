--
-- gh-5806: defer xlog cleanup to keep xlogs until
-- replicas present in "_cluster" are connected.
-- Otherwise we are getting XlogGapError since
-- master might go far forward from replica and
-- replica won't be able to connect without full
-- rebootstrap.
--

fiber = require('fiber')
test_run = require('test_run').new()
engine = test_run:get_cfg('engine')

--
-- Case 1.
--
-- First lets make sure we're getting XlogGapError in
-- case if wal_cleanup_delay is not used.
--

test_run:cmd('create server master with script="replication/gh-5806-master.lua"')
test_run:cmd('start server master')

test_run:switch('master')
box.schema.user.grant('guest', 'replication')

--
-- Keep small number of snaps to force cleanup
-- procedure be more intensive.
box.cfg{checkpoint_count = 1}

engine = test_run:get_cfg('engine')
s = box.schema.space.create('test', {engine = engine})
_ = s:create_index('pk')

test_run:switch('default')
test_run:cmd('create server replica with rpl_master=master,\
              script="replication/replica.lua"')
test_run:cmd('start server replica')

--
-- On replica we create an own space which allows us to
-- use more complex scenario and disables replica from
-- automatic rejoin (since replica can't do auto-rejoin if
-- there gonna be an own data loss). This allows us to
-- trigger XlogGapError in the log.
test_run:switch('replica')
box.cfg{checkpoint_count = 1}
s = box.schema.space.create('testreplica')
_ = s:create_index('pk')
box.space.testreplica:insert({1})
box.snapshot()

--
-- Stop the replica node and generate
-- xlogs on the master.
test_run:switch('master')
test_run:cmd('stop server replica')

box.space.test:insert({1})
box.snapshot()

--
-- We need to restart the master node since otherwise
-- the replica will be preventing us from removing old
-- xlog because it will be tracked by gc consumer which
-- kept in memory while master node is running.
--
-- Once restarted we write a new record into master's
-- space and run snapshot which removes old xlog required
-- by replica to subscribe leading to XlogGapError which
-- we need to test.
test_run:cmd('restart server master')
box.space.test:insert({2})
box.snapshot()
assert(not box.info.gc().is_paused)

--
-- Start replica and wait for error.
test_run:cmd('start server replica with wait=False, wait_load=False')

--
-- Wait error to appear, 60 seconds should be more than enough,
-- usually it happens in a couple of seconds.
test_run:switch('default')
test_run:wait_log('master', 'XlogGapError', nil, 60) ~= nil

--
-- Cleanup.
test_run:cmd('stop server master')
test_run:cmd('cleanup server master')
test_run:cmd('delete server master')
test_run:cmd('stop server replica')
test_run:cmd('cleanup server replica')
test_run:cmd('delete server replica')

--
-- Case 2.
--
-- Lets make sure we're not getting XlogGapError in
-- case if wal_cleanup_delay is used the code is almost
-- the same as for Case 1 except we don't disable cleanup
-- fiber but delay it up to a hour until replica is up
-- and running.
--

test_run:cmd('create server master with script="replication/gh-5806-master.lua"')
test_run:cmd('start server master with args="3600"')

test_run:switch('master')
box.schema.user.grant('guest', 'replication')

box.cfg{checkpoint_count = 1}

engine = test_run:get_cfg('engine')
s = box.schema.space.create('test', {engine = engine})
_ = s:create_index('pk')

test_run:switch('default')
test_run:cmd('create server replica with rpl_master=master,\
              script="replication/replica.lua"')
test_run:cmd('start server replica')

test_run:switch('replica')
box.cfg{checkpoint_count = 1}
s = box.schema.space.create('testreplica')
_ = s:create_index('pk')
box.space.testreplica:insert({1})
box.snapshot()

test_run:switch('master')
test_run:cmd('stop server replica')

box.space.test:insert({1})
box.snapshot()

test_run:cmd('restart server master with args="3600"')
box.space.test:insert({2})
box.snapshot()
assert(box.info.gc().is_paused)

test_run:cmd('start server replica')

--
-- Make sure no error happened.
test_run:switch('default')
assert(test_run:grep_log("master", "XlogGapError") == nil)

test_run:cmd('stop server master')
test_run:cmd('cleanup server master')
test_run:cmd('delete server master')
test_run:cmd('stop server replica')
test_run:cmd('cleanup server replica')
test_run:cmd('delete server replica')
--
--
-- Case 3: Fill _cluster with replica but then delete
-- the replica so that master's cleanup leave in "paused"
-- state, and then simply decrease the timeout to make
-- cleanup fiber work again.
--
test_run:cmd('create server master with script="replication/gh-5806-master.lua"')
test_run:cmd('start server master with args="3600"')

test_run:switch('master')
box.schema.user.grant('guest', 'replication')

test_run:switch('default')
test_run:cmd('create server replica with rpl_master=master,\
              script="replication/replica.lua"')
test_run:cmd('start server replica')

test_run:switch('master')
test_run:cmd('stop server replica')
test_run:cmd('cleanup server replica')
test_run:cmd('delete server replica')

test_run:cmd('restart server master with args="3600"')
assert(box.info.gc().is_paused)

test_run:switch('master')
box.cfg{wal_cleanup_delay = 0.01}
test_run:wait_cond(function() return not box.info.gc().is_paused end)

test_run:switch('default')
test_run:cmd('stop server master')
test_run:cmd('cleanup server master')
test_run:cmd('delete server master')

--
-- Case 4: Fill _cluster with replica but then delete
-- the replica so that master's cleanup leave in "paused"
-- state, and finally cleanup the _cluster to kick cleanup.
--
test_run:cmd('create server master with script="replication/gh-5806-master.lua"')
test_run:cmd('start server master')

test_run:switch('master')
box.schema.user.grant('guest', 'replication')

test_run:switch('default')
test_run:cmd('create server replica with rpl_master=master,\
              script="replication/replica.lua"')
test_run:cmd('start server replica')

test_run:switch('default')
master_uuid = test_run:eval('master', 'return box.info.uuid')[1]
replica_uuid = test_run:eval('replica', 'return box.info.uuid')[1]
master_cluster = test_run:eval('master', 'return box.space._cluster:select()')[1]
assert(master_cluster[1][2] == master_uuid)
assert(master_cluster[2][2] == replica_uuid)

test_run:cmd('stop server replica')
test_run:cmd('cleanup server replica')
test_run:cmd('delete server replica')

test_run:switch('master')
test_run:cmd('restart server master with args="3600"')
assert(box.info.gc().is_paused)

--
-- Drop the replica from _cluster and make sure
-- cleanup fiber is not paused anymore.
test_run:switch('default')
deleted_uuid = test_run:eval('master', 'return box.space._cluster:delete(2)')[1][2]
assert(replica_uuid == deleted_uuid)

test_run:switch('master')
test_run:wait_cond(function() return not box.info.gc().is_paused end)

test_run:switch('default')
test_run:cmd('stop server master')
test_run:cmd('cleanup server master')
test_run:cmd('delete server master')
