print '-------------------------------------------------------------'
print 'gh-806: cant prune old replicas by deleting their server ids'
print '-------------------------------------------------------------'

env = require('test_run')
test_run = env.new('127.0.0.1', 8080)
replica_set = require('fast_replica')
fiber = require('fiber')

box.space._cluster:len() == 1
#box.info.vclock == 1

box.schema.user.grant('guest', 'read,write,execute', 'universe')

-- Create space and fill it
space = box.schema.create_space('test')
index = box.space.test:create_index('primary')
for i=1,10 do  space:insert{i, 'test'} end

-- create max number of replicas and check
replica_set.join(test_run, box.schema.REPLICA_MAX - 2)
while box.space._cluster:len() ~= box.schema.REPLICA_MAX - 1 do fiber.sleep(0.001) end

box.space._cluster:len() == box.schema.REPLICA_MAX - 1
#box.info.vclock == box.schema.REPLICA_MAX - 1

-- try to add one more replica
uuid = require('uuid')
box.space._cluster:insert{box.schema.REPLICA_MAX, uuid.str()}

-- Delete all replication nodes
replica_set.drop_all(test_run)
box.space._cluster:len() == 1
#box.info.vclock == 1

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
test_run:cmd('eval replica1 "return box.info.server.id"')

box.space._cluster:len() == 2
#box.info.vclock == 2

-- Cleanup
replica_set.drop_all(test_run)
box.space._cluster:len() == 1
#box.info.vclock == 1
box.space.test:drop()
box.schema.user.revoke('guest', 'read,write,execute', 'universe')
