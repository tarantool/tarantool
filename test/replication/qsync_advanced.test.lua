env = require('test_run')
test_run = env.new()
engine = test_run:get_cfg('engine')
fiber = require('fiber')

orig_synchro_quorum = box.cfg.replication_synchro_quorum
orig_synchro_timeout = box.cfg.replication_synchro_timeout

NUM_INSTANCES = 2
BROKEN_QUORUM = NUM_INSTANCES + 1

test_run:cmd("setopt delimiter ';'")
disable_sync_mode = function()
    local s = box.space._space:get(box.space.sync.id)
    local new_s = s:update({{'=', 6, {is_sync=false}}})
    box.space._space:replace(new_s)
end;
test_run:cmd("setopt delimiter ''");

box.schema.user.grant('guest', 'replication')

-- Setup an async cluster with two instances.
test_run:cmd('create server replica with rpl_master=default,\
                                         script="replication/replica.lua"')
test_run:cmd('start server replica with wait=True, wait_load=True')

-- Successful write.
-- Testcase setup.
test_run:switch('default')
box.cfg{replication_synchro_quorum=NUM_INSTANCES, replication_synchro_timeout=1000}
_ = box.schema.space.create('sync', {is_sync=true, engine=engine})
_ = box.space.sync:create_index('pk')
box.ctl.promote()
-- Testcase body.
box.space.sync:insert{1} -- success
test_run:cmd('switch replica')
box.space.sync:select{} -- 1
-- Testcase cleanup.
test_run:switch('default')
box.space.sync:drop()

-- Unsuccessfull write.
-- Testcase setup.
test_run:switch('default')
box.cfg{replication_synchro_quorum=BROKEN_QUORUM, replication_synchro_timeout=0.001}
_ = box.schema.space.create('sync', {is_sync=true, engine=engine})
_ = box.space.sync:create_index('pk')
-- Testcase body.
box.space.sync:insert{1}
test_run:switch('replica')
box.space.sync:select{} -- none
-- Testcase cleanup.
test_run:switch('default')
box.space.sync:drop()

-- [RFC, quorum commit] attempt to write multiple transactions, expected the
-- same order as on client in case of achieved quorum.
-- Testcase setup.
test_run:switch('default')
box.cfg{replication_synchro_quorum=NUM_INSTANCES, replication_synchro_timeout=1000}
_ = box.schema.space.create('sync', {is_sync=true, engine=engine})
_ = box.space.sync:create_index('pk')
-- Testcase body.
box.space.sync:insert{1}
box.space.sync:insert{2}
box.space.sync:insert{3}
box.space.sync:select{} -- 1, 2, 3
test_run:switch('replica')
box.space.sync:select{} -- 1, 2, 3
-- Testcase cleanup.
test_run:switch('default')
box.space.sync:drop()

-- Synchro timeout is not bigger than replication_synchro_timeout value.
-- Testcase setup.
test_run:switch('default')
box.cfg{replication_synchro_quorum=BROKEN_QUORUM, replication_synchro_timeout=0.001}
_ = box.schema.space.create('sync', {is_sync=true, engine=engine})
_ = box.space.sync:create_index('pk')
-- Testcase body.
start = fiber.clock()
box.space.sync:insert{1}
duration = fiber.clock() - start
duration >= box.cfg.replication_synchro_timeout or duration -- true
-- Testcase cleanup.
test_run:switch('default')
box.space.sync:drop()

-- replication_synchro_quorum
test_run:switch('default')
INT_MIN = -2147483648
INT_MAX = 2147483648
box.cfg{replication_synchro_quorum=INT_MAX} -- error
box.cfg.replication_synchro_quorum -- old value
box.cfg{replication_synchro_quorum=INT_MIN} -- error
box.cfg.replication_synchro_quorum -- old value

