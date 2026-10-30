--
-- gh-5447: Test for box.info.replication[n].downstream.lag.
-- We need to be sure that slow ACKs delivery might be
-- caught by monitoring tools.
--

fiber = require('fiber')
test_run = require('test_run').new()
engine = test_run:get_cfg('engine')

box.schema.user.grant('guest', 'replication')

test_run:cmd('create server replica with rpl_master=default, \
             script="replication/replica.lua"')
test_run:cmd('start server replica')

replica_id = test_run:get_server_id('replica')

--
-- Upon replica startup there is no ACKs to process.
assert(box.info.replication[replica_id].downstream.lag == 0)

s = box.schema.space.create('test', {engine = engine})
_ = s:create_index('pk')

--
-- The replica should wait some time before writing data
-- to the WAL, otherwise we might not even notice the lag
-- if media is too fast. Before disabling WAL we need to
-- wait the space get propagated.
test_run:switch('replica')
test_run:wait_lsn('replica', 'default')
box.error.injection.set("ERRINJ_WAL_DELAY", true)

--
-- Insert a record and wakeup replica's WAL to process data.
test_run:switch('default')
box.space.test:insert({1})
-- The record is written on the master node.
test_run:switch('replica')
box.error.injection.set("ERRINJ_WAL_DELAY", false)

--
-- Wait the record to be ACKed, the lag value should be nonzero.
test_run:switch('default')
test_run:wait_lsn('replica', 'default')
assert(box.info.replication[replica_id].downstream.lag > 0)

--
-- Cleanup everything.
test_run:switch('default')
box.space.test:drop()
box.schema.user.revoke('guest', 'replication')
test_run:cmd('stop server replica')
test_run:cmd('cleanup server replica')
test_run:cmd('delete server replica')
