test_run = require('test_run').new()

--
-- gh-5613: when a new replica is joined to a cluster, it must prefer
-- bootstrapped join sources over non-bootstrapped ones, including self. Even
-- if all the bootstrapped ones are read-only. Otherwise the node might have
-- decided to bootstrap a new cluster on its own and won't be able to join the
-- existing one forever.
--

test_run:cmd('create server master with script="replication/gh-5613-master.lua"')
test_run:cmd('start server master with wait=False')
test_run:cmd('create server replica1 with script="replication/gh-5613-replica1.lua"')
test_run:cmd('start server replica1')
test_run:switch('master')
box.cfg{read_only = true}
test_run:switch('default')

test_run:cmd('create server replica2 with script="replication/gh-5613-replica2.lua"')
-- It returns false, but it is expected.
test_run:cmd('start server replica2 with crash_expected=True')
opts = {filename = 'gh-5613-replica2.log'}
assert(test_run:grep_log(nil, 'ER_READONLY', nil, opts) ~= nil)

test_run:cmd('delete server replica2')
test_run:cmd('stop server replica1')
test_run:cmd('delete server replica1')
test_run:cmd('stop server master')
test_run:cmd('delete server master')
