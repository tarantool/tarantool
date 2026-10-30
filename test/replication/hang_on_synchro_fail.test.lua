test_run = require('test_run').new()
fiber = require('fiber')
--
-- All appliers could hang after failing to apply a synchronous message: either
-- CONFIRM or ROLLBACK.
--
box.schema.user.grant('guest', 'replication')

_ = box.schema.space.create('sync', {is_sync=true})
_ = box.space.sync:create_index('pk')
box.ctl.promote()

old_synchro_quorum = box.cfg.replication_synchro_quorum
box.cfg{replication_synchro_quorum=3}
-- A huge timeout so that we can perform some actions on a replica before
-- writing ROLLBACK.
old_synchro_timeout = box.cfg.replication_synchro_timeout
box.cfg{replication_synchro_timeout=1000}

test_run:cmd('create server replica with rpl_master=default,\
              script="replication/replica.lua"')
test_run:cmd('start server replica')

_ = fiber.new(box.space.sync.insert, box.space.sync, {1})
test_run:wait_lsn('replica', 'default')

test_run:switch('replica')

box.error.injection.set('ERRINJ_WAL_IO', true)

test_run:switch('default')

box.cfg{replication_synchro_timeout=0.01}

test_run:switch('replica')

test_run:wait_upstream(1, {status='stopped',\
                           message_re='Failed to write to disk'})
box.error.injection.set('ERRINJ_WAL_IO', false)

-- Applier is killed due to a failed WAL write, so restart replication to
-- check whether it hangs or not. Actually this single applier would fail an
-- assertion rather than hang, but all the other appliers, if any, would hang.
old_repl = box.cfg.replication
box.cfg{replication=""}
box.cfg{replication=old_repl}

test_run:wait_upstream(1, {status='follow'})

-- Cleanup.
test_run:switch('default')
test_run:cmd('stop server replica')
test_run:cmd('delete server replica')
box.cfg{replication_synchro_quorum=old_synchro_quorum,\
        replication_synchro_timeout=old_synchro_timeout}
box.space.sync:drop()
box.schema.user.revoke('guest', 'replication')
box.ctl.demote()

