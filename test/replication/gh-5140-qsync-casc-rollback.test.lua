test_run = require('test_run').new()
engine = test_run:get_cfg('engine')
fiber = require('fiber')
--
-- gh-5140: qsync cascading rollback. Without cascading rollback it can happen
-- that a transaction is seemingly rolled back, but after restart it is
-- committed and visible. This is how it was possible:
--
--     * Master writes a sync transaction to WAL with LSN1;
--
--     * It starts waiting for ACKs;
--
--     * No ACKs for timeout - it starts writing to WAL the command
--       ROLLBACK(LSN1). To rollback everything with LSN >= LSN1 but < LSN of
--       the ROLLBACK record itself;
--
--     * Another fiber starts a new transaction, while ROLLBACK is in progress;
--
--     * Limbo is not empty, so the new transaction is added there. Then it
--       also starts writing itself to WAL;
--
--     * ROLLBACK finishes WAL write. It rolls back all the transactions in the
--       limbo to conform with the 'reversed rollback order' rule. Including
--       the latest transaction;
--
--     * The latest transaction finished its WAL write with LSN2 and sees that
--       it was rolled back by the limbo already.
--
-- All seems to be fine, but actually what happened is that ROLLBACK(LSN1) is
-- written to WAL *before* the latest transaction with LSN2. Now when restart
-- happens, ROLLBACK(LSN1) is replayed first, and then the latest LSN2
-- transaction is replayed second - it will be committed successfully, and will
-- be visible.
-- On the summary: transaction canceled its rollback after instance restart.
-- Expected behaviour is that while ROLLBACK is in progress, all newer
-- transactions should not even try going to WAL. They should be rolled back
-- immediately.
--
box.schema.user.grant('guest', 'super')

test_run:cmd('create server replica with rpl_master=default,			\
             script="replication/replica.lua"')
test_run:cmd('start server replica with wait=True, wait_load=True')

box.cfg{replication_synchro_quorum = 2, replication_synchro_timeout = 1000}

_ = box.schema.space.create('sync', {is_sync = true, engine = engine})
_ = _:create_index('pk')
_ = box.schema.space.create('async', {is_sync=false, engine = engine})
_ = _:create_index('pk')
box.ctl.promote(); box.ctl.wait_rw()
-- Write something to flush the master state to replica.
box.space.sync:replace{1}

box.cfg{replication_synchro_quorum = 3, replication_synchro_timeout = 0.001}
-- First WAL write will be fine. Second will be delayed. In this
-- test first is the transaction itself. Second is the ROLLBACK
-- record.
box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', 1)
ok, err = nil
f = fiber.create(function()                                                     \
    ok, err = pcall(box.space.sync.replace, box.space.sync, {2})                \
end)
while not box.error.injection.get("ERRINJ_WAL_DELAY") do fiber.sleep(0.001) end
-- ROLLBACK is in progress now. All newer transactions should be rolled back
-- immediately until the ROLLBACK record is written, and all the older
-- transactions are rolled back too. This is needed to preserve the 'reversed
-- rollback order' rule.
box.space.sync:replace{3}
box.space.async:replace{3}
box.error.injection.set("ERRINJ_WAL_DELAY", false)
test_run:wait_cond(function() return f:status() == 'dead' end)
ok, err

box.cfg{replication_synchro_quorum = 2, replication_synchro_timeout = 1000}
box.space.async:replace{4}
box.space.sync:replace{4}
box.space.async:select{}
box.space.sync:select{}

test_run:switch('replica')
box.space.async:select{}
box.space.sync:select{}

test_run:switch('default')
-- Key to reproduce the cascading rollback not done is to restart. On restart
-- all the records are replayed one be one without yields for WAL writes, and
-- nothing should change.
test_run:cmd('restart server default')
test_run:cmd('restart server replica')

test_run:switch('replica')
box.space.async:select{}
box.space.sync:select{}

test_run:switch('default')
box.space.async:select{}
box.space.sync:select{}

box.ctl.promote(); box.ctl.wait_rw()

box.space.sync:drop()
box.space.async:drop()

test_run:cmd('stop server replica')
test_run:cmd('delete server replica')

box.schema.user.revoke('guest', 'super')
box.ctl.demote()
