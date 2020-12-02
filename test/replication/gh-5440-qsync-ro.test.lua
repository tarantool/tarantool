--
-- gh-5440 everyone but the limbo owner is read-only on non-empty limbo.
--
env = require('test_run')
test_run = env.new()
fiber = require('fiber')

box.schema.user.grant('guest', 'replication')
test_run:cmd('create server replica with rpl_master=default, script="replication/replica.lua"')
test_run:cmd('start server replica with wait=True, wait_load=True')

_ = box.schema.space.create('test', {is_sync=true})
_ = box.space.test:create_index('pk')

old_synchro_quorum = box.cfg.replication_synchro_quorum
old_synchro_timeout = box.cfg.replication_synchro_timeout

-- Make sure that the master stalls on commit leaving the limbo non-empty.
box.cfg{replication_synchro_quorum=3, replication_synchro_timeout=1000}

f = fiber.new(function() box.space.test:insert{1} end)
f:status()

-- Wait till replica's limbo is non-empty.
test_run:wait_lsn('replica', 'default')
test_run:cmd('switch replica')

box.info.ro
box.space.test:insert{2}
success = false
f = require('fiber').new(function() box.ctl.wait_rw() success = true end)
f:status()

test_run:cmd('switch default')

-- Empty the limbo.
box.cfg{replication_synchro_quorum=2}

test_run:cmd('switch replica')

test_run:wait_cond(function() return success end)
box.info.ro
-- Should succeed now.
box.space.test:insert{2}

-- Cleanup.
test_run:cmd('switch default')
box.cfg{replication_synchro_quorum=old_synchro_quorum,\
        replication_synchro_timeout=old_synchro_timeout}
box.space.test:drop()
test_run:cmd('stop server replica')
test_run:cmd('delete server replica')
box.schema.user.revoke('guest', 'replication')
