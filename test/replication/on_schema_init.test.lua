env = require('test_run')
test_run = env.new()

-- gh-3159: on_schema_init triggers

-- the replica has set an on_schema_init trigger, which will set
-- _space:before_replace triggers to change 'test_engine' space engine
-- and 'test_local' space is_local flag when replication starts.
test_run:cmd('create server replica with rpl_master=default, script="replication/replica_on_schema_init.lua"')

test_engine = box.schema.space.create('test_engine', {engine='memtx'})
-- Make sure that space.before_replace trigger is invoked for rows
-- received during both initial and final join stages.
box.snapshot()
test_local =  box.schema.space.create('test_local', {is_local=false})
test_engine.engine
test_local.is_local

_ = test_engine:create_index("pk")
_ = test_local:create_index("pk")

test_engine:insert{1}
test_local:insert{2}

box.schema.user.grant('guest', 'replication')

test_run:cmd('start server replica')
test_run:cmd('switch replica')

box.space.test_engine.engine
box.space.test_local.is_local

box.space.test_engine:insert{3}
box.space.test_local:insert{4}

box.space.test_engine:select{}
box.space.test_local:select{}

test_run:cmd('switch default')

test_run:cmd('set variable replica_port to "replica.listen"')
box.cfg{replication=replica_port}
test_engine:select{}
-- the space truly became local on replica
test_local:select{}

box.cfg{replication=nil}
test_run:cmd('stop server replica with cleanup=1')
test_run:cleanup_cluster()
box.schema.user.revoke('guest', 'replication')
test_engine:drop()
test_local:drop()
