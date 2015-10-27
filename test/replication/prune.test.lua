env = require('test_run')
test_run = env.new('127.0.0.1', 8080)
replica_set = require('fast_replica')
fiber = require('fiber')

box.schema.user.grant('guest', 'read,write,execute', 'universe')

-- Create space and fill it
space = box.schema.create_space('test')
index = box.space.test:create_index('primary')
for i=1,10 do  space:insert{i, 'test'} end

-- create max number of replicas and check
replica_set.join(test_run, 30)
while #box.space._cluster:select{} ~= 31 do fiber.sleep(0.001) end

#box.space._cluster:select{}
#box.info.vclock

-- try to add one more replica
uuid = require('uuid')
box.space._cluster:insert{#box.info.vclock + 1, uuid.str()}

-- Delete all replication nodes
replica_set.drop_all(test_run)
#box.space._cluster:select{}
#box.info.vclock

-- restart and check
box.snapshot()
test_run:cmd('restart server default')
replica_set = require('fast_replica')
fiber = require('fiber')

-- Rejoin 30 new nodes and check
replica_set.join(test_run, 30)
while #box.space._cluster:select{} ~= 31 do fiber.sleep(0.001) end

-- Check server ids
test = {}
test_run:cmd('setopt delimiter ";"')
for i=1,30 do
    test[i] = test_run:cmd('eval replica' .. tostring(i) .. ' "return box.info.server.id"')
end;
test_run:cmd('setopt delimiter ""');
test

#box.space._cluster:select{}
#box.info.vclock
