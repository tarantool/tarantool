test_run = require('test_run').new()

old_election_mode = box.cfg.election_mode
old_replication_timeout = box.cfg.replication_timeout

--
-- gh-5506: Raft state machine crashed in case there was a WAL write in
-- progress, and Raft was disabled + enabled back immediately. It didn't assume
-- that there can be a not finished WAL write when Raft is just enabled.
--

-- Start a WAL write and wait when it starts.
box.error.injection.set("ERRINJ_WAL_DELAY_COUNTDOWN", 0)
box.cfg{                                                                        \
    election_mode = 'candidate',                                                \
    replication_timeout = 0.1,                                                  \
}
test_run:wait_cond(function()                                                   \
    return box.error.injection.get("ERRINJ_WAL_DELAY")                          \
end)

-- Restart the state machine. It should notice the not finished WAL write and
-- continue waiting.
box.cfg{election_mode = 'off'}
box.cfg{election_mode = 'candidate'}
box.error.injection.set("ERRINJ_WAL_DELAY", false)

box.cfg{                                                                        \
    election_mode = old_election_mode,                                          \
}

--
-- Another crash could happen when election mode was configured to be
-- 'candidate' with a known leader, but there was a not finished WAL write.
-- The node tried to start waiting for the leader death, even though with an
-- active WAL write it should wait for its end first.
--
box.schema.user.grant('guest', 'super')
test_run:cmd('create server replica with rpl_master=default,\
              script="replication/replica.lua"')
test_run:cmd('start server replica with wait=True, wait_load=True')

test_run:switch('replica')
box.cfg{election_mode = 'voter'}
box.error.injection.set("ERRINJ_WAL_DELAY_COUNTDOWN", 0)

test_run:switch('default')
box.cfg{election_mode = 'candidate'}
test_run:wait_cond(function()                                                   \
    return box.info.election.state == 'leader'                                  \
end)

test_run:switch('replica')
test_run:wait_cond(function()                                                   \
    return box.error.injection.get("ERRINJ_WAL_DELAY")                          \
end)
box.cfg{election_mode = 'candidate'}
box.error.injection.set("ERRINJ_WAL_DELAY", false)

test_run:switch('default')
test_run:cmd('stop server replica')
test_run:cmd('delete server replica')

box.schema.user.revoke('guest', 'super')
box.cfg{                                                                        \
    election_mode = old_election_mode,                                          \
    replication_timeout = old_replication_timeout,                              \
}
box.ctl.demote()
