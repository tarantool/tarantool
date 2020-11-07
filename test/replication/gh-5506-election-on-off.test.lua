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
    replication_timeout = old_replication_timeout,                              \
}
