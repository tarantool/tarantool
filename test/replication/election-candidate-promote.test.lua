test_run = require('test_run').new()

SERVERS = {'election_replica1', 'election_replica2', 'election_replica3'}
test_run:create_cluster(SERVERS, 'replication')
test_run:wait_fullmesh(SERVERS)
is_leader_cmd = 'return box.info.election.state == \'leader\''

test_run:cmd("setopt delimiter ';'")
function get_leader_nr()
    local leader_nr = 0
    test_run:wait_cond(function()
        for nr = 1,3 do
            local is_leader = test_run:eval('election_replica'..nr, is_leader_cmd)[1]
            if is_leader then
                leader_nr = nr
                return true
            end
        end
        return false
    end)
    return leader_nr
end;
test_run:cmd("setopt delimiter ''");

leader_nr = get_leader_nr()

assert(leader_nr ~= 0)

term = test_run:eval('election_replica'..leader_nr,\
                     'return box.info.election.term')[1]

next_nr = leader_nr % 3 + 1
-- Someone else may become a leader, thus promote may fail. But we're testing
-- that it takes effect at all, so that's fine.
_ = pcall(test_run.eval, test_run, 'election_replica'..next_nr, 'box.ctl.promote()')
new_term = test_run:eval('election_replica'..next_nr,\
                         'return box.info.election.term')[1]
assert(new_term > term)
leader_nr = get_leader_nr()
assert(leader_nr ~= 0)

test_run:drop_cluster(SERVERS)
