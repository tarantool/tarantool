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
box.cfg{replication_synchro_quorum = 2, replication_synchro_timeout = 1000}

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
box.cfg{replication_synchro_quorum = 3, replication_synchro_timeout = 0.001}
t = fiber.time()
box.space.sync:insert{2}
-- Check that master waited for acks.
fiber.time() - t > box.cfg.replication_synchro_timeout
box.cfg{replication_synchro_quorum = 2, replication_synchro_timeout = 1000}
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

--
-- Fully local async transaction also waits for existing sync txn.
--
test_run:switch('default')
box.cfg{replication_synchro_timeout = 1000, replication_synchro_quorum = 2}
_ = box.schema.create_space('locallocal', {is_local = true, engine = engine})
_ = _:create_index('pk')
-- Propagate local vclock to some insane value to ensure it won't
-- affect anything.
box.begin() for i = 1, 500 do box.space.locallocal:replace{1} end box.commit()
do                                                                              \
    f1 = fiber.create(box.space.sync.replace, box.space.sync, {8})              \
    f2 = fiber.create(box.space.locallocal.replace, box.space.locallocal, {8})  \
    box.space.test:replace{8}                                                   \
end
f1:status()
f2:status()
box.space.sync:select{8}
box.space.locallocal:select{8}
box.space.test:select{8}

test_run:switch('replica')
box.space.sync:select{8}
box.space.locallocal:select{8}
box.space.test:select{8}

-- Ensure sync rollback will affect all pending fully local async
-- transactions too.
test_run:switch('default')
box.cfg{replication_synchro_timeout = 0.001, replication_synchro_quorum = 3}
do                                                                              \
    f1 = fiber.create(box.space.sync.replace, box.space.sync, {9})              \
    f2 = fiber.create(box.space.locallocal.replace, box.space.locallocal, {9})  \
    box.space.test:replace{9}                                                   \
end
test_run:wait_cond(function() return f1:status() == 'dead' end)
test_run:wait_cond(function() return f2:status() == 'dead' end)
box.space.sync:select{9}
box.space.locallocal:select{9}
box.space.test:select{9}
test_run:switch('replica')
box.space.sync:select{9}
box.space.locallocal:select{9}
box.space.test:select{9}

--
-- gh-4928: test that a sync transaction works fine with local
-- rows in the end.
--
test_run:switch('default')
box.cfg{replication_synchro_timeout = 1000, replication_synchro_quorum = 2}
-- Propagate local vclock to some insane value to ensure it won't
-- affect anything.
box.begin() for i = 1, 500 do box.space.locallocal:replace{1} end box.commit()
do                                                                              \
    box.begin()                                                                 \
    box.space.sync:replace{10}                                                  \
    box.space.locallocal:replace({10})                                          \
    box.commit()                                                                \
end
box.space.sync:select{10}
box.space.locallocal:select{10}

test_run:switch('replica')
box.space.sync:select{10}
box.space.locallocal:select{10}

--
-- gh-5123: quorum 1 still should write CONFIRM.
--
test_run:switch('default')
box.cfg{replication_synchro_quorum = 1, replication_synchro_timeout = 5}
oldlsn = box.info.lsn
box.space.sync:replace{7}
newlsn = box.info.lsn
assert(newlsn >= oldlsn + 2)
test_run:switch('replica')
box.space.sync:select{7}

--
-- gh-5119: dynamic limbo configuration. Updated parameters should
-- be applied even to existing transactions.
--
test_run:switch('default')
box.cfg{replication_synchro_quorum = 3, replication_synchro_timeout = 1000}
ok, err = nil
f = fiber.create(function()                                                     \
    ok, err = pcall(box.space.sync.insert, box.space.sync, {11})                \
end)
f:status()
box.cfg{replication_synchro_timeout = 0.001}
test_run:wait_cond(function() return f:status() == 'dead' end)
ok, err
box.space.sync:select{11}
test_run:switch('replica')
box.space.sync:select{11}

-- Test it is possible to early ACK a transaction with a new quorum.
test_run:switch('default')
box.cfg{replication_synchro_timeout = 1000}
ok, err = nil
f = fiber.create(function()                                                     \
    ok, err = pcall(box.space.sync.insert, box.space.sync, {12})                \
end)
f:status()
box.cfg{replication_synchro_quorum = 2}
test_run:wait_cond(function() return f:status() == 'dead' end)
ok, err
box.space.sync:select{12}
test_run:switch('replica')
box.space.sync:select{12}

--
-- gh-5138: synchro rows were not saved onto txns region, and
-- could get corrupted under load.
--
test_run:switch('default')
box.cfg{replication_synchro_timeout = 1000}
for i = 1, 100 do box.space.sync:replace{i} end
test_run:cmd('switch replica')
box.space.sync:count()
-- Rows could be corrupted during WAL writes. Restart should
-- reveal the problem during recovery.
test_run:cmd('restart server replica')
box.space.sync:count()
test_run:cmd('switch default')
for i = 1, 100 do box.space.sync:delete{i} end
test_run:cmd('switch replica')
box.space.sync:count()

--
-- gh-5445: NOPs bypass the limbo for the sake of vclock bumps from foreign
-- instances, but also works for local rows.
--
test_run:switch('default')
box.cfg{replication_synchro_quorum = 3, replication_synchro_timeout = 1000}
f = fiber.create(function() box.space.sync:replace{1} end)
test_run:wait_lsn('replica', 'default')

test_run:switch('replica')
function skip_row() return nil end
old_lsn = box.info.lsn
_ = box.space.sync:before_replace(skip_row)
box.space.sync:replace{2}
box.space.sync:before_replace(nil, skip_row)
assert(box.space.sync:get{2} == nil)
assert(box.space.sync:get{1} ~= nil)

test_run:switch('default')
box.cfg{replication_synchro_quorum = 2}
test_run:wait_cond(function() return f:status() == 'dead' end)
box.space.sync:truncate()

--
-- gh-5191: test box.info.synchro interface. For
-- this sake we stop the replica and initiate data
-- write in sync space which won't pass due to timeout.
-- While we're sitting in a wait cycle the queue should
-- not be empty.
--
-- Make sure this test is the *LAST* one since we stop
-- the replica node and never restart it back before the
-- cleanup procedure, also we're spinning on default node
-- and do not switch to other nodes.
--
test_run:cmd('switch default')
test_run:cmd('stop server replica')
assert(box.info.synchro.queue.len == 0)
box.cfg{replication_synchro_timeout = 2}
f = fiber.new(function() box.space.sync:insert{1024} end)
test_run:wait_cond(function() return box.info.synchro.queue.len == 1 end)
test_run:wait_cond(function() return box.info.synchro.queue.len == 0 end)

-- Cleanup
box.cfg{                                                                        \
    replication_synchro_quorum = old_synchro_quorum,                            \
    replication_synchro_timeout = old_synchro_timeout,                          \
    replication_timeout = old_timeout,                                          \
}
test_run:cmd('delete server replica')
box.space.test:drop()
box.space.sync:drop()
box.schema.user.revoke('guest', 'replication')
