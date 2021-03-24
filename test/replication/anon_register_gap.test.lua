test_run = require('test_run').new()

--
-- When master's registering an anonymous replica, it might ignore the replica's
-- current vclock, and skip the data in range (replica_clock, master_clock).
--
box.schema.user.grant('guest', 'replication')
_ = box.schema.space.create('test')
_ = box.space.test:create_index('pk')

test_run:cmd('create server replica with rpl_master=default,\
              script="replication/anon1.lua"')
test_run:cmd('start server replica')

test_run:wait_lsn('replica', 'default')
box.error.injection.set('ERRINJ_RELAY_SEND_DELAY', true)

box.space.test:insert{1}

test_run:switch('replica')

test_run:wait_upstream(1, {status='disconnected'})
box.space.test:select{}

fiber = require('fiber')
f = fiber.new(function() box.cfg{replication_anon=false} end)
test_run:wait_upstream(1, {status='register'})

test_run:switch('default')
box.error.injection.set('ERRINJ_RELAY_SEND_DELAY', false)
box.space.test:insert{2}

test_run:switch('replica')
test_run:wait_lsn('replica', 'default')
f:status()
box.space.test:select{}

-- Cleanup
test_run:switch('default')
test_run:cmd('stop server replica')
test_run:cmd('delete server replica')
box.space.test:drop()
box.schema.user.revoke('guest', 'replication')
