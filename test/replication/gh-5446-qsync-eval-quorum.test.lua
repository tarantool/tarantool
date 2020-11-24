test_run = require('test_run').new()
engine = test_run:get_cfg('engine')

box.schema.user.grant('guest', 'replication')

-- Save old settings for cleanup sake
old_synchro_quorum = box.cfg.replication_synchro_quorum
old_synchro_timeout = box.cfg.replication_synchro_timeout

-- Test syntax error
box.cfg{replication_synchro_quorum = "aaa"}

-- Test out of bounds values
box.cfg{replication_synchro_quorum = "N+1"}
box.cfg{replication_synchro_quorum = "N-1"}

-- Test big number value
box.cfg{replication_synchro_quorum = '4294967296 + 1'}
box.cfg{replication_synchro_quorum = 'N + 4294967296'}
box.cfg{replication_synchro_quorum = 4294967297}

-- Timeouts for replication
function cfg_set_pass_tmo() box.cfg{replication_synchro_timeout = 1000} end
function cfg_set_fail_tmo() box.cfg{replication_synchro_timeout = 0.5} end

-- Use canonical majority formula
box.cfg{replication_synchro_quorum = "N/2+1"}
cfg_set_pass_tmo()

-- Create a sync space we will operate on
s = box.schema.space.create('sync', {is_sync = true, engine = engine})
_ = s:create_index('pk')

-- Only one master node -> 1/2 + 1 = 1
s:insert{1} -- should pass

-- 1 replica, 2 nodes -> replication_synchro_quorum = 2/2 + 1 = 2
test_run:cmd('create server replica1 with rpl_master=default,\
              script="replication/replica-quorum-1.lua"')
test_run:cmd('start server replica1 with wait=True, wait_load=True')
s:insert{2} -- should pass
cfg_set_fail_tmo()
test_run:cmd('stop server replica1')
s:insert{3} -- should fail
cfg_set_pass_tmo()
test_run:cmd('start server replica1 with wait=True, wait_load=True')
s:insert{3} -- should pass

-- 6 replicas, 7 nodes -> replication_synchro_quorum = 7/2 + 1 = 4
test_run:cmd('create server replica2 with rpl_master=default,\
              script="replication/replica-quorum-2.lua"')
test_run:cmd('start server replica2 with wait=True, wait_load=True')

test_run:cmd('create server replica3 with rpl_master=default,\
              script="replication/replica-quorum-3.lua"')
test_run:cmd('start server replica3 with wait=True, wait_load=True')

test_run:cmd('create server replica4 with rpl_master=default,\
              script="replication/replica-quorum-4.lua"')
test_run:cmd('start server replica4 with wait=True, wait_load=True')

test_run:cmd('create server replica5 with rpl_master=default,\
              script="replication/replica-quorum-5.lua"')
test_run:cmd('start server replica5 with wait=True, wait_load=True')

test_run:cmd('create server replica6 with rpl_master=default,\
              script="replication/replica-quorum-6.lua"')
test_run:cmd('start server replica6 with wait=True, wait_load=True')

-- All replicas are up and running
s:insert{4} -- should pass

-- Now start stopping replicas until hit quorum limit
test_run:cmd('stop server replica1') -- 5 replicas, 6 nodes
s:insert{5} -- should pass
test_run:cmd('stop server replica2') -- 4 replicas, 5 nodes
s:insert{6} -- should pass
test_run:cmd('stop server replica3') -- 3 replicas, 4 nodes
s:insert{7} -- should pass
cfg_set_fail_tmo()
test_run:cmd('stop server replica4') -- 2 replicas, 3 nodes
s:insert{8} -- should fail
-- Bring back one to hit the quorum
cfg_set_pass_tmo()
test_run:cmd('start server replica4 with wait=True, wait_load=True')
s:insert{8} -- should pass
-- And finally all replicas are back one by one
test_run:cmd('start server replica3 with wait=True, wait_load=True')
s:insert{9} -- should pass
test_run:cmd('start server replica2 with wait=True, wait_load=True')
s:insert{10} -- should pass
test_run:cmd('start server replica1 with wait=True, wait_load=True')
s:insert{11} -- should pass

-- cleanup

test_run:cmd('stop server replica1')
test_run:cmd('delete server replica1')
test_run:cmd('stop server replica2')
test_run:cmd('delete server replica2')
test_run:cmd('stop server replica3')
test_run:cmd('delete server replica3')
test_run:cmd('stop server replica4')
test_run:cmd('delete server replica4')
test_run:cmd('stop server replica5')
test_run:cmd('delete server replica5')
test_run:cmd('stop server replica6')
test_run:cmd('delete server replica6')

s:drop()

box.schema.user.revoke('guest', 'replication')

box.cfg{                                                                        \
    replication_synchro_quorum = old_synchro_quorum,                            \
    replication_synchro_timeout = old_synchro_timeout,                          \
}
