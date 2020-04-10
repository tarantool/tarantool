env = require('test_run')
vclock_diff = require('fast_replica').vclock_diff
test_run = env.new()


--
-- gh-3186 Anonymous replicas.
--
-- Prepare master.
box.schema.user.grant('guest', 'replication')
_ = box.schema.space.create('loc', {is_local=true})
_ = box.schema.space.create('temp', {temporary=true})
_ = box.schema.space.create('test')
_ = box.space.loc:create_index('pk')
_ = box.space.temp:create_index('pk')
_ = box.space.test:create_index('pk')
box.space.test:insert{1}

test_run:cmd('create server replica_anon with rpl_master=default, script="replication/anon1.lua"')
test_run:cmd('start server replica_anon')
test_run:cmd('switch replica_anon')

box.info.status
box.info.id
box.info.lsn
test_run:wait_upstream(1, {status='follow'})

-- Temporary spaces are accessible as read / write.
for i = 1,10 do box.space.temp:insert{i} end
box.space.temp:select{}

box.info.lsn

-- Same for local spaces.
for i = 1,10 do box.space.loc:insert{i} end
box.space.loc:select{}

-- Replica-local changes are accounted for in 0 vclock component.
box.info.lsn
box.info.vclock[0]

-- Replica is read-only.
box.cfg.read_only
box.cfg{read_only=false}

box.space.test:insert{2}

box.space.loc:drop()
box.space.loc:truncate()

test_run:cmd('switch default')

-- Replica isn't visible on master.
#box.info.replication

-- Test that replication (even anonymous) from an anonymous
-- instance is forbidden. An anonymous replica will fetch
-- a snapshot though.
test_run:cmd([[create server replica_anon2 with rpl_master=replica_anon,\
             script="replication/anon2.lua"]])
test_run:cmd('start server replica_anon2')
test_run:wait_log('replica_anon2',\
                  'Replication does not support replicating from an anonymous instance',\
                  nil, 10)
test_run:cmd('switch replica_anon2')
a = box.info.vclock[1]
-- The instance did fetch a snapshot.
a > 0
-- 0-th vclock component isn't propagated across the cluster.
box.info.vclock[0]
test_run:cmd('switch default')
box.space.test:insert{2}
test_run:cmd("switch replica_anon2")
-- Second replica doesn't follow master through the
-- 1st one. Replication from an anonymous instance
-- is forbidden indeed.
box.info.vclock[1] == a or box.info.vclock[1]

test_run:cmd('switch replica_anon')

test_run:cmd('stop server replica_anon2')
test_run:cmd('delete server replica_anon2')

-- Promote anonymous replica.
box.cfg{replication_anon=false}
-- Cannot switch back after becoming "normal".
box.cfg{replication_anon=true}

box.info.id
#box.info.replication
test_run:wait_upstream(1, {status='follow'})
box.info.replication.downstream

old_lsn = box.info.vclock[2] or 0

-- Now read_only can be turned off.
box.cfg{read_only=false}
box.space.test:insert{3}
-- New changes are tracked under freshly assigned id.
box.info.vclock[2] == old_lsn + 1

test_run:cmd('switch default')

-- Other instances may replicate from a previously-anonymous one.
test_run:cmd("set variable repl_source to 'replica_anon.listen'")
box.cfg{replication=repl_source}
#box.info.replication
test_run:wait_upstream(2, {status='follow'})
test_run:wait_downstream(2, {status='follow'})
#box.info.vclock

-- Cleanup.
box.cfg{replication=""}
test_run:cmd('stop server replica_anon')
test_run:cmd('delete server replica_anon')
box.space.test:drop()
box.space.temp:drop()
box.space.loc:drop()
box.schema.user.revoke('guest', 'replication')
test_run:cleanup_cluster()

--
-- Check that in case of a master absence an anon replica can't
-- deanonymize itself, regardless of quorum value.
--
test_run:cmd('create server master with script="replication/master1.lua"')
test_run:cmd('start server master')
test_run:switch('master')
box.schema.user.grant('guest', 'replication')
test_run:cmd('create server replica_anon with rpl_master=master, script="replication/anon1.lua"')
test_run:cmd('start server replica_anon')
test_run:switch('replica_anon')
box.cfg{replication_connect_quorum = 0}
test_run:cmd('stop server master')
test_run:cmd('delete server master')
box.cfg{replication_anon = false}
test_run:switch('default')
test_run:cmd('stop server replica_anon')
test_run:cmd('delete server replica_anon')
