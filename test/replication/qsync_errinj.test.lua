test_run = require('test_run').new()
engine = test_run:get_cfg('engine')

old_synchro_quorum = box.cfg.replication_synchro_quorum
old_synchro_timeout = box.cfg.replication_synchro_timeout
old_timeout = box.cfg.replication_timeout
box.schema.user.grant('guest', 'super')

test_run:cmd('create server replica with rpl_master=default,\
             script="replication/replica.lua"')
test_run:cmd('start server replica with wait=True, wait_load=True')

_ = box.schema.space.create('sync', {is_sync = true, engine = engine})
_ = box.space.sync:create_index('pk')
box.ctl.promote()

--
-- gh-5100: slow ACK sending shouldn't stun replica for the
-- replication timeout seconds.
--
test_run:cmd('switch default')
box.cfg{replication_timeout = 1000, replication_synchro_quorum = 2, replication_synchro_timeout = 1000}

test_run:switch('replica')
box.cfg{replication_timeout = 1000}
box.error.injection.set('ERRINJ_APPLIER_SLOW_ACK', true)

test_run:cmd('switch default')
for i = 1, 10 do box.space.sync:replace{i} end
box.space.sync:count()

test_run:switch('replica')
box.space.sync:count()
box.error.injection.set('ERRINJ_APPLIER_SLOW_ACK', false)

--
-- gh-5123: replica WAL fail shouldn't crash with quorum 1.
--
test_run:switch('default')
box.cfg{replication_synchro_quorum = 1, replication_synchro_timeout = 5}
box.space.sync:insert{11}

test_run:switch('replica')
box.error.injection.set('ERRINJ_WAL_IO', true)

test_run:switch('default')
box.space.sync:insert{12}

test_run:switch('replica')
test_run:wait_upstream(1, {status='stopped'})
box.error.injection.set('ERRINJ_WAL_IO', false)

test_run:cmd('restart server replica')
box.space.sync:select{12}

--
-- gh-5147: at local WAL write fail limbo entries should be
-- deleted from the end of the limbo, not from the beginning.
-- Otherwise it should crash.
--
test_run:switch('default')
fiber = require('fiber')
box.cfg{replication_synchro_quorum = 3, replication_synchro_timeout = 1000}
box.error.injection.set("ERRINJ_WAL_DELAY", true)
ok1, err1 = nil
f1 = fiber.create(function()                                                    \
    ok1, err1 = pcall(box.space.sync.replace, box.space.sync, {13})             \
end)
box.error.injection.set("ERRINJ_WAL_IO", true)
box.space.sync:replace({14})
box.error.injection.set("ERRINJ_WAL_IO", false)
box.error.injection.set("ERRINJ_WAL_DELAY", false)
box.cfg{replication_synchro_quorum = 2}
box.space.sync:replace({15})
test_run:wait_cond(function() return f1:status() == 'dead' end)
ok1, err1
box.space.sync:select{13}, box.space.sync:select{14}, box.space.sync:select{15}
test_run:switch('replica')
box.space.sync:select{13}, box.space.sync:select{14}, box.space.sync:select{15}

--
-- gh-5146: txn module should correctly handle errors from WAL
-- thread, not only TX thread errors.
--
test_run:switch('default')
box.cfg{replication_synchro_quorum = 3, replication_synchro_timeout = 1000}
box.space.sync:truncate()
-- Make a next row stuck in WAL thread.
box.error.injection.set("ERRINJ_WAL_DELAY", true)
ok1, err1 = nil
f1 = fiber.create(function()                                                    \
    ok1, err1 = pcall(box.space.sync.replace, box.space.sync, {1})              \
end)
-- When the suspended write will be set free, it will stumble into
-- an error.
box.error.injection.set("ERRINJ_WAL_ROTATE", true)
-- Set the suspected write free. TX thread will receive success
-- in terms of talking to WAL, but will get error inside the WAL
-- response.
box.error.injection.set("ERRINJ_WAL_DELAY", false)
test_run:wait_cond(function() return f1:status() == 'dead' end)
ok1, err1
box.error.injection.set("ERRINJ_WAL_ROTATE", false)
box.cfg{replication_synchro_quorum = 2}
box.space.sync:replace{2}
test_run:switch('replica')
box.space.sync:select{}

