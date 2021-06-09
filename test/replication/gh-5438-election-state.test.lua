test_run = require('test_run').new()

--
-- gh-5428 send out Raft state to subscribers, even when Raft is disabled.
--
-- Bump Raft term while the replica's offline.
term = box.info.election.term
old_election_mode = box.cfg.election_mode
box.cfg{election_mode = 'candidate'}
test_run:wait_cond(function() return box.info.election.term > term end)

-- Make sure the replica receives new term on subscribe.
box.cfg{election_mode = 'off'}

box.schema.user.grant('guest', 'replication')
test_run:cmd('create server replica with rpl_master=default,\
                                         script="replication/replica.lua"')
test_run:cmd('start server replica')
test_run:wait_cond(function()\
    return test_run:eval('replica', 'return box.info.election.term')[1] ==\
           box.info.election.term\
end)

-- Cleanup.
box.ctl.demote()
box.cfg{election_mode = old_election_mode}
test_run:cmd('stop server replica')
test_run:cmd('delete server replica')
box.schema.user.revoke('guest', 'replication')