-- replication_synchro_timeout
test_run:switch('default')
DOUBLE_MAX = 9007199254740992
box.cfg{replication_synchro_timeout=DOUBLE_MAX}
box.cfg.replication_synchro_timeout -- DOUBLE_MAX
box.cfg{replication_synchro_timeout=DOUBLE_MAX+1}
box.cfg.replication_synchro_timeout -- DOUBLE_MAX
box.cfg{replication_synchro_timeout=-1} -- error
box.cfg.replication_synchro_timeout -- old value
box.cfg{replication_synchro_timeout=0} -- error
box.cfg.replication_synchro_timeout -- old value

-- TX is in synchronous replication.
-- Testcase setup.
test_run:switch('default')
box.cfg{replication_synchro_quorum=NUM_INSTANCES, replication_synchro_timeout=1000}
_ = box.schema.space.create('sync', {is_sync=true, engine=engine})
_ = box.space.sync:create_index('pk')
-- Testcase body.
box.begin() box.space.sync:insert({1}) box.commit()
box.begin() box.space.sync:insert({2}) box.commit()
-- Testcase cleanup.
box.space.sync:drop()

-- [RFC, summary] switch sync replicas into async ones, expected success and
-- data consistency on a leader and replicas.
-- Testcase setup.
test_run:switch('default')
box.cfg{replication_synchro_quorum=NUM_INSTANCES, replication_synchro_timeout=1000}
_ = box.schema.space.create('sync', {is_sync=true, engine=engine})
_ = box.space.sync:create_index('pk')
-- Testcase body.
box.space.sync:insert{1}
box.space.sync:select{} -- 1
test_run:switch('replica')
box.space.sync:select{} -- 1
test_run:switch('default')
-- Disable synchronous mode.
disable_sync_mode()
-- Space is in async mode now.
box.cfg{replication_synchro_quorum=NUM_INSTANCES}
box.space.sync:insert{2} -- success
box.space.sync:insert{3} -- success
box.cfg{replication_synchro_quorum=BROKEN_QUORUM}
box.space.sync:insert{4} -- success
box.cfg{replication_synchro_quorum=NUM_INSTANCES}
box.space.sync:insert{5} -- success
box.space.sync:select{} -- 1, 2, 3, 4, 5
test_run:cmd('switch replica')
box.space.sync:select{} -- 1, 2, 3, 4, 5
-- Testcase cleanup.
test_run:switch('default')
box.space.sync:drop()

-- Warn user when setting `replication_synchro_quorum` to a value
-- greater than number of instances in a cluster, see gh-5122.
box.cfg{replication_synchro_quorum=BROKEN_QUORUM} -- warning

-- [RFC, summary] switch from leader to replica and vice versa, expected
-- success and data consistency on a leader and replicas (gh-5124).
-- Testcase setup.
test_run:switch('default')
test_run:cmd("set variable replica_url to 'replica.listen'")
box.cfg{                                                                        \
    replication_synchro_quorum = NUM_INSTANCES,                                 \
    replication_synchro_timeout = 1000,                                         \
    replication = replica_url,                                                  \
}
_ = box.schema.space.create('sync', {is_sync=true, engine=engine})
_ = box.space.sync:create_index('pk')
-- Testcase body.
box.space.sync:insert{1}
box.space.sync:select{} -- 1
test_run:switch('replica')
box.space.sync:select{} -- 1
box.cfg{read_only=false} -- promote replica to master
box.ctl.promote()
test_run:switch('default')
box.cfg{read_only=true} -- demote master to replica
test_run:switch('replica')
box.cfg{replication_synchro_quorum = 2, replication_synchro_timeout = 1000}
box.space.sync:insert{2}
box.space.sync:select{} -- 1, 2
test_run:switch('default')
box.space.sync:select{} -- 1, 2
-- Revert cluster configuration.
test_run:switch('default')
box.cfg{read_only=false}
box.ctl.promote()
test_run:switch('replica')
box.cfg{read_only=true}
-- Testcase cleanup.
test_run:switch('default')
box.space.sync:drop()
box.cfg{replication = {}}

