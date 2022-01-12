test_run = require('test_run').new()
box.schema.user.grant('guest', 'super')

old_election_mode = box.cfg.election_mode
old_election_timeout = box.cfg.election_timeout
old_replication_synchro_timeout = box.cfg.replication_synchro_timeout
old_replication_timeout = box.cfg.replication_timeout
old_replication = box.cfg.replication
old_synchro_quorum = box.cfg.replication_synchro_quorum

test_run:cmd('create server replica with rpl_master=default,\
              script="replication/replica.lua"')
test_run:cmd('start server replica with wait=True, wait_load=True')
-- Any election activities require fullmesh.
box.cfg{replication = test_run:eval('replica', 'box.cfg.listen')[1]}

--
-- gh-5339: leader election manages transaction limbo automatically.
--
-- Idea of the test is that there are 2 nodes. A leader and a
-- follower. The leader creates a synchronous transaction, it gets
-- replicated to the follower, the leader dies. Now when the
-- follower is elected as a new leader, it should finish the
-- pending transaction.
--
_ = box.schema.create_space('test', {is_sync = true})
_ = _:create_index('pk')
box.cfg{election_mode = 'voter'}

test_run:switch('replica')
fiber = require('fiber')
-- Replication timeout is small to speed up a first election start.
-- Election timeout is set to a huge value to ensure the election does not hang
-- anywhere. Indeed, there can't be a split-vote when candidate is only one.
box.cfg{                                                                        \
    election_mode = 'candidate',                                                \
    election_timeout = 1000000,                                                 \
    replication_synchro_quorum = 3,                                             \
    replication_synchro_timeout = 1000000,                                      \
    replication_timeout = 0.1,                                                  \
}

-- Promote is written asynchronously to the instance becoming the leader, so
-- wait for it. As soon as it's written, the instance's definitely a leader.
test_run:wait_cond(function()                                                   \
    return box.info.synchro.queue.owner == box.info.id                          \
end)
assert(box.info.election.state == 'leader')

lsn = box.info.lsn
_ = fiber.create(function()                                                     \
    ok, err = pcall(box.space.test.replace, box.space.test, {1})                \
end)
-- Wait WAL write.
test_run:wait_cond(function() return box.info.lsn > lsn end)
-- Wait replication to the other instance.
test_run:wait_lsn('default', 'replica')

test_run:switch('default')
test_run:cmd('stop server replica')
test_run:cmd('delete server replica')
-- Will fail - the node is not a leader.
ok, err = pcall(box.space.test.replace, box.space.test, {2})
assert(not ok)
assert(err.code == box.error.READONLY)

-- Set synchro timeout to a huge value to ensure, that when a leader is elected,
-- it won't wait for this timeout.
box.cfg{replication_synchro_timeout = 1000000}

-- Configure separately from synchro timeout not to depend on the order of
-- synchro and election options appliance. Replication timeout is tiny to speed
-- up notice of the old leader death.
-- Election timeout is set to a huge value to ensure the election does not hang
-- anywhere. Indeed, there can't be a split-vote when candidate is only one.
box.cfg{                                                                        \
    election_mode = 'candidate',                                                \
    election_timeout = 1000000,                                                 \
    replication_timeout = 0.01,                                                 \
    replication_synchro_quorum = 1,                                             \
}

box.ctl.wait_rw()
assert(box.info.election.state == 'leader')
_ = box.space.test:replace{2}
box.space.test:select{}
box.space.test:drop()

box.cfg{                                                                        \
    election_mode = old_election_mode,                                          \
    election_timeout = old_election_timeout,                                    \
    replication_timeout = old_replication_timeout,                              \
    replication = old_replication,                                              \
    replication_synchro_timeout = old_replication_synchro_timeout,              \
    replication_synchro_quorum = old_synchro_quorum,                            \
}
box.ctl.demote()
box.schema.user.revoke('guest', 'super')
