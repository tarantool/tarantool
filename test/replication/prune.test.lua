print '-------------------------------------------------------------'
print 'gh-806: cant prune old replicas by deleting their server ids'
print '-------------------------------------------------------------'

env = require('test_run')
test_run = env.new()
engine = test_run:get_cfg('engine')
replica_set = require('fast_replica')
fiber = require('fiber')

test_run:cleanup_cluster()

box.space._cluster:len() == 1

box.schema.user.grant('guest', 'read,write,execute', 'universe')

-- Create space and fill it
space = box.schema.create_space('test', {engine = engine})
index = box.space.test:create_index('primary')
for i=1,10 do  space:insert{i, 'test'} end

-- create max number of replicas and check
replica_set.join(test_run, box.schema.REPLICA_MAX - 2)
while box.space._cluster:len() ~= box.schema.REPLICA_MAX - 1 do fiber.sleep(0.001) end

box.space._cluster:len() == box.schema.REPLICA_MAX - 1

-- try to add one more replica
uuid = require('uuid')
box.space._cluster:insert{box.schema.REPLICA_MAX, uuid.str()}

-- Delete all replication nodes
replica_set.drop_all(test_run)
box.space._cluster:len() == 1

-- Save a snapshot without removed replicas in vclock
box.snapshot()
-- Master is not crashed then recovering xlog with {replica_id: 0} in header
test_run:cmd('restart server default')
replica_set = require('fast_replica')
fiber = require('fiber')

-- Rejoin replica and check
replica_set.join(test_run, 1)
while box.space._cluster:len() ~= 2 do fiber.sleep(0.001) end

-- Check server ids
test_run:cmd('eval replica1 "return box.info.id"')

box.space._cluster:len() == 2

-- Cleanup
replica_set.drop_all(test_run)
box.space._cluster:len() == 1

-- delete replica from master
replica_set.join(test_run, 1)
while box.space._cluster:len() ~= 2 do fiber.sleep(0.001) end
-- Check server ids
test_run:cmd('eval replica1 "return box.info.id"')
box.space._cluster:len() == 2
replica_set.unregister(test_run, 2)

while test_run:cmd('eval replica1 "box.info.replication[1].upstream.status"')[1] ~= 'stopped' do fiber.sleep(0.001) end
test_run:cmd('eval replica1 "box.info.replication[1].upstream.message"')

-- restart replica and check that replica isn't able to join to cluster
test_run:cmd('stop server replica1')
test_run:cmd('start server replica1 with args="true", wait=False, crash_expected=True')
test_run:cmd('switch replica1')
test_run:wait_upstream(1, {message_re = "Can't subscribe non%-anonymous replica"})
test_run:cmd('switch default')
box.space._cluster:len() == 1
test_run:cmd('eval replica1 "box.info.replication[1].upstream.status"')
test_run:cmd('eval replica1 "box.info.replication[1].upstream.message"')[1]:match("Can't subscribe non%-anonymous replica") ~= nil
replica_set.delete(test_run, 2)

box.space.test:drop()


box.schema.user.revoke('guest', 'read,write,execute', 'universe')
