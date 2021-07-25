--
-- gh-5430: when MVCC was enabled for memtx, new replica registration attempt
-- could fail with 'duplicate error' in _cluster space. This was happening,
-- because _cluster is memtx. Changes to it were not visible for newer requests
-- until commit.
-- New replica ID was looked up in the space by its full scan. The scan used a
-- plain iterator and didn't see replicas, whose registration was in progress of
-- being written to WAL.
-- As a result, if 2 replicas came to register at the same time, they got the
-- same replica ID because didn't see each other in _cluster. One of them would
-- fail to register in the end due to the conflict.
--
-- The test reproduces it by doing anon replica registration. Because during
-- normal join there are more than one access to WAL, the ID assignment is the
-- last one. It makes hard to block the ID assignment only. With anon replica
-- ID assignment the join is already done, the only WAL write is the ID
-- assignment, easy to block and yet not block new replicas registration
-- attempts.
--
test_run = require('test_run').new()

test_run:cmd('create server master with '..                                     \
             'script="replication/gh-5430-mvcc-master.lua"')
test_run:cmd('start server master')

test_run:cmd('create server replica1 with '..                                   \
             'script="replication/gh-5430-mvcc-replica1.lua"')
test_run:cmd('start server replica1')

test_run:cmd('create server replica2 with '..                                   \
             'script="replication/gh-5430-mvcc-replica2.lua"')
test_run:cmd('start server replica2')

test_run:switch('master')
box.error.injection.set('ERRINJ_WAL_DELAY', true)

test_run:switch('replica1')
_ = require('fiber').create(function() box.cfg{replication_anon = false} end)
str = string.format('registering replica %s', box.info.uuid):gsub('-', '%%-')
_ = test_run:wait_log('master', str)

test_run:switch('replica2')
_ = require('fiber').create(function() box.cfg{replication_anon = false} end)
str = string.format('registering replica %s', box.info.uuid):gsub('-', '%%-')
_ = test_run:wait_log('master', str)

test_run:switch('master')
box.error.injection.set('ERRINJ_WAL_DELAY', false)

test_run:switch('replica1')
test_run:wait_cond(function() return box.info.id > 1 end)

test_run:switch('replica2')
test_run:wait_cond(function() return box.info.id > 1 end)

test_run:switch('default')
test_run:cmd('stop server replica2')
test_run:cmd('delete server replica2')
test_run:cmd('stop server replica1')
test_run:cmd('delete server replica1')
test_run:cmd('stop server master')
test_run:cmd('delete server master')
