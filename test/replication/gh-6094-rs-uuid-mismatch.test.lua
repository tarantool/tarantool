test_run = require('test_run').new()

--
-- gh-6094: master instance didn't check if the subscribed instance has the same
-- replicaset UUID as its own. As a result, if the peer is from a different
-- replicaset, the master couldn't find its record in _cluster, and assumed it
-- simply needs to wait a bit more. This led to an infinite re-subscribe.
--
box.schema.user.grant('guest', 'super')

test_run:cmd('create server master2 with script="replication/master1.lua"')
test_run:cmd('start server master2')
test_run:switch('master2')
replication = test_run:eval('default', 'return box.cfg.listen')[1]
box.cfg{replication = {replication}}
assert(test_run:grep_log('master2', 'ER_REPLICASET_UUID_MISMATCH'))
info = box.info
repl_info = info.replication[1]
assert(not repl_info.downstream and not repl_info.upstream)
assert(info.status == 'orphan')

test_run:switch('default')
test_run:cmd('stop server master2')
test_run:cmd('delete server master2')
box.schema.user.revoke('guest', 'super')
