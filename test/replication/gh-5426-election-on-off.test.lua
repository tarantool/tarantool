test_run = require('test_run').new()
box.schema.user.grant('guest', 'super')

old_election_mode = box.cfg.election_mode
old_replication_timeout = box.cfg.replication_timeout

test_run:cmd('create server replica with rpl_master=default,\
              script="replication/replica.lua"')
test_run:cmd('start server replica with wait=True, wait_load=True')

--
-- gh-5426: leader resignation could crash non-candidate nodes.
--
-- Small timeout to speed up the election.
box.cfg{                                                                        \
    replication_timeout = 0.1,                                                  \
    election_mode = 'candidate',                                                \
}

-- First crash could happen when the election was disabled on the non-leader
-- node.
test_run:wait_cond(function() return box.info.election.state == 'leader' end)

test_run:switch('replica')
test_run:wait_cond(function() return box.info.election.leader ~= 0 end)

test_run:switch('default')
box.cfg{election_mode = 'off'}

test_run:switch('replica')
test_run:wait_cond(function() return box.info.election.leader == 0 end)

-- Another crash could happen if election mode was 'voter' on the non-leader
-- node.
box.cfg{election_mode = 'voter'}

test_run:switch('default')
box.cfg{election_mode = 'candidate'}
test_run:wait_cond(function() return box.info.election.state == 'leader' end)

test_run:switch('replica')
test_run:wait_cond(function() return box.info.election.leader ~= 0 end)

test_run:switch('default')
box.cfg{election_mode = 'off'}

test_run:switch('replica')
test_run:wait_cond(function() return box.info.election.leader == 0 end)

-- A crash when follower transitions from candidate to voter.
test_run:switch('default')
box.cfg{election_mode='candidate'}
test_run:wait_cond(function() return box.info.election.state == 'leader' end)
box.cfg{replication_timeout=0.01}

test_run:switch('replica')
-- A small timeout so that the timer goes off faster and the crash happens.
box.cfg{replication_timeout=0.01}
test_run:wait_cond(function() return box.info.election.leader ~= 0 end)
box.cfg{election_mode='candidate'}
box.cfg{election_mode='voter'}
-- Wait for the timer to go off.
require('fiber').sleep(4 * box.cfg.replication_timeout)

test_run:switch('default')
test_run:cmd('stop server replica')
test_run:cmd('delete server replica')
box.cfg{                                                                        \
    election_mode = old_election_mode,                                          \
    replication_timeout = old_replication_timeout,                              \
}
box.ctl.demote()
box.schema.user.revoke('guest', 'super')
