test_run = require('test_run').new()
--
-- gh-1146: Raft protocol for automated leader election.
--

old_election_timeout = box.cfg.election_timeout

-- Election is turned off by default.
assert(box.cfg.election_mode == 'off')
-- Ensure election options are validated.
box.cfg{election_mode = 100}
box.cfg{election_mode = '100'}
box.cfg{election_timeout = -1}
box.cfg{election_timeout = 0}

-- When election is disabled, the instance is a follower. Does not try to become
-- a leader, and does not block write operations.
term = box.info.election.term
vote = box.info.election.vote
assert(box.info.election.state == 'follower')
assert(box.info.election.leader == 0)
assert(not box.info.ro)

-- Turned on election blocks writes until the instance becomes a leader.
box.cfg{election_mode = 'voter'}
assert(box.info.election.state == 'follower')
assert(box.info.ro)
-- Term is not changed, because the instance can't be a candidate,
-- and therefore didn't try to vote nor to bump the term.
assert(box.info.election.term == term)
assert(box.info.election.vote == vote)
assert(box.info.election.leader == 0)

-- Candidate instance votes immediately, if sees no leader.
box.cfg{election_timeout = 1000}
box.cfg{election_mode = 'candidate'}
test_run:wait_cond(function() return box.info.election.state == 'leader' end)
assert(box.info.election.term > term)
assert(box.info.election.vote == box.info.id)
assert(box.info.election.leader == box.info.id)

box.cfg{                                                                        \
    election_mode = 'off',                                                      \
    election_timeout = old_election_timeout                                     \
}

--
-- See if bootstrap with election enabled works.
--
SERVERS = {'election_replica1', 'election_replica2', 'election_replica3'}
test_run:create_cluster(SERVERS, "replication")
test_run:wait_fullmesh(SERVERS)
is_r1_leader = nil
is_r2_leader = nil
is_r3_leader = nil
is_leader_cmd = 'return box.info.election.state == \'leader\''
leader_id_cmd = 'return box.info.election.leader'
test_run:wait_cond(function()                                                   \
    is_r1_leader = test_run:eval('election_replica1', is_leader_cmd)[1]         \
    is_r2_leader = test_run:eval('election_replica2', is_leader_cmd)[1]         \
    is_r3_leader = test_run:eval('election_replica3', is_leader_cmd)[1]         \
    return is_r1_leader or is_r2_leader or is_r3_leader                         \
end)
leader_count = is_r1_leader and 1 or 0
leader_count = leader_count + (is_r2_leader and 1 or 0)
leader_count = leader_count + (is_r3_leader and 1 or 0)
assert(leader_count == 1)
-- All nodes have the same leader.
r1_leader = test_run:eval('election_replica1', leader_id_cmd)[1]
r2_leader = test_run:eval('election_replica2', leader_id_cmd)[1]
r3_leader = test_run:eval('election_replica3', leader_id_cmd)[1]
assert(r1_leader ~= 0)
assert(r1_leader == r2_leader)
assert(r1_leader == r3_leader)

--
-- Leader death starts a new election.
--
leader_name = nil
nonleader1_name = nil
nonleader2_name = nil
if is_r1_leader then                                                            \
    leader_name = 'election_replica1'                                           \
    nonleader1_name = 'election_replica2'                                       \
    nonleader2_name = 'election_replica3'                                       \
elseif is_r2_leader then                                                        \
    leader_name = 'election_replica2'                                           \
    nonleader1_name = 'election_replica1'                                       \
    nonleader2_name = 'election_replica3'                                       \
elseif is_r3_leader then                                                        \
    leader_name = 'election_replica3'                                           \
    nonleader1_name = 'election_replica1'                                       \
    nonleader2_name = 'election_replica2'                                       \
end
-- Lower the quorum so the 2 alive nodes could elect a new leader when the third
-- node dies.
test_run:switch(nonleader1_name)
box.cfg{replication_synchro_quorum = 2}
-- Switch via default where the names are defined.
test_run:switch('default')
test_run:switch(nonleader2_name)
box.cfg{replication_synchro_quorum = 2}

test_run:switch('default')
test_run:cmd(string.format('stop server %s', leader_name))
test_run:wait_cond(function()                                                   \
    is_r1_leader = test_run:eval(nonleader1_name, is_leader_cmd)[1]             \
    is_r2_leader = test_run:eval(nonleader2_name, is_leader_cmd)[1]             \
    return is_r1_leader or is_r2_leader                                         \
end)
r1_leader = test_run:eval(nonleader1_name, leader_id_cmd)[1]
r2_leader = test_run:eval(nonleader2_name, leader_id_cmd)[1]
assert(r1_leader ~= 0)
assert(r1_leader == r2_leader)

test_run:cmd(string.format('start server %s', leader_name))

test_run:drop_cluster(SERVERS)
