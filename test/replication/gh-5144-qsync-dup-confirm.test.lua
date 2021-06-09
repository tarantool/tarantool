test_run = require('test_run').new()
engine = test_run:get_cfg('engine')
fiber = require('fiber')
--
-- gh-5144: qsync should not write duplicate confirms for the same LSN. That
-- could happen when first CONFIRM write is in progress, and more ACKs arrive.
--
box.schema.user.grant('guest', 'super')

test_run:cmd('create server replica1 with rpl_master=default,                   \
             script="replication/replica1.lua"')
test_run:cmd('start server replica1 with wait=True, wait_load=True')

test_run:cmd('create server replica2 with rpl_master=default,                   \
             script="replication/replica2.lua"')
test_run:cmd('start server replica2 with wait=True, wait_load=True')

box.cfg{replication_synchro_quorum = 2, replication_synchro_timeout = 1000}

_ = box.schema.space.create('sync', {is_sync = true, engine = engine})
_ = _:create_index('pk')
box.ctl.promote()

-- Remember the current LSN. In the end, when the following synchronous
-- transaction is committed, result LSN should be this value +2: for the
-- transaction itself and for CONFIRM.
lsn_before_txn = box.info.lsn

box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', 1)
ok, err = nil
f = fiber.create(function()                                                     \
    ok, err = pcall(box.space.sync.insert, box.space.sync, {1})                 \
end)
-- First replica's ACK is received. Quorum 2 is achieved. CONFIRM write is in
-- progress.
while not box.error.injection.get("ERRINJ_WAL_DELAY") do fiber.sleep(0.001) end

-- Wait when both replicas confirm receipt of the sync transaction. It would
-- mean the master knows both LSNs and either already tried to write CONFIRM
-- twice, or didn't and will not.
function wait_lsn_ack(id, lsn)                                                  \
    local this_id = box.info.id                                                 \
    test_run:wait_downstream(id, {status='follow'})                             \
    test_run:wait_cond(function()                                               \
        return box.info.replication[id].downstream.vclock[this_id] >= lsn       \
    end)                                                                        \
end
replica1_id = test_run:get_server_id('replica1')
replica2_id = test_run:get_server_id('replica2')
lsn = box.info.lsn
wait_lsn_ack(replica1_id, lsn)
wait_lsn_ack(replica2_id, lsn)

-- Decrease already achieved quorum to check that it won't generate the same
-- CONFIRM on the parameter update.
box.cfg{replication_synchro_quorum = 1}

-- Let the transaction finish. CONFIRM should be in WAL now.
box.error.injection.set("ERRINJ_WAL_DELAY", false)
test_run:wait_cond(function() return f:status() == 'dead' end)
ok, err

-- First +1 is the transaction itself. Second +1 is CONFIRM.
assert(box.info.lsn - lsn_before_txn == 2)

box.space.sync:drop()

test_run:cmd('stop server replica1')
test_run:cmd('delete server replica1')
test_run:cmd('stop server replica2')
test_run:cmd('delete server replica2')

box.ctl.demote()
box.schema.user.revoke('guest', 'super')
