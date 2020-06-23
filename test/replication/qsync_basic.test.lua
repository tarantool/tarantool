--
-- gh-4282: synchronous replication. It allows to make certain
-- spaces commit only when their changes are replicated to a
-- quorum of replicas.
--
s1 = box.schema.create_space('test1', {is_sync = true})
s1.is_sync
pk = s1:create_index('pk')
box.begin() s1:insert({1}) s1:insert({2}) box.commit()
s1:select{}

-- Default is async.
s2 = box.schema.create_space('test2')
s2.is_sync

-- Net.box takes sync into account.
box.schema.user.grant('guest', 'super')
netbox = require('net.box')
c = netbox.connect(box.cfg.listen)
c.space.test1.is_sync
c.space.test2.is_sync
c:close()
box.schema.user.revoke('guest', 'super')

s1:drop()
s2:drop()

-- Local space can't be synchronous.
box.schema.create_space('test', {is_sync = true, is_local = true})

--
-- gh-4847, gh-4848: CONFIRM and ROLLBACK entries in WAL.
--
env = require('test_run')
test_run = env.new()
fiber = require('fiber')
engine = test_run:get_cfg('engine')

box.schema.user.grant('guest', 'replication')
-- Set up synchronous replication options.
quorum = box.cfg.replication_synchro_quorum
timeout = box.cfg.replication_synchro_timeout
box.cfg{replication_synchro_quorum=2, replication_synchro_timeout=0.1}

test_run:cmd('create server replica with rpl_master=default,\
                                         script="replication/replica.lua"')
test_run:cmd('start server replica with wait=True, wait_load=True')

_ = box.schema.space.create('sync', {is_sync=true, engine=engine})
_ = box.space.sync:create_index('pk')

lsn = box.info.lsn
box.space.sync:insert{1}
-- 1 for insertion, 1 for CONFIRM message.
box.info.lsn - lsn
-- Raise quorum so that master has to issue a ROLLBACK.
box.cfg{replication_synchro_quorum=3}
t = fiber.time()
box.space.sync:insert{2}
-- Check that master waited for acks.
fiber.time() - t > box.cfg.replication_synchro_timeout
box.cfg{replication_synchro_quorum=2}
box.space.sync:insert{3}
box.space.sync:select{}

-- Check consistency on replica.
test_run:cmd('switch replica')
box.space.sync:select{}

-- Check consistency in recovered data.
test_run:cmd('restart server replica')
box.space.sync:select{}

-- Cleanup.
test_run:cmd('switch default')

box.cfg{replication_synchro_quorum=quorum, replication_synchro_timeout=timeout}
test_run:cmd('stop server replica')
test_run:cmd('delete server replica')
box.space.sync:drop()
box.schema.user.revoke('guest', 'replication')
