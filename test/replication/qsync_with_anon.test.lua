env = require('test_run')
test_run = env.new()
engine = test_run:get_cfg('engine')

orig_synchro_quorum = box.cfg.replication_synchro_quorum
orig_synchro_timeout = box.cfg.replication_synchro_timeout

NUM_INSTANCES = 2
BROKEN_QUORUM = NUM_INSTANCES + 1

box.schema.user.grant('guest', 'replication')

-- Setup a cluster with anonymous replica.
test_run:cmd('create server replica_anon with rpl_master=default, script="replication/anon1.lua"')
test_run:cmd('start server replica_anon')
test_run:cmd('switch replica_anon')

-- [RFC, Asynchronous replication] successful transaction applied on async
-- replica.
-- Testcase setup.
test_run:switch('default')
box.cfg{replication_synchro_quorum=NUM_INSTANCES, replication_synchro_timeout=1000}
_ = box.schema.space.create('sync', {is_sync=true, engine=engine})
_ = box.space.sync:create_index('pk')
box.ctl.promote()
-- Testcase body.
test_run:switch('default')
box.space.sync:insert{1} -- success
box.space.sync:insert{2} -- success
box.space.sync:insert{3} -- success
test_run:cmd('switch replica_anon')
box.space.sync:select{} -- 1, 2, 3
-- Testcase cleanup.
test_run:switch('default')
box.space.sync:drop()

-- [RFC, Asynchronous replication] failed transaction rolled back on async
-- replica.
-- Testcase setup.
box.cfg{replication_synchro_quorum = NUM_INSTANCES, replication_synchro_timeout = 1000}
_ = box.schema.space.create('sync', {is_sync=true, engine=engine})
_ = box.space.sync:create_index('pk')
-- Write something to flush the current master's state to replica.
_ = box.space.sync:insert{1}
_ = box.space.sync:delete{1}

box.cfg{replication_synchro_quorum = BROKEN_QUORUM, replication_synchro_timeout = 1000}
fiber = require('fiber')
ok, err = nil
f = fiber.create(function()                                                     \
    ok, err = pcall(box.space.sync.insert, box.space.sync, {1})                 \
end)

test_run:cmd('switch replica_anon')
test_run:wait_cond(function() return box.space.sync:count() == 1 end)
box.space.sync:select{}

test_run:switch('default')
box.cfg{replication_synchro_timeout = 0.001}
test_run:wait_cond(function() return f:status() == 'dead' end)
box.space.sync:select{}

test_run:cmd('switch replica_anon')
test_run:wait_cond(function() return box.space.sync:count() == 0 end)
box.space.sync:select{}

test_run:switch('default')
box.cfg{replication_synchro_quorum=NUM_INSTANCES, replication_synchro_timeout=1000}
box.space.sync:insert{1} -- success
test_run:cmd('switch replica_anon')
box.space.sync:select{} -- 1
-- Testcase cleanup.
test_run:switch('default')
box.space.sync:drop()

-- Teardown.
test_run:switch('default')
test_run:cmd('stop server replica_anon')
test_run:cmd('delete server replica_anon')
box.schema.user.revoke('guest', 'replication')
box.cfg{                                                                        \
    replication_synchro_quorum = orig_synchro_quorum,                           \
    replication_synchro_timeout = orig_synchro_timeout,                         \
}
box.ctl.demote()
test_run:cleanup_cluster()
