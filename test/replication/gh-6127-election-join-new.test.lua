test_run = require('test_run').new()

--
-- gh-6127: the algorithm selecting a node from which to join to a replicaset
-- should take into account who is the leader (is writable and can write to
-- _cluster) and who is a follower/candidate.
--
test_run:cmd('create server master1 with script="replication/gh-6127-master1.lua"')
test_run:cmd('start server master1 with wait=False')
test_run:cmd('create server master2 with script="replication/gh-6127-master2.lua"')
test_run:cmd('start server master2')

test_run:switch('master1')
box.cfg{election_mode = 'voter'}
test_run:switch('master2')
-- Perform manual election because it is faster - the automatic one still tries
-- to wait for 'death timeout' first which is several seconds.
box.cfg{                                                                        \
    election_mode = 'manual',                                                   \
    election_timeout = 0.1,                                                     \
}
box.ctl.promote()
box.ctl.wait_rw()
-- Make sure the other node received the promotion row. Vclocks now should be
-- equal so the new node would select only using read-only state and min UUID.
test_run:wait_lsn('master1', 'master2')

-- Min UUID is master1, but it is not writable. Therefore must join from
-- master2.
test_run:cmd('create server replica with script="replication/gh-6127-replica.lua"')
test_run:cmd('start server replica')
test_run:switch('replica')
assert(box.info.leader ~= 0)

test_run:switch('default')
test_run:cmd('stop server replica')
test_run:cmd('delete server replica')
test_run:cmd('stop server master2')
test_run:cmd('delete server master2')
test_run:cmd('stop server master1')
test_run:cmd('delete server master1')