--
-- See if synchro timeout during CONFIRM WAL write won't try to rollback the
-- confirmed transaction.
--
test_run:switch('default')
box.space.sync:truncate()
-- Write something to flush the master's state to the replica.
_ = box.space.sync:insert({1})
_ = box.space.sync:delete({1})

test_run:switch('default')
-- Set a trap for CONFIRM write so as the current txn won't hang, but following
-- CONFIRM will.
box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', 1)
ok, err = nil
f = fiber.create(function()                                                     \
    ok, err = pcall(box.space.sync.replace, box.space.sync, {1})                \
end)

-- Wait when got ACK from replica and started writing CONFIRM.
test_run:wait_cond(function()                                                   \
    return box.error.injection.get("ERRINJ_WAL_DELAY")                          \
end)

-- Let the pending synchro transaction go. It should timeout, notice the
-- on-going confirmation, and go to sleep instead of doing rollback.
box.cfg{replication_synchro_timeout = 0.001}
fiber.yield()

-- Let the confirmation finish.
box.error.injection.set("ERRINJ_WAL_DELAY", false)

-- Now the transaction sees the CONFIRM is ok, and it is also finished.
test_run:wait_cond(function() return f:status() == 'dead' end)
ok, err
box.space.sync:select{}

test_run:switch('replica')
box.space.sync:select{}

--
-- See what happens when the quorum is collected during writing ROLLBACK.
-- CONFIRM for the same LSN should not be written.
--
test_run:switch('default')
box.cfg{replication_synchro_timeout = 1000, replication_synchro_quorum = 2}
box.space.sync:truncate()
-- Write something to flush the master's state to the replica.
_ = box.space.sync:insert({1})
_ = box.space.sync:delete({1})

test_run:switch('replica')
-- Block WAL write to block ACK sending.
box.error.injection.set("ERRINJ_WAL_DELAY", true)

test_run:switch('default')
-- Set a trap for ROLLBACK write so as the txn itself won't hang, but ROLLBACK
-- will.
box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', 1)
box.cfg{replication_synchro_timeout = 0.001}
lsn = box.info.lsn
ok, err = nil
f = fiber.create(function()                                                     \
    ok, err = pcall(box.space.sync.replace, box.space.sync, {1})                \
end)
-- Wait ROLLBACK WAL write start.
test_run:wait_cond(function()                                                   \
    return box.error.injection.get("ERRINJ_WAL_DELAY")                          \
end)
-- The transaction is written to WAL. ROLLBACK is not yet.
lsn = lsn + 1
assert(box.info.lsn == lsn)

test_run:switch('replica')
-- Let ACKs go. Master will receive ACK, but shouldn't try to CONFIRM. Because
-- ROLLBACK for the same LSN is in progress right now already.
box.error.injection.set("ERRINJ_WAL_DELAY", false)

test_run:switch('default')
-- Wait ACK receipt.
function wait_lsn_ack(id, lsn)                                                  \
    local this_id = box.info.id                                                 \
    test_run:wait_downstream(id, {status='follow'})                             \
    test_run:wait_cond(function()                                               \
        return box.info.replication[id].downstream.vclock[this_id] >= lsn       \
    end)                                                                        \
end
replica_id = test_run:get_server_id('replica')
wait_lsn_ack(replica_id, lsn)

-- See if parameters change will try to write CONFIRM.
box.cfg{replication_synchro_quorum = 1}
box.cfg{replication_synchro_quorum = 2}

-- Let ROLLBACK go and finish the test.
box.error.injection.set("ERRINJ_WAL_DELAY", false)
test_run:wait_cond(function() return f:status() == 'dead' end)
ok, err
box.cfg{replication_synchro_timeout = 1000}
box.space.sync:replace{2}
box.space.sync:select{}

test_run:switch('replica')
box.space.sync:select{}

test_run:cmd('switch default')

box.cfg{                                                                        \
    replication_synchro_quorum = old_synchro_quorum,                            \
    replication_synchro_timeout = old_synchro_timeout,                          \
    replication_timeout = old_timeout,                                          \
}
test_run:cmd('stop server replica')
test_run:cmd('delete server replica')

box.space.sync:drop()
box.schema.user.revoke('guest', 'super')
box.ctl.demote()
