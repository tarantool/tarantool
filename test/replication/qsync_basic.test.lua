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
old_synchro_quorum = box.cfg.replication_synchro_quorum
old_synchro_timeout = box.cfg.replication_synchro_timeout
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

--
-- gh-5100: replica should send ACKs for sync transactions after
-- WAL write immediately, not waiting for replication timeout or
-- a CONFIRM.
--
box.cfg{replication_timeout = 1000, replication_synchro_timeout = 1000}
test_run:switch('default')
old_timeout = box.cfg.replication_timeout
box.cfg{replication_timeout = 1000, replication_synchro_timeout = 1000}
-- Commit something non-sync. So as applier writer fiber would
-- flush the pending heartbeat and go to sleep with the new huge
-- replication timeout.
s = box.schema.create_space('test', {engine = engine})
pk = s:create_index('pk')
s:replace{1}
-- Now commit something sync. It should return immediately even
-- though the replication timeout is huge.
box.space.sync:replace{4}
test_run:switch('replica')
box.space.sync:select{4}

--
-- Async transactions should wait for existing sync transactions
-- finish.
--
test_run:switch('default')
-- Start 2 fibers, which will execute one right after the other
-- in the same event loop iteration.
f = fiber.create(box.space.sync.replace, box.space.sync, {5}) s:replace{5}
f:status()
s:select{5}
box.space.sync:select{5}
test_run:switch('replica')
box.space.test:select{5}
box.space.sync:select{5}
-- Ensure sync rollback will affect all pending async transactions
-- too.
test_run:switch('default')
box.cfg{replication_synchro_timeout = 0.001, replication_synchro_quorum = 3}
f = fiber.create(box.space.sync.replace, box.space.sync, {6}) s:replace{6}
f:status()
s:select{6}
box.space.sync:select{6}
test_run:switch('replica')
box.space.test:select{6}
box.space.sync:select{6}

-- Cleanup.
test_run:cmd('switch default')

box.cfg{                                                                        \
    replication_synchro_quorum = old_synchro_quorum,                            \
    replication_synchro_timeout = old_synchro_timeout,                          \
    replication_timeout = old_timeout,                                          \
}
test_run:cmd('stop server replica')
test_run:cmd('delete server replica')
box.space.test:drop()
box.space.sync:drop()
box.schema.user.revoke('guest', 'replication')
