test_run = require('test_run').new()

--
-- gh-5566 replica tolerates synchronous transactions in final join stream.
--
_ = box.schema.space.create('sync', {is_sync=true})
_ = box.space.sync:create_index('pk')
box.ctl.promote()

box.schema.user.grant('guest', 'replication')
box.schema.user.grant('guest', 'write', 'space', 'sync')

-- Part 1. Make sure a joining instance tolerates synchronous rows in final join
-- stream.
trig = function()\
    box.space.sync:replace{1}\
end
-- The trigger will generate synchronous rows each time a replica joins.
_ = box.space._cluster:on_replace(trig)

orig_synchro_quorum = box.cfg.replication_synchro_quorum
orig_synchro_timeout = box.cfg.replication_synchro_timeout
box.cfg{replication_synchro_quorum=1, replication_synchro_timeout=60}

test_run:cmd('create server replica with rpl_master=default,\
                                         script="replication/replica.lua"')
test_run:cmd('start server replica')

test_run:switch('replica')
test_run:wait_upstream(1, {status='follow'})

test_run:switch('default')
test_run:cmd('stop server replica')
test_run:cmd('delete server replica')

-- Part 2. Make sure master aborts final join if insert to _cluster is rolled
-- back and replica is capable of retrying it.
-- Make the trigger we used above fail with no quorum.
box.cfg{replication_synchro_quorum=2, replication_synchro_timeout=0.01}
-- Try to join the replica once again.
test_run:cmd('create server replica with rpl_master=default,\
                                         script="replication/replica.lua"')
test_run:cmd('start server replica with wait=False')

test_run:wait_log('replica', 'SYNC_QUORUM_TIMEOUT', nil, 60)
-- Remove the trigger to let the replica connect.
box.space._cluster:on_replace(nil, trig)

test_run:switch('replica')
test_run:wait_upstream(1, {status='follow'})

-- Cleanup.
test_run:switch('default')
test_run:cmd('stop server replica')
test_run:cmd('delete server replica')
box.cfg{\
    replication_synchro_quorum=orig_synchro_quorum,\
    replication_synchro_timeout=orig_synchro_timeout\
}
box.space.sync:drop()
test_run:cleanup_cluster()
box.schema.user.revoke('guest', 'replication')
box.ctl.demote()