-- Check behaviour with failed write to WAL on master (ERRINJ_WAL_IO).
-- Testcase setup.
test_run:switch('default')
box.cfg{replication_synchro_quorum=NUM_INSTANCES, replication_synchro_timeout=1000}
_ = box.schema.space.create('sync', {is_sync=true, engine=engine})
_ = box.space.sync:create_index('pk')
-- Testcase body.
box.space.sync:insert{1}
box.space.sync:select{} -- 1
box.error.injection.set('ERRINJ_WAL_IO', true)
box.space.sync:insert{2}
box.error.injection.set('ERRINJ_WAL_IO', false)
box.space.sync:select{} -- 1
test_run:switch('replica')
box.space.sync:select{} -- 1
-- Testcase cleanup.
test_run:switch('default')
box.space.sync:drop()

-- [RFC, quorum commit] check behaviour with failure answer from a replica
-- (ERRINJ_WAL_SYNC) during write, expected disconnect from the replication
-- (gh-5123, set replication_synchro_quorum to 1).
-- Testcase setup.
test_run:switch('default')
box.cfg{replication_synchro_quorum=2, replication_synchro_timeout=0.1}
_ = box.schema.space.create('sync', {is_sync=true, engine=engine})
_ = box.space.sync:create_index('pk')
-- Testcase body.
box.space.sync:insert{1}
box.space.sync:select{} -- 1
test_run:switch('replica')
box.error.injection.set('ERRINJ_WAL_IO', true)
test_run:switch('default')
box.space.sync:insert{2}
test_run:switch('replica')
box.error.injection.set('ERRINJ_WAL_IO', false)
box.space.sync:select{} -- 1
-- Testcase cleanup.
test_run:switch('default')
box.space.sync:drop()

-- Teardown.
test_run:cmd('switch default')
test_run:cmd('stop server replica')
test_run:cmd('delete server replica')
test_run:cleanup_cluster()
box.schema.user.revoke('guest', 'replication')
box.cfg{                                                                        \
    replication_synchro_quorum = orig_synchro_quorum,                           \
    replication_synchro_timeout = orig_synchro_timeout,                         \
}

-- Setup an async cluster.
box.schema.user.grant('guest', 'replication')
test_run:cmd('create server replica with rpl_master=default,\
                                         script="replication/replica.lua"')
test_run:cmd('start server replica with wait=True, wait_load=True')

-- [RFC, summary] switch async replica into sync one, expected
-- success and data consistency on a leader and replica.
-- Testcase setup.
_ = box.schema.space.create('sync', {engine=engine})
_ = box.space.sync:create_index('pk')
box.space.sync:insert{1} -- success
test_run:cmd('switch replica')
box.space.sync:select{} -- 1
test_run:switch('default')
-- Enable synchronous mode.
disable_sync_mode()
-- Space is in sync mode now.
box.cfg{replication_synchro_quorum=NUM_INSTANCES, replication_synchro_timeout=1000}
box.space.sync:insert{2} -- success
box.cfg{replication_synchro_quorum=BROKEN_QUORUM, replication_synchro_timeout=1000}
box.space.sync:insert{3} -- success
box.space.sync:select{} -- 1, 2, 3
test_run:cmd('switch replica')
box.space.sync:select{} -- 1, 2, 3
-- Testcase cleanup.
test_run:switch('default')
box.space.sync:drop()

-- Teardown.
test_run:cmd('switch default')
test_run:cmd('stop server replica')
test_run:cmd('delete server replica')
test_run:cleanup_cluster()
box.schema.user.revoke('guest', 'replication')
box.cfg{                                                                        \
    replication_synchro_quorum = orig_synchro_quorum,                           \
    replication_synchro_timeout = orig_synchro_timeout,                         \
}
box.ctl.demote()
