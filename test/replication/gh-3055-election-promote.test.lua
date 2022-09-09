test_run = require('test_run').new()

--
-- gh-3055 box.ctl.promote(). Call on instance with election_mode='manual'
-- in order to promote it to leader.
SERVERS = {'election_replica1', 'election_replica2', 'election_replica3'}
-- Start in candidate state in order for bootstrap to work.
test_run:create_cluster(SERVERS, 'replication', {args='2 0.1 candidate'})
test_run:wait_fullmesh(SERVERS)

cfg_set_voter =\
    "box.cfg{election_mode='voter'} "..\
    "assert(box.info.election.state == 'follower') "..\
    "assert(box.info.ro)"

for _, server in pairs(SERVERS) do\
    ok, res = test_run:eval(server, cfg_set_voter)\
    assert(ok)\
end

-- Promote without living leader.
test_run:switch('election_replica1')
box.cfg{election_mode='manual'}
assert(box.info.election.state == 'follower')
term = box.info.election.term
box.ctl.promote()
assert(box.info.election.state == 'leader')
test_run:wait_cond(function() return not box.info.ro end)
assert(box.info.election.term > term)

-- Test promote when there's a live leader.
test_run:switch('election_replica2')
term = box.info.election.term
box.cfg{election_mode='manual'}
assert(box.info.election.state == 'follower')
assert(box.info.ro)
assert(box.info.election.leader ~= 0)
box.ctl.promote()
assert(box.info.election.state == 'leader')
test_run:wait_cond(function() return not box.info.ro end)
assert(box.info.election.term > term)

-- Cleanup.
test_run:switch('default')
test_run:drop_cluster(SERVERS)
